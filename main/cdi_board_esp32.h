#ifndef CDI_BOARD_ESP32_H
#define CDI_BOARD_ESP32_H
/*
 * Peta pin ESP32 yang diselaraskan 1:1 dengan penamaan port STM32WB55.
 * 
 * [Jalur Pulser]
 * - PA0  -> Pickup Utama (GPIO4)
 * 
 * [Jalur Output DIY - Menuju Koil]
 * - PA1  -> Gate Center / Koil Tengah (GPIO25) -> Menuju J1.12
 * - PA2  -> Gate Side / Koil Samping (GPIO26)  -> Menuju J1.6
 * 
 * [Jalur Input LEARN - Dari Koil via Isolator]
 * - PB3  -> OEM Tap Center (GPIO16) <- Sadapan dari J1.12
 * - PB4  -> OEM Tap Side (GPIO17)   <- Sadapan dari J1.6
 * 
 * [Jalur Tambahan]
 * - PB5  -> Fan Relay (GPIO13)
 * - PB9  -> Strobe (GPIO27)
 */
#include "driver/gpio.h"
#include "hal/adc_types.h"

/* --- Digital I/O (Penyelarasan 1:1 Nama STM32) --- */
#define CDI_PIN_PA0_PICKUP      GPIO_NUM_4   /* input capture pickup utama */
#define CDI_PIN_PA1_GATE_CTR    GPIO_NUM_25  /* pengganti PA1 (Gate Center) */
#define CDI_PIN_PA2_GATE_SIDE   GPIO_NUM_26  /* pengganti PA2 (Gate Side) */
#define CDI_PIN_PB3_OEM_CTR     GPIO_NUM_16  /* pengganti PB3 (OEM Tap Center) */
#define CDI_PIN_PB4_OEM_SIDE    GPIO_NUM_17  /* pengganti PB4 (OEM Tap Side) */
#define CDI_PIN_PB9_STROBE      GPIO_NUM_27  /* pengganti PB9 (Strobe) */
#define CDI_PIN_PB5_FAN         GPIO_NUM_13  /* pengganti PB5 (Fan Relay) */

/* Alias kompatibilitas kode internal cdi_board_esp32.c */
#define CDI_PIN_PICKUP_CENTER   CDI_PIN_PA0_PICKUP
#define CDI_PIN_OEM_TAP_CENTER  CDI_PIN_PB3_OEM_CTR
#define CDI_PIN_OEM_TAP_SIDE    CDI_PIN_PB4_OEM_SIDE
#define CDI_PIN_GATE_CENTER     CDI_PIN_PA1_GATE_CTR
#define CDI_PIN_GATE_SIDE       CDI_PIN_PA2_GATE_SIDE
#define CDI_PIN_STROBE          CDI_PIN_PB9_STROBE
#define CDI_PIN_FAN_RELAY       CDI_PIN_PB5_FAN

#define CDI_PIN_FAULT_IN        GPIO_NUM_14  /* hardware fault active-low */
#define CDI_PIN_CHG_A           GPIO_NUM_18  /* charger push-pull, sisi A */
#define CDI_PIN_CHG_B           GPIO_NUM_19  /* charger push-pull, sisi B */

/* --- Analog (ADC1) --- */
#define CDI_ADC_UNIT            ADC_UNIT_1
#define CDI_ADC_CH_TPS          ADC_CHANNEL_0 /* GPIO36 */
#define CDI_ADC_CH_TEMP         ADC_CHANNEL_3 /* GPIO39 */
#define CDI_ADC_CH_TPS_REF      ADC_CHANNEL_6 /* GPIO34 */
#define CDI_ADC_CH_HV_CENTER    ADC_CHANNEL_7 /* GPIO35 */
#define CDI_ADC_CH_HV_SIDE      ADC_CHANNEL_4 /* GPIO32 */
#define CDI_ADC_CH_VBAT         ADC_CHANNEL_5 /* GPIO33 */

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