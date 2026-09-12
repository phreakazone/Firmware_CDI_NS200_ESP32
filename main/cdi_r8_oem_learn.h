#ifndef CDI_R8_OEM_LEARN_H
#define CDI_R8_OEM_LEARN_H

#include "cdi_r5.h"

typedef enum {
    CDI_R8_LEARN_IDLE = 0,
    CDI_R8_LEARN_ACTIVE,
    CDI_R8_LEARN_COMPLETE,
    CDI_R8_LEARN_ERROR
} cdi_r8_learn_state_t;

typedef struct {
    cdi_r8_oem_profile_t profile;
    uint32_t timer_hz;
    uint32_t pickup_tick;
    uint32_t period_ticks;
    uint32_t center_delay_ticks;
    uint16_t current_tps;
    uint8_t current_rpm_index;
    uint8_t current_tps_index;
    uint8_t rpm_count;
    uint8_t tps_count;
    uint8_t have_period;
    uint8_t have_center;
    cdi_r8_learn_state_t state;
} cdi_r8_oem_learner_t;

void cdi_r8_oem_learn_init(cdi_r8_oem_learner_t *learner,
                           uint32_t timer_hz);
void cdi_r8_oem_learn_start(cdi_r8_oem_learner_t *learner);
void cdi_r8_oem_learn_abort(cdi_r8_oem_learner_t *learner);
void cdi_r8_oem_learn_pickup(cdi_r8_oem_learner_t *learner,
                             uint32_t tick, uint32_t period_ticks,
                             uint16_t tps_permille,
                             const cdi_r5_map_t *axis_map);
void cdi_r8_oem_learn_center_fire(cdi_r8_oem_learner_t *learner,
                                  uint32_t tick,
                                  const cdi_r7_setup_t *setup);
void cdi_r8_oem_learn_side_fire(cdi_r8_oem_learner_t *learner,
                                uint32_t tick,
                                const cdi_r7_setup_t *setup);
uint8_t cdi_r8_oem_learn_coverage(const cdi_r8_oem_learner_t *learner);
bool cdi_r8_oem_learn_finish(cdi_r8_oem_learner_t *learner,
                             cdi_r5_map_t *destination,
                             bool enable_side);

#endif
