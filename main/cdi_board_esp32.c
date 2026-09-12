#include "cdi_board_esp32.h"

#include "driver/mcpwm_prelude.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "cdi_board";

static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_adc_cali;
static bool s_adc_cali_ok;
static uint16_t s_adc_raw[CDI_ADC_IDX_COUNT];

static const struct { int idx; adc_channel_t ch; } s_adc_map[CDI_ADC_IDX_COUNT] = {
    { CDI_ADC_IDX_TPS,     CDI_ADC_CH_TPS },
    { CDI_ADC_IDX_TEMP,    CDI_ADC_CH_TEMP },
    { CDI_ADC_IDX_TPS_REF, CDI_ADC_CH_TPS_REF },
    { CDI_ADC_IDX_HVC,     CDI_ADC_CH_HV_CENTER },
    { CDI_ADC_IDX_HVS,     CDI_ADC_CH_HV_SIDE },
    { CDI_ADC_IDX_VBAT,    CDI_ADC_CH_VBAT },
};

static mcpwm_timer_handle_t s_chg_timer;
static mcpwm_oper_handle_t s_chg_oper;
static mcpwm_cmpr_handle_t s_chg_cmp_a, s_chg_cmp_b;

void cdi_board_gpio_init(void)
{
    gpio_config_t in_pd = {
        .pin_bit_mask = 1ULL << CDI_PIN_PICKUP_CENTER,
        .mode = GPIO_MODE_INPUT,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE, /* diseleksi rising/falling di driver capture */
    };
    gpio_config(&in_pd);

    gpio_config_t oem_taps = {
        .pin_bit_mask = (1ULL << CDI_PIN_OEM_TAP_CENTER) | (1ULL << CDI_PIN_OEM_TAP_SIDE),
        .mode = GPIO_MODE_INPUT,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_POSEDGE,
    };
    gpio_config(&oem_taps);

    gpio_config_t fault_in = {
        .pin_bit_mask = 1ULL << CDI_PIN_FAULT_IN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE, /* active-low: idle harus HIGH */
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&fault_in);

    gpio_config_t outs = {
        .pin_bit_mask = (1ULL << CDI_PIN_GATE_CENTER) | (1ULL << CDI_PIN_GATE_SIDE) |
                        (1ULL << CDI_PIN_STROBE) | (1ULL << CDI_PIN_FAN_RELAY),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&outs);
    /* Default aman: semua gate & strobo LOW sebelum apa pun lain berjalan. */
    gpio_set_level(CDI_PIN_GATE_CENTER, 0);
    gpio_set_level(CDI_PIN_GATE_SIDE, 0);
    gpio_set_level(CDI_PIN_STROBE, 0);
    gpio_set_level(CDI_PIN_FAN_RELAY, 0);
}

void cdi_board_adc_init(void)
{
    adc_oneshot_unit_init_cfg_t unit_cfg = { .unit_id = CDI_ADC_UNIT };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &s_adc));

    adc_oneshot_chan_cfg_t chan_cfg = {
        .bitwidth = ADC_BITWIDTH_DEFAULT, /* 12-bit, sama seperti STM32 */
        .atten = ADC_ATTEN_DB_12,          /* rentang ~0-3.3V penuh */
    };
    for (int i = 0; i < CDI_ADC_IDX_COUNT; ++i) {
        ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc, s_adc_map[i].ch, &chan_cfg));
    }

 #if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t cali_cfg = {
        .unit_id = CDI_ADC_UNIT,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_cali_create_scheme_line_fitting(&cali_cfg, &s_adc_cali) == ESP_OK) {
        s_adc_cali_ok = true;
    } else {
        ESP_LOGW(TAG, "ADC line-fitting calibration unavailable, memakai raw counts. "
                      "Kalibrasi manual pembagi tegangan HV/VBAT wajib dilakukan.");
    }
#else
    ESP_LOGW(TAG, "Kalibrasi ADC tidak didukung pada arsitektur ini.");
#endif
}

void cdi_board_adc_sample_all(void)
{
    for (int i = 0; i < CDI_ADC_IDX_COUNT; ++i) {
        int raw = 0;
        if (adc_oneshot_read(s_adc, s_adc_map[i].ch, &raw) == ESP_OK) {
            s_adc_raw[s_adc_map[i].idx] = (uint16_t)raw;
        }
    }
}

uint16_t cdi_board_adc_raw(int index)
{
    if (index < 0 || index >= CDI_ADC_IDX_COUNT) return 0;
    return s_adc_raw[index];
}

bool cdi_board_read_fault(void)
{
    return gpio_get_level(CDI_PIN_FAULT_IN) == 0; /* active-low */
}

void cdi_board_set_fan(bool on)
{
    gpio_set_level(CDI_PIN_FAN_RELAY, on ? 1 : 0);
}

void cdi_board_charger_pwm_init(void)
{
    mcpwm_timer_config_t timer_cfg = {
        .group_id = 0,
        .clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT,
        .resolution_hz = 40000000u, /* 40 MHz -> 400 tick/periode @100kHz, resolusi duty halus */
        .count_mode = MCPWM_TIMER_COUNT_MODE_UP,
        .period_ticks = 40000000u / CDI_CHARGER_FREQ_HZ,
    };
    ESP_ERROR_CHECK(mcpwm_new_timer(&timer_cfg, &s_chg_timer));

    mcpwm_operator_config_t oper_cfg = { .group_id = 0 };
    ESP_ERROR_CHECK(mcpwm_new_operator(&oper_cfg, &s_chg_oper));
    ESP_ERROR_CHECK(mcpwm_operator_connect_timer(s_chg_oper, s_chg_timer));

    mcpwm_comparator_config_t cmp_cfg = { .flags.update_cmp_on_tez = true };
    ESP_ERROR_CHECK(mcpwm_new_comparator(s_chg_oper, &cmp_cfg, &s_chg_cmp_a));
    ESP_ERROR_CHECK(mcpwm_new_comparator(s_chg_oper, &cmp_cfg, &s_chg_cmp_b));
    mcpwm_comparator_set_compare_value(s_chg_cmp_a, 0);
    mcpwm_comparator_set_compare_value(s_chg_cmp_b, 0);

    mcpwm_generator_config_t gen_a_cfg = { .gen_gpio_num = CDI_PIN_CHG_A };
    mcpwm_generator_config_t gen_b_cfg = { .gen_gpio_num = CDI_PIN_CHG_B };
    mcpwm_gen_handle_t gen_a, gen_b;
    ESP_ERROR_CHECK(mcpwm_new_generator(s_chg_oper, &gen_a_cfg, &gen_a));
    ESP_ERROR_CHECK(mcpwm_new_generator(s_chg_oper, &gen_b_cfg, &gen_b));

    /* A: HIGH di TEZ, LOW saat compare A tercapai (leading edge, non-inverted) */
    mcpwm_generator_set_action_on_timer_event(gen_a,
        MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, MCPWM_TIMER_EVENT_EMPTY,
                                      MCPWM_GEN_ACTION_HIGH));
    mcpwm_generator_set_action_on_compare_event(gen_a,
        MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, s_chg_cmp_a,
                                        MCPWM_GEN_ACTION_LOW));
    /* B: komplemen dari A (push-pull). Dead-time module di bawah mencegah
     * shoot-through, pengganti langsung BDTR STM32. */
    mcpwm_generator_set_action_on_timer_event(gen_b,
        MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, MCPWM_TIMER_EVENT_EMPTY,
                                      MCPWM_GEN_ACTION_LOW));
    mcpwm_generator_set_action_on_compare_event(gen_b,
        MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, s_chg_cmp_b,
                                        MCPWM_GEN_ACTION_HIGH));

    mcpwm_dead_time_config_t dt_a = {
        .posedge_delay_ticks = (uint32_t)((uint64_t)CDI_CHARGER_DEADTIME_NS * 40u / 1000u),
    };
    mcpwm_dead_time_config_t dt_b = {
        .negedge_delay_ticks = (uint32_t)((uint64_t)CDI_CHARGER_DEADTIME_NS * 40u / 1000u),
        .flags.invert_output = true,
    };
    ESP_ERROR_CHECK(mcpwm_generator_set_dead_time(gen_a, gen_a, &dt_a));
    ESP_ERROR_CHECK(mcpwm_generator_set_dead_time(gen_a, gen_b, &dt_b));

    ESP_ERROR_CHECK(mcpwm_timer_enable(s_chg_timer));
    ESP_ERROR_CHECK(mcpwm_timer_start_stop(s_chg_timer, MCPWM_TIMER_START_NO_STOP));
}

void cdi_board_charger_set_duty_permille(uint16_t duty_permille)
{
    if (duty_permille > 1000u) duty_permille = 1000u;
    uint32_t period = 40000000u / CDI_CHARGER_FREQ_HZ;
    uint32_t cmp = (period * duty_permille) / 1000u;
    /* Cap duty ~48% per sisi: push-pull perlu margin dead-time + tidak boleh
     * 100% (akan menghapus periode OFF yang dibutuhkan inti trafo reset). */
    uint32_t max_cmp = (period * 480u) / 1000u;
    if (cmp > max_cmp) cmp = max_cmp;
    mcpwm_comparator_set_compare_value(s_chg_cmp_a, duty_permille == 0 ? 0 : cmp);
    mcpwm_comparator_set_compare_value(s_chg_cmp_b, duty_permille == 0 ? 0 : cmp);
}
