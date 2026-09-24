#ifndef CDI_TIMING_MODES_H
#define CDI_TIMING_MODES_H

#include <stdbool.h>
#include <stdint.h>

#define CDI_TIMING_MAGIC 0x314D4954u /* TIM1 */
#define CDI_TIMING_VERSION 1u

typedef enum {
    CDI_TIMING_STANDARD = 0,
    CDI_TIMING_SOFT = 1,
    CDI_TIMING_RESPONSIVE = 2,
    CDI_TIMING_KUDA = 3
} cdi_timing_mode_t;

typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t mode;
    uint8_t intensity; /* 0..10 */
    uint8_t reserved;
    uint16_t min_rpm;
    uint16_t max_rpm;
} cdi_timing_config_t;

void cdi_timing_config_defaults(cdi_timing_config_t *config);
bool cdi_timing_config_valid(const cdi_timing_config_t *config);
void cdi_timing_evaluate(const cdi_timing_config_t *config, uint32_t rpm,
                         uint16_t tps_permille, uint8_t *phase,
                         int16_t *trim_cdeg, bool *soft_cut);

#endif
