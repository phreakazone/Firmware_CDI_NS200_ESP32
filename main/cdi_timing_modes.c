#include "cdi_timing_modes.h"
#include <string.h>

void cdi_timing_config_defaults(cdi_timing_config_t *config)
{
    if (config == NULL) return;
    memset(config, 0, sizeof(*config));
    config->magic = CDI_TIMING_MAGIC;
    config->version = CDI_TIMING_VERSION;
    config->mode = CDI_TIMING_STANDARD;
    config->min_rpm = 1150u;
    config->max_rpm = 1700u;
}

bool cdi_timing_config_valid(const cdi_timing_config_t *c)
{
    return c != NULL && c->magic == CDI_TIMING_MAGIC &&
           c->version == CDI_TIMING_VERSION && c->mode <= CDI_TIMING_CUSTOM &&
           c->intensity <= 10u && c->min_rpm >= 500u &&
           c->max_rpm <= 5000u && c->min_rpm < c->max_rpm;
}

void cdi_timing_evaluate(const cdi_timing_config_t *c, uint32_t rpm,
                         uint16_t tps, uint8_t *phase,
                         int16_t *trim_cdeg)
{
    /* Lumpy idle is produced by rapidly swinging timing on consecutive
     * combustion events, not by deleting sparks. Values are signed pattern
     * weights; the final trim is limited to +/-8 degrees. */
    static const int8_t kuda_pattern[8] = {2, -1, -2, 1, 2, -1, -3, 1};
    static const int8_t drumband_pattern[12] = {2, -2, 2, -2, 1, -3, 2, -2, 2, -2, 1, -3};
    static const int8_t fomo_pattern[10] = {3, -3, 2, -2, 3, -3, 1, -3, 2, -2};
    static const int8_t custom_pattern[8] = {2, -2, 1, -1, 3, -3, 1, -2};
    int16_t value = 0;
    int16_t step;
    if (trim_cdeg != NULL) *trim_cdeg = 0;
    if (!cdi_timing_config_valid(c) || phase == NULL || trim_cdeg == NULL ||
        c->mode == CDI_TIMING_STANDARD || c->intensity == 0u ||
        rpm < c->min_rpm || rpm > c->max_rpm ||
        tps > 50u) return;
    ++*phase;
    if (c->mode == CDI_TIMING_SOFT) {
        value = (int16_t)(-(int16_t)c->intensity * 20); /* max -2 deg */
    } else if (c->mode == CDI_TIMING_RESPONSIVE) {
        value = (int16_t)((int16_t)c->intensity * 20);  /* max +2 deg */
    } else if (c->mode == CDI_TIMING_KUDA) {
        step = (int16_t)(c->intensity * 20);
        value = (int16_t)(kuda_pattern[*phase & 7u] * step);
    } else if (c->mode == CDI_TIMING_DRUMBAND) {
        step = (int16_t)(c->intensity * 20);
        value = (int16_t)(drumband_pattern[*phase % 12u] * step);
    } else if (c->mode == CDI_TIMING_FOMO) {
        step = (int16_t)(c->intensity * 20);
        value = (int16_t)(fomo_pattern[*phase % 10u] * step);
    } else if (c->mode == CDI_TIMING_CUSTOM) {
        step = (int16_t)(c->intensity * 20);
        value = (int16_t)(custom_pattern[*phase & 7u] * step);
    }

    /* Guard bands prevent a show profile from pulling the engine farther
     * toward a stall or runaway. Spark is never skipped by timing presets. */
    if (rpm <= (uint32_t)c->min_rpm + 100u && value < 0) value = 0;
    if (rpm + 100u >= c->max_rpm && value > 0) value = 0;
    if (value > 800) value = 800;
    if (value < -800) value = -800;
    *trim_cdeg = value;
}
