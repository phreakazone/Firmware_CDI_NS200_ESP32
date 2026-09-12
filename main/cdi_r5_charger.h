#ifndef CDI_R5_CHARGER_H
#define CDI_R5_CHARGER_H

#include "cdi_r5.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    CDI_R5_CHG_OFF = 0,
    CDI_R5_CHG_RAMP,
    CDI_R5_CHG_REGULATING,
    CDI_R5_CHG_FAULT
} cdi_r5_charger_state_t;

typedef struct {
    uint16_t duty_permille;
    uint16_t center_volts;
    uint16_t side_volts;
    uint16_t max_duty_permille;
    cdi_r5_charger_state_t state;
    bool fault_latched;
} cdi_r5_charger_t;

void cdi_r5_charger_init(cdi_r5_charger_t *charger);
uint16_t cdi_r5_hv_adc_to_volts(uint16_t adc12);
uint16_t cdi_r5_vbat_adc_to_mv(uint16_t adc12);
void cdi_r5_charger_update(cdi_r5_charger_t *charger,
                           uint16_t target_volts,
                           uint16_t adc_center,
                           uint16_t adc_side,
                           bool output_permission,
                           bool software_enable,
                           bool hardware_fault_low);
bool cdi_r5_charger_clear_fault(cdi_r5_charger_t *charger,
                                bool engine_stopped,
                                bool software_enable);

#endif
