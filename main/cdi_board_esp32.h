#ifndef CDI_BOARD_ESP32_H
#define CDI_BOARD_ESP32_H
/*
 * Kontrak pin IgniTra CDI ESP32 R9 Modular.
 * Nama di bawah sama dengan NetLabel skematik EasyEDA; jangan mengganti GPIO
 * tanpa memperbarui skematik, README dan referensi protokol aplikasi.
 */
#include "driver/gpio.h"
#include "hal/adc_types.h"

/* --- Digital I/O: nama native skematik --- */
#define CDI_PIN_PICKUP_DIG      GPIO_NUM_4
#define CDI_PIN_GATE_C          GPIO_NUM_25
#define CDI_PIN_GATE_S          GPIO_NUM_26
#define CDI_PIN_STROBE          GPIO_NUM_27
#define CDI_PIN_FAULT_N         GPIO_NUM_14  /* active-low, pull-up RFAULT */
#define CDI_PIN_FAN_CTL         GPIO_NUM_13
#define CDI_PIN_PWM_A           GPIO_NUM_18
#define CDI_PIN_PWM_B           GPIO_NUM_19
#define CDI_PIN_OEM_CENTER      GPIO_NUM_16  /* PC817 open collector, active-low */
#define CDI_PIN_OEM_SIDE        GPIO_NUM_17  /* PC817 open collector, active-low */
#define CDI_PIN_BENCH_LOOP      GPIO_NUM_5
#define CDI_PIN_AUDIO_PWM       GPIO_NUM_23  /* cadangan AUX; selalu LOW saat ini */

/* Alias lama dipertahankan agar port ESP32 dan aplikasi lama tidak terputus. */
#define CDI_PIN_PICKUP_CENTER   CDI_PIN_PICKUP_DIG
#define CDI_PIN_OEM_TAP_CENTER  CDI_PIN_OEM_CENTER
#define CDI_PIN_OEM_TAP_SIDE    CDI_PIN_OEM_SIDE
#define CDI_PIN_GATE_CENTER     CDI_PIN_GATE_C
#define CDI_PIN_GATE_SIDE       CDI_PIN_GATE_S
#define CDI_PIN_FAN_RELAY       CDI_PIN_FAN_CTL
#define CDI_PIN_FAULT_IN        CDI_PIN_FAULT_N
#define CDI_PIN_CHG_A           CDI_PIN_PWM_A
#define CDI_PIN_CHG_B           CDI_PIN_PWM_B

/* --- Analog (ADC1) --- */
#define CDI_ADC_UNIT            ADC_UNIT_1
#define CDI_ADC_CH_TPS          ADC_CHANNEL_0 /* GPIO36, TPS_ADC */
#define CDI_ADC_CH_TEMP         ADC_CHANNEL_3 /* GPIO39, TEMP_ADC */
#define CDI_ADC_CH_TPS_REF      ADC_CHANNEL_6 /* GPIO34, TPS_REF_ADC */
#define CDI_ADC_CH_HV_CENTER    ADC_CHANNEL_7 /* GPIO35, HV_C_ADC */
#define CDI_ADC_CH_HV_SIDE      ADC_CHANNEL_4 /* GPIO32, HV_S_ADC */
#define CDI_ADC_CH_VBAT         ADC_CHANNEL_5 /* GPIO33, VBAT_ADC */

enum { CDI_ADC_IDX_TPS=0, CDI_ADC_IDX_TEMP, CDI_ADC_IDX_TPS_REF,
       CDI_ADC_IDX_HVC, CDI_ADC_IDX_HVS, CDI_ADC_IDX_VBAT, CDI_ADC_IDX_COUNT };

#define CDI_CHARGER_FREQ_HZ      100000u
#define CDI_CHARGER_DEADTIME_NS  200u

void cdi_board_gpio_init(void);
void cdi_board_adc_init(void);
void cdi_board_charger_pwm_init(void);
void cdi_board_charger_set_duty_permille(uint16_t duty_permille);
bool cdi_board_read_fault(void);
void cdi_board_set_fan(bool on);
uint16_t cdi_board_adc_raw(int index);
void cdi_board_adc_sample_all(void);

#endif
