#include "cdi_r5_charger.h"

#include <stddef.h>

#define HV_DIVIDER_TOP_OHM 1080000u
#define HV_DIVIDER_BOTTOM_OHM 8200u
#define ADC_FULL_SCALE 4095u
#define ADC_REFERENCE_MV 3300u
#define HARD_OVERVOLT_VOLTS 370u
#define MAX_IMBALANCE_VOLTS 50u

uint16_t cdi_r5_hv_adc_to_volts(uint16_t adc12)
{
    uint64_t numerator;
    if (adc12 > ADC_FULL_SCALE) adc12 = ADC_FULL_SCALE;
    numerator = (uint64_t)adc12 * ADC_REFERENCE_MV *
                (HV_DIVIDER_TOP_OHM + HV_DIVIDER_BOTTOM_OHM);
    return (uint16_t)(numerator /
        ((uint64_t)ADC_FULL_SCALE * HV_DIVIDER_BOTTOM_OHM * 1000u));
}

uint16_t cdi_r5_vbat_adc_to_mv(uint16_t adc12)
{
    uint64_t numerator;
    if (adc12 > ADC_FULL_SCALE) adc12 = ADC_FULL_SCALE;
    numerator = (uint64_t)adc12 * ADC_REFERENCE_MV * (100000u + 22000u);
    return (uint16_t)(numerator / ((uint64_t)ADC_FULL_SCALE * 22000u));
}

void cdi_r5_charger_init(cdi_r5_charger_t *charger)
{
    if (charger == NULL) return;
    *charger = (cdi_r5_charger_t){0};
    charger->max_duty_permille = 300u;
}

void cdi_r5_charger_update(cdi_r5_charger_t *charger,
                           uint16_t target_volts,
                           uint16_t adc_center,
                           uint16_t adc_side,
                           bool output_permission,
                           bool software_enable,
                           bool hardware_fault_low)
{
    uint16_t target = target_volts;
    uint16_t low, high, difference;
    if (charger == NULL) return;
    if (target > 345u) target = 345u;
    charger->center_volts = cdi_r5_hv_adc_to_volts(adc_center);
    charger->side_volts = cdi_r5_hv_adc_to_volts(adc_side);
    low = charger->center_volts < charger->side_volts ?
          charger->center_volts : charger->side_volts;
    high = charger->center_volts > charger->side_volts ?
           charger->center_volts : charger->side_volts;
    difference = high - low;
    if (hardware_fault_low || high >= HARD_OVERVOLT_VOLTS ||
        (high > 80u && difference > MAX_IMBALANCE_VOLTS)) {
        charger->fault_latched = true;
        charger->state = CDI_R5_CHG_FAULT;
        charger->duty_permille = 0u;
        return;
    }
    if (charger->fault_latched) {
        charger->state = CDI_R5_CHG_FAULT;
        charger->duty_permille = 0u;
        return;
    }
    if (!output_permission || !software_enable) {
        charger->state = CDI_R5_CHG_OFF;
        charger->duty_permille = 0u;
        return;
    }
    if (high >= target + 4u) {
        charger->state = CDI_R5_CHG_REGULATING;
        charger->duty_permille = 0u;
    } else if (low + 10u < target) {
        charger->state = CDI_R5_CHG_RAMP;
        if (charger->duty_permille + 2u <= charger->max_duty_permille)
            charger->duty_permille += 2u;
    } else {
        charger->state = CDI_R5_CHG_REGULATING;
        if (charger->duty_permille > 0u) --charger->duty_permille;
    }
}

bool cdi_r5_charger_clear_fault(cdi_r5_charger_t *charger,
                                bool engine_stopped,
                                bool software_enable)
{
    if (charger == NULL || !engine_stopped || software_enable) return false;
    charger->fault_latched = false;
    charger->state = CDI_R5_CHG_OFF;
    charger->duty_permille = 0u;
    return true;
}
