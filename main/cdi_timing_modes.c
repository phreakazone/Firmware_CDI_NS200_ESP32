#include "cdi_timing_modes.h"
#include <string.h>

void cdi_timing_config_defaults(cdi_timing_config_t *config)
{
    if (config == NULL) return;
    memset(config, 0, sizeof(*config));
    config->magic = CDI_TIMING_MAGIC;
    config->version = CDI_TIMING_VERSION;
    config->mode = CDI_TIMING_STANDARD;
    config->min_rpm = 700u;
    config->max_rpm = 1800u;
}

bool cdi_timing_config_valid(const cdi_timing_config_t *c)
{
    return c != NULL && c->magic == CDI_TIMING_MAGIC &&
           c->version == CDI_TIMING_VERSION && c->mode <= CDI_TIMING_KUDA &&
           c->intensity <= 10u && c->min_rpm >= 500u &&
           c->max_rpm <= 5000u && c->min_rpm < c->max_rpm;
}

void cdi_timing_evaluate(const cdi_timing_config_t *c, uint32_t rpm,
                         uint16_t tps, uint8_t *phase,
                         int16_t *trim_cdeg, bool *soft_cut)
{
    static const int8_t kuda_pattern[8] = {0, -1, -2, -1, 0, -3, -1, -2};
    if (trim_cdeg != NULL) *trim_cdeg = 0;
    if (soft_cut != NULL) *soft_cut = false;
    if (!cdi_timing_config_valid(c) || phase == NULL || trim_cdeg == NULL ||
        soft_cut == NULL || c->mode == CDI_TIMING_STANDARD ||
        c->intensity == 0u || rpm < c->min_rpm || rpm > c->max_rpm ||
        tps > 200u) return;
    ++*phase;
    if (c->mode == CDI_TIMING_SOFT) {
        *trim_cdeg = (int16_t)(-(int16_t)c->intensity * 40); /* max -4 deg */
    } else if (c->mode == CDI_TIMING_RESPONSIVE) {
        *trim_cdeg = (int16_t)((int16_t)c->intensity * 20);  /* max +2 deg */
    } else if (c->mode == CDI_TIMING_KUDA) {
        int16_t step = (int16_t)(c->intensity * 40);
        *trim_cdeg = (int16_t)(kuda_pattern[*phase & 7u] * step);
        /* Intensity rendah hanya mengubah timing. Skip dibatasi 1/12 event. */
        *soft_cut = c->intensity >= 8u && (*phase % 12u) == 0u;
    }
}
