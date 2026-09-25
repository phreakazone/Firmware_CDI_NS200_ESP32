#include "cdi_engine_esp32.h"
#include "cdi_board_esp32.h"
#include "cdi_module_io.h"
#include "cdi_timing_modes.h"
#include "cdi_timebase.h"
#include "cdi_r5.h"
#include "cdi_r5_charger.h"
#include "cdi_r5_protocol.h"
#include "cdi_r5_ble.h"
#include "cdi_r8_oem_learn.h"
#include "cdi_r8_ota.h"
#include "cdi_selftest.h"

/* =========================================================================
 * SAKELAR MODE UJI MEJA (BENCH TEST)
 * Ubah angka di bawah ini menjadi 0 sebelum CDI dipasang ke motor sungguhan!
 * 1 = Bypass sensor aki dan HV diaktifkan (Aman untuk di meja/USB)
 * 0 = Mode Produksi/Motor (Semua sensor perlindungan diaktifkan)
 * ========================================================================= */
#define BENCH_TEST_MODE 0

#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_mac.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "cdi_engine";

static cdi_r5_store_image_t store;
static cdi_r5_protocol_t protocol;
static cdi_r5_charger_t charger;
static cdi_r8_oem_learner_t oem_learner;
static cdi_r5_engine_config_t engine = {
    .timer_hz = 1000000u, 
    .pulses_per_revolution = 1u,
    .gate_pulse_us = 80u,
};

static cdi_r8_ota_t ota_handler;
static const esp_partition_t *update_partition = NULL;
static esp_ota_handle_t update_handle = 0;

static volatile uint64_t s_last_pickup_tick;
static volatile int64_t s_last_pickup_us;
static uint8_t s_soft_phase;
static volatile bool s_battery_ok_state;
static volatile bool s_center_high, s_side_high, s_strobe_high;
static volatile uint32_t s_center_width, s_side_width, s_strobe_width;
static uint16_t s_applied_edge = 0xffffu;
static int16_t s_last_advance_cdeg;
static uint8_t s_last_limiter_state;
static uint16_t s_telemetry_sequence;
static int64_t s_first_start_good_ms;
static bool s_first_start_proof_written;
static bool s_fan_on;
static cdi_module_io_t s_module_io;
static cdi_timing_config_t s_timing;
static uint8_t s_timing_phase;
static cdi_aux_input_config_t s_aux_input_config;
static volatile bool s_aux_keyless_on, s_aux_starter_on;
/* Default OFF until U7/P3 has been sampled. This prevents a boot-time spark
 * window when neither the mechanical contact nor keyless control is active. */
static volatile bool s_ignition_run_allowed;
static volatile bool s_mechanical_contact_on;
static volatile uint8_t s_contact_source = CDI_R9_CONTACT_OFF;
static volatile int64_t s_aux_starter_deadline_us;
static uint8_t s_last_request_mask;

static void force_safe_full(void);

#define NVS_NAMESPACE "cdi_r5"
#define NVS_KEY_STORE "store"
#define NVS_KEY_FSPROOF "fsproof"
#define NVS_KEY_TIMING "timing"
#define NVS_KEY_AUX_INPUT "auxinput"

static bool nvs_load_store(cdi_r5_store_image_t *image)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return false;
    size_t len = sizeof(*image);
    esp_err_t err = nvs_get_blob(h, NVS_KEY_STORE, image, &len);
    nvs_close(h);
    return err == ESP_OK && len == sizeof(*image);
}

static bool nvs_save_store(const cdi_r5_store_image_t *image)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t err = nvs_set_blob(h, NVS_KEY_STORE, image, sizeof(*image));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err == ESP_OK;
}

static bool nvs_fsproof_load(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return false;
    uint8_t v = 0; size_t len = sizeof(v);
    esp_err_t err = nvs_get_blob(h, NVS_KEY_FSPROOF, &v, &len);
    nvs_close(h);
    return err == ESP_OK && v == 1u;
}

static bool nvs_fsproof_write(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return false;
    uint8_t v = 1u;
    esp_err_t err = nvs_set_blob(h, NVS_KEY_FSPROOF, &v, sizeof(v));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err == ESP_OK;
}

static bool nvs_fsproof_clear(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return false;
    nvs_erase_key(h, NVS_KEY_FSPROOF);
    esp_err_t err = nvs_commit(h);
    nvs_close(h);
    return err == ESP_OK;
}

static bool persist_maps(const cdi_r5_store_image_t *image, void *context)
{
    (void)context;
    return nvs_save_store(image);
}

static bool nvs_load_timing(cdi_timing_config_t *config)
{
    nvs_handle_t h;
    size_t len = sizeof(*config);
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return false;
    esp_err_t err = nvs_get_blob(h, NVS_KEY_TIMING, config, &len);
    nvs_close(h);
    return err == ESP_OK && len == sizeof(*config) &&
           cdi_timing_config_valid(config);
}

static bool persist_timing(const cdi_timing_config_t *config, void *context)
{
    nvs_handle_t h;
    (void)context;
    if (config == NULL || !cdi_timing_config_valid(config) ||
        nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t err = nvs_set_blob(h, NVS_KEY_TIMING, config, sizeof(*config));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err == ESP_OK;
}

static bool nvs_load_aux_input(cdi_aux_input_config_t *config)
{
    nvs_handle_t h;
    size_t len = sizeof(*config);
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return false;
    esp_err_t err = nvs_get_blob(h, NVS_KEY_AUX_INPUT, config, &len);
    nvs_close(h);
    return err == ESP_OK && len == sizeof(*config) &&
           cdi_aux_input_config_valid(config);
}

static bool persist_aux_input(const cdi_aux_input_config_t *config, void *context)
{
    nvs_handle_t h;
    (void)context;
    if (!cdi_aux_input_config_valid(config) ||
        nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t err = nvs_set_blob(h, NVS_KEY_AUX_INPUT, config, sizeof(*config));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err == ESP_OK;
}

static bool aux_control(uint8_t channel, bool enable, uint16_t duration_ms,
                        void *context)
{
    (void)context;
    if (channel == CDI_R9_AUX_ALL) {
        /* CONTACT OFF: starter OFF -> spark/HV OFF -> keyless relay OFF. */
        s_aux_starter_on = false;
        s_ignition_run_allowed = false;
        force_safe_full();
        s_aux_keyless_on = false;
        s_contact_source = s_mechanical_contact_on ?
            CDI_R9_CONTACT_MECHANICAL : CDI_R9_CONTACT_OFF;
    } else if (channel == CDI_R9_AUX_KEYLESS) {
        if (enable) {
            s_ignition_run_allowed = true;
            s_aux_keyless_on = true;
            s_contact_source = s_mechanical_contact_on ?
                CDI_R9_CONTACT_MECHANICAL : CDI_R9_CONTACT_KEYLESS;
        } else {
            s_aux_starter_on = false;
            s_aux_keyless_on = false;
            s_ignition_run_allowed = s_mechanical_contact_on;
            s_contact_source = s_mechanical_contact_on ?
                CDI_R9_CONTACT_MECHANICAL : CDI_R9_CONTACT_OFF;
            if (!s_ignition_run_allowed) force_safe_full();
        }
    } else if (channel == CDI_R9_AUX_STARTER) {
        if (!enable) {
            s_aux_starter_on = false;
        } else {
            bool manual_neutral_ok =
                s_aux_input_config.vehicle_profile != CDI_AUX_PROFILE_UNIVERSAL_MANUAL ||
                (cdi_module_io_requests_healthy(&s_module_io) &&
                 (cdi_module_io_request_mask(&s_module_io) & CDI_REQ_PIN9) != 0u);
            if (!s_ignition_run_allowed || !s_aux_input_config.enabled ||
                !s_battery_ok_state || protocol.rpm >= 300u ||
                protocol.hardware_fault || !manual_neutral_ok ||
                duration_ms < 100u || duration_ms > 3000u)
                return false;
            s_aux_starter_on = true;
            s_aux_starter_deadline_us = esp_timer_get_time() +
                                        (int64_t)duration_ms * 1000;
        }
    } else {
        return false;
    }
    return cdi_module_io_set_aux(&s_module_io, s_aux_keyless_on,
                                 s_aux_starter_on, false);
}

static void set_protocol_device_identity(void)
{
    uint8_t mac[6];
    char serial[CDI_DEVICE_SERIAL_LEN];
    if (esp_efuse_mac_get_default(mac) != ESP_OK) {
        cdi_r5_protocol_set_identity(&protocol, "UNAVAILABLE");
        return;
    }
    (void)snprintf(serial, sizeof(serial), "IGT-ESP32-%02X%02X%02X%02X%02X%02X",
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    cdi_r5_protocol_set_identity(&protocol, serial);
}

static bool esp32_ota_erase(void *context) {
    update_partition = esp_ota_get_next_update_partition(NULL);
    return (update_partition != NULL && esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &update_handle) == ESP_OK);
}

static bool esp32_ota_program(uint32_t offset, const uint8_t *data, size_t length, void *context) {
    return (esp_ota_write(update_handle, data, length) == ESP_OK);
}

static bool esp32_ota_finalize(const cdi_r8_ota_manifest_t *manifest, void *context) {
    return (esp_ota_end(update_handle) == ESP_OK && esp_ota_set_boot_partition(update_partition) == ESP_OK);
}

void cdi_engine_handle_ota_data(const uint8_t *data, size_t len) {
    cdi_r8_ota_write(&ota_handler, ota_handler.received, data, len);
}

static bool s_ble_connected;
bool cdi_ble_is_connected(void) { return s_ble_connected; }
__attribute__((weak)) void cdi_ble_notify_telemetry(const uint8_t *d, uint16_t n) { (void)d; (void)n; }
__attribute__((weak)) void cdi_ble_notify_response(const uint8_t *d, uint16_t n) { (void)d; (void)n; }
void cdi_engine_set_ble_connected(bool connected) { s_ble_connected = connected; }

static void IRAM_ATTR force_safe_isr(void)
{
    gpio_set_level(CDI_PIN_GATE_CENTER, 0);
    gpio_set_level(CDI_PIN_GATE_SIDE, 0);
    cdi_timebase_disarm(CDI_TB_SLOT_CENTER);
    cdi_timebase_disarm(CDI_TB_SLOT_SIDE);
    s_center_high = s_side_high = false;
}

static void force_safe_full(void)
{
    force_safe_isr();
    cdi_board_charger_set_duty_permille(0);
}

static void IRAM_ATTR sync_setup_fields(void)
{
    const cdi_r7_setup_t *s = &store.setup;
    engine.trigger_angle_cdeg = s->trigger_angle_cdeg;
    engine.side_offset_cdeg = s->side_offset_cdeg;
    engine.pulses_per_revolution = s->pulses_per_revolution;
    engine.gate_pulse_us = s->gate_pulse_us;
    engine.calibrated = s->stage >= CDI_R7_STAGE_TDC_SAVED;
    engine.center_enabled = s->center_enabled != 0;
    engine.side_enabled = s->side_enabled != 0 &&
        (protocol.module_present_mask & CDI_R9_MODULE_SIDE) != 0u;
    engine.advance_min_cdeg = s->profile_advance_min_cdeg;
    engine.advance_max_cdeg = s->profile_advance_max_cdeg;
    engine.rpm_limit_override = 0; engine.advance_cap_cdeg = 0; engine.hv_target_override = 0;
    if (s->stage == CDI_R7_STAGE_FIRST_START) {
        engine.rpm_limit_override = s->first_start_rpm_limit;
        engine.advance_cap_cdeg = s->first_start_advance_cap_cdeg;
        engine.hv_target_override = s->first_start_hv_volts;
    }
}

static void sync_setup_edge_from_task(void)
{
    const cdi_r7_setup_t *s = &store.setup;
    if (s_applied_edge != s->pickup_edge && protocol.rpm == 0u) {
        gpio_set_intr_type(CDI_PIN_PICKUP_CENTER,
            s->pickup_edge == CDI_R7_EDGE_RISING ? GPIO_INTR_POSEDGE : GPIO_INTR_NEGEDGE);
        s_applied_edge = s->pickup_edge;
    }
}

static bool IRAM_ATTR center_slot_cb(void *ctx, uint64_t due, uint64_t *next)
{
    (void)ctx;
    if (!s_center_high) {
        cdi_selftest_mark_center_due(due); /* uji jitter */
        gpio_set_level(CDI_PIN_GATE_CENTER, 1);
        s_center_high = true;
        *next = due + s_center_width;
        return true;
    }
    gpio_set_level(CDI_PIN_GATE_CENTER, 0);
    s_center_high = false;
    return false;
}

static bool IRAM_ATTR side_slot_cb(void *ctx, uint64_t due, uint64_t *next)
{
    (void)ctx;
    if (!s_side_high) {
        gpio_set_level(CDI_PIN_GATE_SIDE, 1);
        s_side_high = true;
        *next = due + s_side_width;
        return true;
    }
    gpio_set_level(CDI_PIN_GATE_SIDE, 0);
    s_side_high = false;
    return false;
}

static bool IRAM_ATTR strobe_slot_cb(void *ctx, uint64_t due, uint64_t *next)
{
    (void)ctx;
    if (!s_strobe_high) {
        gpio_set_level(CDI_PIN_STROBE, 1);
        s_strobe_high = true;
        *next = due + s_strobe_width;
        return true;
    }
    gpio_set_level(CDI_PIN_STROBE, 0);
    s_strobe_high = false;
    return false;
}

static void IRAM_ATTR pickup_isr(void *arg)
{
    (void)arg;
    cdi_r5_decision_t d;
    uint64_t now = cdi_timebase_now();
    uint64_t period64 = s_last_pickup_tick ? now - s_last_pickup_tick : 0u;

    /* FILTER NOISE: Abaikan sinyal palsu < 2000us untuk mencegah crash/WDT timeout */
    if (s_last_pickup_tick != 0u && period64 < 2000u) return;

    uint32_t period = (uint32_t)period64;

    if (s_last_pickup_tick != 0u && period > 10000u && period < 3000000u) {
        static uint32_t prev;
        if (prev && period > prev * 65u / 100u && period < prev * 135u / 100u) {
            if (protocol.pickup_quality < 100u) ++protocol.pickup_quality;
        } else if (protocol.pickup_quality) {
            --protocol.pickup_quality;
        }
        prev = period;
    }
    s_last_pickup_tick = now;
    s_last_pickup_us = esp_timer_get_time();

    sync_setup_fields();
    engine.trigger_angle_cdeg = protocol.setup_trigger_cdeg;
    engine.output_permission = !protocol.strobe_active && engine.center_enabled &&
        store.setup.operating_mode == CDI_R8_OP_DIY &&
        store.setup.diy_oem_unplug_confirmed && s_battery_ok_state &&
        s_ignition_run_allowed && !protocol.firmware_update_active;
    engine.pro_enabled = store.setup.pro_enabled != 0u;
    protocol.output_permission = engine.output_permission;
    protocol.pro_enabled = engine.pro_enabled;

    if (store.setup.operating_mode == CDI_R8_OP_OEM_LEARN) {
        if (s_last_pickup_tick != 0u && period > 10000u && period < 3000000u) {
            protocol.rpm = (uint32_t)(((uint64_t)engine.timer_hz * 60u) /
                ((uint64_t)period * engine.pulses_per_revolution));
            cdi_r8_oem_learn_pickup(&oem_learner, (uint32_t)now, period,
                protocol.tps_permille, &protocol.working);
        }
        force_safe_isr();
        return;
    }

    if (protocol.strobe_active && s_last_pickup_tick != 0u && period > 10000u && period < 3000000u) {
        uint32_t delay = (uint32_t)(((uint64_t)period * engine.pulses_per_revolution *
            protocol.setup_trigger_cdeg) / 36000u);
        s_strobe_high = false;
        s_strobe_width = engine.timer_hz / 2500u;
        cdi_timebase_arm(CDI_TB_SLOT_STROBE, now + delay);
        if (protocol.strobe_samples < 65535u) ++protocol.strobe_samples;
        force_safe_isr();
        return;
    }

    int16_t timing_trim = 0;
    cdi_timing_evaluate(&s_timing, protocol.rpm, protocol.tps_permille,
                        &s_timing_phase, &timing_trim);
    engine.advance_trim_cdeg = (int16_t)(protocol.live_trim_cdeg + timing_trim);
    cdi_r5_status_t decision_status = cdi_r5_make_decision(&engine, &protocol.working,
        period, protocol.tps_permille, &s_soft_phase, &d);
    if (decision_status == CDI_R5_OK) {
        protocol.rpm = d.rpm;
        s_last_advance_cdeg = d.advance_cdeg;
        s_last_limiter_state = (uint8_t)d.action;
    }
    if (decision_status != CDI_R5_OK || d.action != CDI_R5_SPARK_FIRE || charger.fault_latched) {
        gpio_set_level(CDI_PIN_GATE_CENTER, 0);
        gpio_set_level(CDI_PIN_GATE_SIDE, 0);
        return;
    }
    s_center_high = s_side_high = false;
    s_center_width = s_side_width = d.gate_width_ticks;
    if (engine.center_enabled) cdi_timebase_arm(CDI_TB_SLOT_CENTER, now + d.center_delay_ticks);
    if (engine.side_enabled) cdi_timebase_arm(CDI_TB_SLOT_SIDE, now + d.side_delay_ticks);
}

static void IRAM_ATTR oem_tap_isr(void *arg)
{
    uint32_t pin = (uint32_t)(uintptr_t)arg;
    if (store.setup.operating_mode != CDI_R8_OP_OEM_LEARN ||
        oem_learner.state != CDI_R8_LEARN_ACTIVE) return;
    uint32_t now = (uint32_t)cdi_timebase_now();
    if (pin == CDI_PIN_OEM_TAP_CENTER) {
        cdi_r8_oem_learn_center_fire(&oem_learner, now, &store.setup);
    } else if (pin == CDI_PIN_OEM_TAP_SIDE) {
        cdi_r8_oem_learn_side_fire(&oem_learner, now, &store.setup);
    }
}

void cdi_engine_init(void)
{
    force_safe_full();
    gpio_set_level(CDI_PIN_STROBE, 0);

    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_err);

    if (!nvs_load_timing(&s_timing)) {
        cdi_timing_config_defaults(&s_timing);
        (void)persist_timing(&s_timing, NULL);
    }
    if (!nvs_load_aux_input(&s_aux_input_config)) {
        cdi_aux_input_config_defaults(&s_aux_input_config);
        (void)persist_aux_input(&s_aux_input_config, NULL);
    }
    if (cdi_module_io_init(&s_module_io) != ESP_OK)
        ESP_LOGE(TAG, "PCF8574 U6 tidak merespons; modul opsional fail-safe OFF");

    if (!nvs_load_store(&store) || cdi_r5_store_validate(&store) != CDI_R5_OK) {
        cdi_r5_load_defaults(&store);
        (void)nvs_save_store(&store);
    }
    if (store.setup.stage == CDI_R7_STAGE_FIRST_START && nvs_fsproof_load()) {
        store.setup.first_start_proven = 1u;
        store.setup.stage = CDI_R7_STAGE_READY;
        store.setup.center_enabled = 1u;
        store.setup.side_enabled =
            (cdi_r9_effective_module_mask(&store) & CDI_R9_MODULE_SIDE) &&
            store.oem_profile.valid && store.oem_profile.side_samples >= 10u;
        cdi_r5_store_seal(&store);
        (void)nvs_save_store(&store);
        (void)nvs_fsproof_clear();
    } else if (store.setup.stage != CDI_R7_STAGE_FIRST_START) {
        (void)nvs_fsproof_clear();
    }

    cdi_r5_protocol_init(&protocol, &store);
    set_protocol_device_identity();
    cdi_r5_protocol_set_persist(&protocol, persist_maps, NULL);
    cdi_r9_protocol_attach_timing(&protocol, &s_timing, persist_timing, NULL);
    cdi_r9_protocol_attach_aux(&protocol, aux_control, NULL);
    cdi_r9_protocol_attach_aux_inputs(&protocol, &s_aux_input_config,
                                      persist_aux_input, NULL);
    (void)cdi_module_io_poll(&s_module_io);
    protocol.module_present_mask = cdi_module_io_present_mask(&s_module_io);
    protocol.module_io_ok = cdi_module_io_healthy(&s_module_io);
    protocol.aux_request_mask = cdi_module_io_request_mask(&s_module_io);
    protocol.aux_request_io_ok = cdi_module_io_requests_healthy(&s_module_io);
    s_mechanical_contact_on = protocol.aux_request_io_ok &&
        (protocol.aux_request_mask & CDI_REQ_IGNITION) != 0u;
    s_ignition_run_allowed = s_mechanical_contact_on;
    s_contact_source = s_mechanical_contact_on ?
        CDI_R9_CONTACT_MECHANICAL : CDI_R9_CONTACT_OFF;
    /* Start from zero so a held J1.1 request present during wake-up is
     * processed on the first 20 ms service pass. */
    s_last_request_mask = 0u;
    cdi_r8_oem_learn_init(&oem_learner, engine.timer_hz);
    
    cdi_r8_protocol_attach_oem_learner(&protocol, &oem_learner);
    cdi_r8_ota_init(&ota_handler, esp32_ota_erase, esp32_ota_program, esp32_ota_finalize, NULL);
    cdi_r8_protocol_attach_ota(&protocol, &ota_handler);
    
    cdi_r5_charger_init(&charger);
    sync_setup_fields();
    s_applied_edge = 0xffffu; 

    cdi_timebase_set_slot_handler(CDI_TB_SLOT_CENTER, center_slot_cb, NULL);
    cdi_timebase_set_slot_handler(CDI_TB_SLOT_SIDE, side_slot_cb, NULL);
    cdi_timebase_set_slot_handler(CDI_TB_SLOT_STROBE, strobe_slot_cb, NULL);

    /* Filter error ISR already installed */
    esp_err_t isr_err = gpio_install_isr_service(ESP_INTR_FLAG_IRAM | ESP_INTR_FLAG_LEVEL3);
    if (isr_err != ESP_OK && isr_err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Gagal menginstal ISR service");
    }

    gpio_set_intr_type(CDI_PIN_PICKUP_CENTER, GPIO_INTR_NEGEDGE);
    ESP_ERROR_CHECK(gpio_isr_handler_add(CDI_PIN_PICKUP_CENTER, pickup_isr, NULL));
    ESP_ERROR_CHECK(gpio_isr_handler_add(CDI_PIN_OEM_TAP_CENTER, oem_tap_isr,
        (void *)(uintptr_t)CDI_PIN_OEM_TAP_CENTER));
    ESP_ERROR_CHECK(gpio_isr_handler_add(CDI_PIN_OEM_TAP_SIDE, oem_tap_isr,
        (void *)(uintptr_t)CDI_PIN_OEM_TAP_SIDE));

    ESP_LOGI(TAG, "CDI engine initialised (mode=%d, stage=%d)",
        store.setup.operating_mode, store.setup.stage);
}

void cdi_engine_tick_1ms(void)
{
    static uint8_t divider;
    static uint8_t telemetry_divider;
    static uint8_t module_divider;

    cdi_board_adc_sample_all();
    if (++module_divider >= 20u) {
        module_divider = 0u;
        (void)cdi_module_io_poll(&s_module_io);
        protocol.module_present_mask = cdi_module_io_present_mask(&s_module_io);
        protocol.module_io_ok = cdi_module_io_healthy(&s_module_io);
        protocol.aux_request_mask = cdi_module_io_request_mask(&s_module_io);
        protocol.aux_request_io_ok = cdi_module_io_requests_healthy(&s_module_io);

        bool mechanical_now = protocol.aux_request_io_ok &&
            (protocol.aux_request_mask & CDI_REQ_IGNITION) != 0u;
        bool mechanical_rising = mechanical_now && !s_mechanical_contact_on;
        bool mechanical_falling = !mechanical_now && s_mechanical_contact_on;
        s_mechanical_contact_on = mechanical_now;

        bool aux_ready = protocol.module_io_ok &&
            protocol.aux_request_io_ok && s_aux_input_config.enabled &&
            (protocol.module_present_mask & CDI_R9_MODULE_AUX) != 0u;
        if (!aux_ready) {
            s_aux_starter_on = false;
            if (s_aux_keyless_on) {
                s_aux_keyless_on = false;
                if (s_contact_source == CDI_R9_CONTACT_KEYLESS) {
                    s_ignition_run_allowed = false;
                    s_contact_source = CDI_R9_CONTACT_OFF;
                    force_safe_full();
                }
            }
        } else {
            uint8_t rising = protocol.aux_request_mask &
                             (uint8_t)~s_last_request_mask;
            if (mechanical_rising) {
                s_ignition_run_allowed = true;
                s_contact_source = CDI_R9_CONTACT_MECHANICAL;
                s_aux_keyless_on = true; /* K1 hold before physical OFF. */
            }
            if (mechanical_falling &&
                s_contact_source == CDI_R9_CONTACT_MECHANICAL)
                (void)aux_control(CDI_R9_AUX_ALL, false, 0u, NULL);
            if ((rising & CDI_REQ_KEYLESS) != 0u && !mechanical_now)
                (void)aux_control(CDI_R9_AUX_KEYLESS,
                                  !s_aux_keyless_on, 0u, NULL);
            if ((rising & CDI_REQ_START) != 0u)
                (void)aux_control(CDI_R9_AUX_STARTER, true, 1500u, NULL);
        }
        s_last_request_mask = protocol.aux_request_mask;
    }
    uint16_t battery_mv = cdi_r5_vbat_adc_to_mv(cdi_board_adc_raw(CDI_ADC_IDX_VBAT));
    bool battery_ok = battery_mv >= 9500u && battery_mv <= 16000u;
    
    #if BENCH_TEST_MODE
    battery_ok = true; /* BYPASS TEGANGAN AKI */
    #endif
    
    s_battery_ok_state = battery_ok;
    bool fault_low = cdi_board_read_fault();

    protocol.tps_raw = cdi_board_adc_raw(CDI_ADC_IDX_TPS);
    if (store.setup.tps_open_adc > store.setup.tps_closed_adc + 50u) {
        uint32_t raw = protocol.tps_raw;
        if (raw <= store.setup.tps_closed_adc) protocol.tps_permille = 0u;
        else if (raw >= store.setup.tps_open_adc) protocol.tps_permille = 1000u;
        else protocol.tps_permille = (uint16_t)(((raw - store.setup.tps_closed_adc) * 1000u) /
            (uint32_t)(store.setup.tps_open_adc - store.setup.tps_closed_adc));
    } else {
        protocol.tps_permille = 0u;
    }

    sync_setup_fields();
    sync_setup_edge_from_task();
    engine.output_permission = !protocol.strobe_active && engine.center_enabled &&
        store.setup.operating_mode == CDI_R8_OP_DIY &&
        store.setup.diy_oem_unplug_confirmed && battery_ok &&
        s_ignition_run_allowed && !protocol.firmware_update_active;
    engine.pro_enabled = store.setup.pro_enabled != 0u;
    protocol.output_permission = engine.output_permission;
    protocol.pro_enabled = engine.pro_enabled;

    if ((esp_timer_get_time() - s_last_pickup_us) > 500000) protocol.rpm = 0u;
    const bool side_present = protocol.module_io_ok &&
        (protocol.module_present_mask & CDI_R9_MODULE_SIDE) != 0u;
    const uint16_t hvs_adc = side_present ? cdi_board_adc_raw(CDI_ADC_IDX_HVS) : 0u;
    protocol.hv_center = cdi_r5_hv_adc_to_volts(cdi_board_adc_raw(CDI_ADC_IDX_HVC));
    protocol.hv_side = side_present ? cdi_r5_hv_adc_to_volts(hvs_adc) : 0u;
    
    #if BENCH_TEST_MODE
    protocol.hv_center = 0; /* BYPASS TEGANGAN HANTU HV */
    protocol.hv_side = 0;
    #endif
    
    protocol.hv_enabled = charger.duty_permille != 0u || protocol.hv_center >= 30u ||
        (side_present && protocol.hv_side >= 30u);

    if (!engine.output_permission || fault_low || protocol.strobe_active) force_safe_full();

    int16_t temperature_cdeg = 0;
    bool temperature_valid = cdi_r9_temperature_from_adc(&store.setup,
        cdi_board_adc_raw(CDI_ADC_IDX_TEMP), &temperature_cdeg);
    bool thermal_configured = protocol.module_io_ok &&
        (protocol.module_present_mask & CDI_R9_MODULE_THERMAL) != 0u &&
        (cdi_r9_effective_module_mask(&store) & CDI_R9_MODULE_THERMAL) != 0u;
    s_fan_on = thermal_configured ?
        cdi_r9_fan_update(&store.setup, temperature_cdeg,
                          temperature_valid, s_fan_on) : false;
    cdi_board_set_fan(s_fan_on);
    protocol.temp_raw = cdi_board_adc_raw(CDI_ADC_IDX_TEMP);
    protocol.tps_ref_raw = cdi_board_adc_raw(CDI_ADC_IDX_TPS_REF);
    protocol.vbat_raw = cdi_board_adc_raw(CDI_ADC_IDX_VBAT);
    protocol.temperature_cdeg = temperature_valid ? temperature_cdeg : INT16_MIN;
    protocol.temperature_valid = temperature_valid;
    protocol.fan_output = s_fan_on;
    protocol.hardware_fault = fault_low;

    if (s_aux_starter_on &&
        (esp_timer_get_time() >= s_aux_starter_deadline_us ||
         protocol.rpm >= 500u || fault_low || !s_battery_ok_state ||
         !protocol.module_io_ok || !s_aux_input_config.enabled ||
         !(protocol.module_present_mask & CDI_R9_MODULE_AUX)))
        s_aux_starter_on = false;
    (void)cdi_module_io_set_aux(&s_module_io, s_aux_keyless_on,
                                s_aux_starter_on, false);
    protocol.aux_keyless_on = s_aux_keyless_on;
    protocol.aux_starter_on = s_aux_starter_on;
    protocol.aux_mechanical_on = s_mechanical_contact_on;
    protocol.aux_ignition_allowed = s_ignition_run_allowed;
    protocol.aux_engine_running = protocol.rpm >= 500u;
    protocol.aux_contact_source = s_contact_source;

    if (store.setup.stage != CDI_R7_STAGE_FIRST_START) {
        s_first_start_good_ms = 0; protocol.first_start_seconds = 0u;
    } else if (protocol.rpm >= 500u && protocol.rpm <= 3200u && !charger.fault_latched) {
        if (s_first_start_good_ms < 60000) ++s_first_start_good_ms;
        protocol.first_start_seconds = (uint16_t)(s_first_start_good_ms / 1000);
        if (s_first_start_good_ms >= 3000 && !s_first_start_proof_written) {
            force_safe_full(); charger.duty_permille = 0u;
            s_first_start_proof_written = nvs_fsproof_write();
            if (s_first_start_proof_written) { store.setup.first_start_proven = 1u; cdi_r5_store_seal(&store); }
        }
    }
    if (store.setup.stage == CDI_R7_STAGE_FIRST_START && store.setup.first_start_proven &&
        protocol.rpm == 0u && protocol.hv_center < 30u && protocol.hv_side < 30u) {
        store.setup.stage = CDI_R7_STAGE_READY; store.setup.center_enabled = 1u;
        store.setup.side_enabled =
            (cdi_r9_effective_module_mask(&store) & CDI_R9_MODULE_SIDE) &&
            store.oem_profile.valid && store.oem_profile.side_samples >= 10u;
        cdi_r5_store_seal(&store);
        if (nvs_save_store(&store)) (void)nvs_fsproof_clear();
    }

    if (++divider < 10u) return;
    divider = 0u;
    cdi_r5_charger_update(&charger,
        engine.hv_target_override ? engine.hv_target_override : protocol.working.hv_target_volts,
        cdi_board_adc_raw(CDI_ADC_IDX_HVC), hvs_adc,
        engine.side_enabled,
        engine.output_permission, engine.output_permission && !protocol.strobe_active, fault_low);
    cdi_board_charger_set_duty_permille(charger.duty_permille);

    /* Sinkronisasi data Bluetooth disetel ke 5u (20Hz) agar tidak patah-patah */
    if (++telemetry_divider >= 5u) {
        telemetry_divider = 0u;
        cdi_r5_ble_telemetry_t t = {0};
        uint8_t packet[CDI_R5_BLE_TELEMETRY_SIZE];
        t.sequence = ++s_telemetry_sequence;
        t.rpm = protocol.rpm > 65535u ? 65535u : (uint16_t)protocol.rpm;
        t.tps_permille = protocol.tps_permille;
        t.advance_cdeg = s_last_advance_cdeg;
        t.battery_centivolts = (uint16_t)(battery_mv / 10u);
        t.hv_center_volts = protocol.hv_center;
        t.hv_side_volts = protocol.hv_side;
        t.temperature_cdeg = temperature_valid ? temperature_cdeg : INT16_MIN;
        t.active_slot = store.active_slot;
        t.limiter_state = s_last_limiter_state;
        t.flags = (engine.output_permission ? CDI_R5_TF_ARM : 0u) |
                  (engine.pro_enabled ? CDI_R5_TF_PRO_JUMPER : 0u) |
                  (protocol.hv_enabled ? CDI_R5_TF_HV_ENABLED : 0u) |
                  (engine.calibrated ? CDI_R5_TF_CALIBRATED : 0u) |
                  (cdi_ble_is_connected() ? CDI_R5_TF_BLE_LINK : 0u);
        if (store.setup.stage == CDI_R7_STAGE_READY) t.flags |= CDI_R5_TF_READY;
        if (store.setup.stage == CDI_R7_STAGE_FIRST_START) t.flags |= CDI_R5_TF_FIRST_START;
        t.fault_bits = fault_low ? CDI_R5_FAULT_HW_CLAMP : 0u;
        if (!battery_ok) t.fault_bits |= CDI_R5_FAULT_BATTERY;
        if (!engine.calibrated) t.fault_bits |= CDI_R5_FAULT_CALIBRATION;
        if (charger.fault_latched) t.fault_bits |= CDI_R5_FAULT_HV_OVERVOLT;
        t.setup_stage = store.setup.stage;
        t.output_flags = (store.setup.center_enabled ? CDI_R7_OF_CENTER : 0u) |
                          (store.setup.side_enabled ? CDI_R7_OF_SIDE : 0u) |
                          (protocol.strobe_active ? CDI_R7_OF_STROBE : 0u) |
                          (s_fan_on ? CDI_R7_OF_FAN : 0u);
        t.trigger_angle_cdeg = protocol.setup_trigger_cdeg;
        t.pickup_quality = protocol.pickup_quality > 100u ? 100u : (uint8_t)protocol.pickup_quality;
        t.first_start_seconds = protocol.first_start_seconds > 255u ? 255u : (uint8_t)protocol.first_start_seconds;
        size_t n = cdi_r5_ble_encode_telemetry(&t, packet, sizeof(packet));
        if (n != 0u && cdi_ble_is_connected()) cdi_ble_notify_telemetry(packet, (uint16_t)n);
        
        if (protocol.firmware_update_active) {
            uint8_t ota_status_buf[2] = {ota_handler.state, (uint8_t)ota_handler.error_code};
            if (cdi_ble_is_connected()) {
                cdi_ble_notify_ota_status(ota_status_buf, 2);
            }
            if (ota_handler.state == 3) {
                esp_restart();
            }
        }
    }
}

size_t cdi_engine_handle_command(const uint8_t *data, size_t length, uint8_t *reply, size_t reply_size)
{
    return cdi_r5_ble_handle_command(&protocol, data, length, reply, reply_size);
}
