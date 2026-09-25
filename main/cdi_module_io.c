#include "cdi_module_io.h"
#include "cdi_board_esp32.h"
#include "cdi_r5.h"

#include "driver/i2c_master.h"
#include <string.h>

#define MOD_I2C_PORT I2C_NUM_0
#define DET_MASK 0x1fu
#define REQUEST_MASK 0x0fu
#define EXP1_BIT (1u << 5)
#define EXP2_BIT (1u << 6)
#define EXP3_BIT (1u << 7)
#define IO_TIMEOUT_MS 5

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_u6;
static i2c_master_dev_handle_t s_u7;

static esp_err_t add_device(uint8_t address, i2c_master_dev_handle_t *device)
{
    i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = 100000u,
    };
    return i2c_master_bus_add_device(s_bus, &config, device);
}

static esp_err_t init_bus(void)
{
    if (s_bus != NULL && s_u6 != NULL && s_u7 != NULL) return ESP_OK;

    i2c_master_bus_config_t config = {
        .i2c_port = MOD_I2C_PORT,
        .sda_io_num = CDI_PIN_MOD_I2C_SDA,
        .scl_io_num = CDI_PIN_MOD_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7u,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags.enable_internal_pullup = false,
    };
    esp_err_t err = i2c_new_master_bus(&config, &s_bus);
    if (err != ESP_OK) return err;

    err = add_device(CDI_MOD_I2C_ADDRESS, &s_u6);
    if (err == ESP_OK) err = add_device(CDI_REQ_I2C_ADDRESS, &s_u7);
    if (err == ESP_OK) return ESP_OK;

    if (s_u7 != NULL) {
        i2c_master_bus_rm_device(s_u7);
        s_u7 = NULL;
    }
    if (s_u6 != NULL) {
        i2c_master_bus_rm_device(s_u6);
        s_u6 = NULL;
    }
    i2c_del_master_bus(s_bus);
    s_bus = NULL;
    return err;
}

static esp_err_t write_byte(i2c_master_dev_handle_t device, uint8_t value)
{
    if (device == NULL) return ESP_ERR_INVALID_STATE;
    return i2c_master_transmit(device, &value, 1u, IO_TIMEOUT_MS);
}

static esp_err_t write_latch(const cdi_module_io_t *io)
{
    return write_byte(s_u6, io->output_latch);
}

void cdi_aux_input_config_defaults(cdi_aux_input_config_t *config)
{
    if (config == NULL) return;
    memset(config, 0, sizeof(*config));
    config->magic = CDI_AUX_INPUT_MAGIC;
    config->version = CDI_AUX_INPUT_VERSION;
    config->vehicle_profile = CDI_AUX_PROFILE_NS200;
}

bool cdi_aux_input_config_valid(const cdi_aux_input_config_t *config)
{
    return config != NULL && config->magic == CDI_AUX_INPUT_MAGIC &&
           config->version == CDI_AUX_INPUT_VERSION &&
           config->enabled <= 1u &&
           config->vehicle_profile <= CDI_AUX_PROFILE_UNIVERSAL_MATIC;
}

esp_err_t cdi_module_io_init(cdi_module_io_t *io)
{
    if (io == NULL) return ESP_ERR_INVALID_ARG;
    memset(io, 0, sizeof(*io));
    /* Both expanders power up HIGH. U6 EXP outputs therefore remain OFF. */
    io->output_latch = 0xffu;
    esp_err_t err = init_bus();
    if (err == ESP_OK) err = write_latch(io);
    io->healthy = err == ESP_OK;

    esp_err_t request_err = err == ESP_OK ?
        write_byte(s_u7, 0xffu) : err;
    io->request_healthy = request_err == ESP_OK;
    return err != ESP_OK ? err : request_err;
}

static bool poll_u6(cdi_module_io_t *io)
{
    uint8_t raw = 0xffu;
    esp_err_t err = s_u6 == NULL ? ESP_ERR_INVALID_STATE :
        i2c_master_receive(s_u6, &raw, 1u, IO_TIMEOUT_MS);
    if (err != ESP_OK) {
        if (io->consecutive_errors < 255u) ++io->consecutive_errors;
        if (io->consecutive_errors >= 3u) {
            io->healthy = false;
            io->present_mask = 0u;
        }
        return false;
    }
    io->consecutive_errors = 0u;
    io->healthy = true;
    uint8_t candidate = (uint8_t)(~raw) & DET_MASK;
    if (candidate != io->candidate_mask) {
        io->candidate_mask = candidate;
        io->stable_samples = 1u;
    } else if (io->stable_samples < 3u) {
        ++io->stable_samples;
    }
    if (io->stable_samples >= 3u)
        io->present_mask = io->candidate_mask & CDI_R9_MODULE_ALL;
    return true;
}

static bool poll_u7(cdi_module_io_t *io)
{
    uint8_t raw = 0xffu;
    esp_err_t err = s_u7 == NULL ? ESP_ERR_INVALID_STATE :
        i2c_master_receive(s_u7, &raw, 1u, IO_TIMEOUT_MS);
    if (err != ESP_OK) {
        if (io->request_consecutive_errors < 255u)
            ++io->request_consecutive_errors;
        if (io->request_consecutive_errors >= 3u) {
            io->request_healthy = false;
            io->request_mask = 0u;
        }
        return false;
    }
    io->request_consecutive_errors = 0u;
    io->request_healthy = true;
    uint8_t candidate = (uint8_t)(~raw) & REQUEST_MASK;
    if (candidate != io->request_candidate_mask) {
        io->request_candidate_mask = candidate;
        io->request_stable_samples = 1u;
    } else if (io->request_stable_samples < 3u) {
        ++io->request_stable_samples;
    }
    if (io->request_stable_samples >= 3u)
        io->request_mask = io->request_candidate_mask;
    return true;
}

bool cdi_module_io_poll(cdi_module_io_t *io)
{
    if (io == NULL) return false;
    bool u6_ok = poll_u6(io);
    bool u7_ok = poll_u7(io);
    return u6_ok && u7_ok;
}

uint8_t cdi_module_io_present_mask(const cdi_module_io_t *io)
{ return io != NULL && io->healthy ? io->present_mask : 0u; }

uint8_t cdi_module_io_request_mask(const cdi_module_io_t *io)
{ return io != NULL && io->request_healthy ? io->request_mask : 0u; }

bool cdi_module_io_healthy(const cdi_module_io_t *io)
{ return io != NULL && io->healthy; }

bool cdi_module_io_requests_healthy(const cdi_module_io_t *io)
{ return io != NULL && io->request_healthy; }

bool cdi_module_io_set_aux(cdi_module_io_t *io, bool keyless_on,
                           bool starter_on, bool exp3_on)
{
    if (io == NULL || !io->healthy) return false;
    uint8_t next = io->output_latch | EXP1_BIT | EXP2_BIT | EXP3_BIT;
    if (keyless_on) next &= (uint8_t)~EXP1_BIT;
    if (starter_on) next &= (uint8_t)~EXP2_BIT;
    if (exp3_on) next &= (uint8_t)~EXP3_BIT;
    if (next == io->output_latch) return true;
    uint8_t previous = io->output_latch;
    io->output_latch = next;
    if (write_latch(io) == ESP_OK) return true;
    io->output_latch = previous;
    return false;
}
