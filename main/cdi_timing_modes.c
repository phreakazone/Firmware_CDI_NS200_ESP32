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
           c->version == CDI_TIMING_VERSION && c->mode <= CDI_TIMING_CUSTOM &&
           c->intensity <= 10u && c->min_rpm >= 500u &&
           c->max_rpm <= 5000u && c->min_rpm < c->max_rpm;
}

void cdi_timing_evaluate(const cdi_timing_config_t *c, uint32_t rpm,
                         uint16_t tps, uint8_t *phase,
                         int16_t *trim_cdeg, bool *soft_cut)
{
    static const int8_t kuda_pattern[8] = {0, -1, -2, -1, 0, -3, -1, -2};
    static const int8_t drumband_pattern[12] = {-2, 0, -4, 0, -2, 0, -5, 0, -3, 0, -4, 0};
    static const int8_t fomo_pattern[10] = {0, -2, 0, -3, -1, 0, -4, 0, -2, -1};
    static const int8_t custom_pattern[8] = {-2, -1, 0, 1, 0, -3, -1, 0};
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
    } else if (c->mode == CDI_TIMING_DRUMBAND) {
        int16_t step = (int16_t)(c->intensity * 25);
        *trim_cdeg = (int16_t)(drumband_pattern[*phase % 12u] * step);
        *soft_cut = c->intensity >= 7u && (*phase % 10u) == 0u;
    } else if (c->mode == CDI_TIMING_FOMO) {
        int16_t step = (int16_t)(c->intensity * 30);
        *trim_cdeg = (int16_t)(fomo_pattern[*phase % 10u] * step);
        *soft_cut = c->intensity >= 8u && (*phase % 10u) == 0u;
    } else if (c->mode == CDI_TIMING_CUSTOM) {
        int16_t step = (int16_t)(c->intensity * 20);
        *trim_cdeg = (int16_t)(custom_pattern[*phase & 7u] * step);
    }
}
