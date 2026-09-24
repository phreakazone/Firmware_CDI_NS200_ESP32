#include "cdi_module_io.h"
#include "cdi_board_esp32.h"
#include "cdi_r5.h"

#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include <string.h>

#define MOD_I2C_PORT I2C_NUM_0
#define DET_MASK 0x1fu
#define EXP1_BIT (1u << 5)
#define EXP2_BIT (1u << 6)
#define EXP3_BIT (1u << 7)
#define IO_TIMEOUT pdMS_TO_TICKS(5)

static esp_err_t write_latch(const cdi_module_io_t *io)
{
    return i2c_master_write_to_device(MOD_I2C_PORT, CDI_MOD_I2C_ADDRESS,
                                      &io->output_latch, 1u, IO_TIMEOUT);
}

esp_err_t cdi_module_io_init(cdi_module_io_t *io)
{
    if (io == NULL) return ESP_ERR_INVALID_ARG;
    memset(io, 0, sizeof(*io));
    /* PCF power-up HIGH. DET remain inputs; active-low EXP drivers remain OFF. */
    io->output_latch = 0xffu;
    i2c_config_t cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = CDI_PIN_MOD_I2C_SDA,
        .scl_io_num = CDI_PIN_MOD_I2C_SCL,
        .sda_pullup_en = GPIO_PULLUP_DISABLE,
        .scl_pullup_en = GPIO_PULLUP_DISABLE,
        .master.clk_speed = 100000u,
        .clk_flags = 0,
    };
    esp_err_t err = i2c_param_config(MOD_I2C_PORT, &cfg);
    if (err == ESP_OK)
        err = i2c_driver_install(MOD_I2C_PORT, cfg.mode, 0, 0, 0);
    if (err == ESP_ERR_INVALID_STATE) err = ESP_OK;
    if (err == ESP_OK) err = write_latch(io);
    io->healthy = err == ESP_OK;
    return err;
}

bool cdi_module_io_poll(cdi_module_io_t *io)
{
    uint8_t raw = 0xffu;
    if (io == NULL) return false;
    esp_err_t err = i2c_master_read_from_device(MOD_I2C_PORT,
        CDI_MOD_I2C_ADDRESS, &raw, 1u, IO_TIMEOUT);
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

uint8_t cdi_module_io_present_mask(const cdi_module_io_t *io)
{ return io != NULL && io->healthy ? io->present_mask : 0u; }

bool cdi_module_io_healthy(const cdi_module_io_t *io)
{ return io != NULL && io->healthy; }

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
    io->output_latch = previous; /* retry on the next control tick */
    return false;
}
