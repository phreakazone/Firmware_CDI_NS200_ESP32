#include "cdi_engine_esp32.h"
#include "cdi_board_esp32.h"
#include "cdi_timebase.h"
#include "cdi_r5.h"
#include "cdi_r5_charger.h"
#include "cdi_r5_protocol.h"
#include "cdi_r5_ble.h"
#include "cdi_r8_oem_learn.h"
#include "cdi_r8_ota.h"
#include "cdi_selftest.h" /* uji jitter tanpa osiloskop, lihat INSTRUKSI.md -- aman ditinggal
                             terpasang di produksi selama cdi_selftest_init() tidak dipanggil */

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
static bool s_fan_on; /* status kipas hasil cdi_r9_fan_update(), untuk histeresis lintas-tick */

#define NVS_NAMESPACE "cdi_r5"
#define NVS_KEY_STORE "store"
#define NVS_KEY_FSPROOF "fsproof"

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
    engine.side_enabled = s->side_enabled != 0;
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
        cdi_selftest_mark_center_due(due); /* uji jitter, lihat cdi_selftest.h */
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
        !protocol.firmware_update_active;
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

    if (!nvs_load_store(&store) || cdi_r5_store_validate(&store) != CDI_R5_OK) {
        cdi_r5_load_defaults(&store);
        (void)nvs_save_store(&store);
    }
    if (store.setup.stage == CDI_R7_STAGE_FIRST_START && nvs_fsproof_load()) {
        store.setup.first_start_proven = 1u;
        store.setup.stage = CDI_R7_STAGE_READY;
        store.setup.center_enabled = 1u;
        store.setup.side_enabled = store.oem_profile.valid && store.oem_profile.side_samples >= 10u;
        cdi_r5_store_seal(&store);
        (void)nvs_save_store(&store);
        (void)nvs_fsproof_clear();
    } else if (store.setup.stage != CDI_R7_STAGE_FIRST_START) {
        (void)nvs_fsproof_clear();
    }

    cdi_r5_protocol_init(&protocol, &store);
    cdi_r5_protocol_set_persist(&protocol, persist_maps, NULL);
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

    ESP_ERROR_CHECK(gpio_install_isr_service(ESP_INTR_FLAG_IRAM | ESP_INTR_FLAG_LEVEL3));
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

    cdi_board_adc_sample_all();
    uint16_t battery_mv = cdi_r5_vbat_adc_to_mv(cdi_board_adc_raw(CDI_ADC_IDX_VBAT));
    bool battery_ok = battery_mv >= 9500u && battery_mv <= 16000u;
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
        !protocol.firmware_update_active;
    engine.pro_enabled = store.setup.pro_enabled != 0u;
    protocol.output_permission = engine.output_permission;
    protocol.pro_enabled = engine.pro_enabled;

    if ((esp_timer_get_time() - s_last_pickup_us) > 500000) protocol.rpm = 0u;
    protocol.hv_center = cdi_r5_hv_adc_to_volts(cdi_board_adc_raw(CDI_ADC_IDX_HVC));
    protocol.hv_side = cdi_r5_hv_adc_to_volts(cdi_board_adc_raw(CDI_ADC_IDX_HVS));
    protocol.hv_enabled = charger.duty_permille != 0u || protocol.hv_center >= 30u || protocol.hv_side >= 30u;

    if (!engine.output_permission || fault_low || protocol.strobe_active) force_safe_full();

    /* FIX: sebelumnya baris ini cuma cek fan_mode != OFF (ON dan AUTO
     * diperlakukan sama), sehingga cdi_r9_temperature_from_adc()/
     * cdi_r9_fan_update() tidak pernah terpanggil dan histeresis+fail-safe
     * NTC mati total. adc[CDI_ADC_IDX_TEMP] -> suhu -> keputusan fan -> relay. */
    int16_t temperature_cdeg = 0;
    bool temperature_valid = cdi_r9_temperature_from_adc(&store.setup,
        cdi_board_adc_raw(CDI_ADC_IDX_TEMP), &temperature_cdeg);
    s_fan_on = cdi_r9_fan_update(&store.setup, temperature_cdeg, temperature_valid, s_fan_on);
    cdi_board_set_fan(s_fan_on);

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
        store.setup.side_enabled = store.oem_profile.valid && store.oem_profile.side_samples >= 10u;
        cdi_r5_store_seal(&store);
        if (nvs_save_store(&store)) (void)nvs_fsproof_clear();
    }

    if (++divider < 10u) return;
    divider = 0u;
    cdi_r5_charger_update(&charger,
        engine.hv_target_override ? engine.hv_target_override : protocol.working.hv_target_volts,
        cdi_board_adc_raw(CDI_ADC_IDX_HVC), cdi_board_adc_raw(CDI_ADC_IDX_HVS),
        engine.output_permission, engine.output_permission && !protocol.strobe_active, fault_low);
    cdi_board_charger_set_duty_permille(charger.duty_permille);

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