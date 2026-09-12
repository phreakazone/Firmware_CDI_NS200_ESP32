#include "cdi_r8_oem_learn.h"

#include <string.h>

#define OEM_PROFILE_MAGIC 0x384D454Fu
#define OEM_PROFILE_VERSION 1u
#define OEM_MIN_PERIOD_TICKS 10000u
#define OEM_MAX_PERIOD_TICKS 3000000u
#define OEM_MIN_SAMPLES_PER_CELL 2u

static uint8_t nearest_axis(const uint16_t *axis, uint8_t count, uint32_t value)
{
    uint8_t best = 0u;
    uint32_t best_delta = value > axis[0] ? value - axis[0] : axis[0] - value;
    uint8_t i;
    for (i = 1u; i < count; ++i) {
        uint32_t d = value > axis[i] ? value - axis[i] : axis[i] - value;
        if (d < best_delta) { best = i; best_delta = d; }
    }
    return best;
}

static void profile_reset(cdi_r8_oem_profile_t *p)
{
    memset(p, 0, sizeof(*p));
    p->magic = OEM_PROFILE_MAGIC;
    p->version = OEM_PROFILE_VERSION;
}

void cdi_r8_oem_learn_init(cdi_r8_oem_learner_t *l, uint32_t timer_hz)
{
    if (l == NULL) return;
    memset(l, 0, sizeof(*l));
    profile_reset(&l->profile);
    l->timer_hz = timer_hz;
}

void cdi_r8_oem_learn_start(cdi_r8_oem_learner_t *l)
{
    if (l == NULL) return;
    profile_reset(&l->profile);
    l->pickup_tick = l->period_ticks = l->center_delay_ticks = 0u;
    l->have_period = l->have_center = 0u;
    l->state = CDI_R8_LEARN_ACTIVE;
}

void cdi_r8_oem_learn_abort(cdi_r8_oem_learner_t *l)
{
    if (l == NULL) return;
    profile_reset(&l->profile);
    l->state = CDI_R8_LEARN_IDLE;
    l->have_period = l->have_center = 0u;
}

void cdi_r8_oem_learn_pickup(cdi_r8_oem_learner_t *l, uint32_t tick,
                             uint32_t period_ticks, uint16_t tps,
                             const cdi_r5_map_t *map)
{
    uint32_t rpm;
    if (l == NULL || map == NULL || l->state != CDI_R8_LEARN_ACTIVE) return;
    l->pickup_tick = tick;
    l->have_center = 0u;
    if (period_ticks < OEM_MIN_PERIOD_TICKS ||
        period_ticks > OEM_MAX_PERIOD_TICKS || l->timer_hz == 0u) {
        l->have_period = 0u;
        ++l->profile.rejected_pulses;
        return;
    }
    rpm = (uint32_t)(((uint64_t)l->timer_hz * 60u) / period_ticks);
    if (rpm < 300u || rpm > CDI_R5_ABSOLUTE_RPM_CAP + 500u) {
        l->have_period = 0u;
        ++l->profile.rejected_pulses;
        return;
    }
    l->period_ticks = period_ticks;
    l->current_tps = tps > 1000u ? 1000u : tps;
    l->rpm_count = map->rpm_count;
    l->tps_count = map->tps_count;
    l->current_rpm_index = nearest_axis(map->rpm_axis, map->rpm_count, rpm);
    l->current_tps_index = nearest_axis(map->tps_axis, map->tps_count, l->current_tps);
    l->have_period = 1u;
}

static bool delay_to_advance(const cdi_r8_oem_learner_t *l, uint32_t tick,
                             const cdi_r7_setup_t *s, int16_t *advance,
                             uint32_t *delay_ticks)
{
    uint32_t elapsed, delay_cdeg;
    int32_t a;
    if (!l->have_period || s->pulses_per_revolution == 0u) return false;
    elapsed = tick - l->pickup_tick;
    if (elapsed >= l->period_ticks) return false;
    delay_cdeg = (uint32_t)(((uint64_t)elapsed * 36000u) /
                           ((uint64_t)l->period_ticks * s->pulses_per_revolution));
    a = (int32_t)s->trigger_angle_cdeg - (int32_t)delay_cdeg;
    if (a < 0 || a > 3600) return false;
    *advance = (int16_t)a;
    if (delay_ticks != NULL) *delay_ticks = elapsed;
    return true;
}

void cdi_r8_oem_learn_center_fire(cdi_r8_oem_learner_t *l, uint32_t tick,
                                  const cdi_r7_setup_t *s)
{
    int16_t sample;
    uint32_t delay;
    uint8_t *n;
    int16_t *avg;
    if (l == NULL || s == NULL || l->state != CDI_R8_LEARN_ACTIVE ||
        !delay_to_advance(l, tick, s, &sample, &delay)) {
        if (l != NULL && l->state == CDI_R8_LEARN_ACTIVE)
            ++l->profile.rejected_pulses;
        return;
    }
    n = &l->profile.samples[l->current_tps_index][l->current_rpm_index];
    avg = &l->profile.advance_cdeg[l->current_tps_index][l->current_rpm_index];
    if (*n == 0u) *avg = sample;
    else *avg = (int16_t)(((int32_t)*avg * *n + sample) / ((int32_t)*n + 1));
    if (*n < 255u) ++*n;
    if (l->profile.accepted_pulses < 65535u) ++l->profile.accepted_pulses;
    l->center_delay_ticks = delay;
    l->have_center = 1u;
}

void cdi_r8_oem_learn_side_fire(cdi_r8_oem_learner_t *l, uint32_t tick,
                                const cdi_r7_setup_t *s)
{
    int16_t ignored;
    uint32_t side_delay;
    int32_t offset;
    uint16_t n;
    if (l == NULL || s == NULL || l->state != CDI_R8_LEARN_ACTIVE ||
        !l->have_center ||
        !delay_to_advance(l, tick, s, &ignored, &side_delay)) return;
    offset = (int32_t)(((int64_t)((int32_t)side_delay -
             (int32_t)l->center_delay_ticks) * 36000) /
             ((int64_t)l->period_ticks * s->pulses_per_revolution));
    if (offset < -3000 || offset > 3000) return;
    n = l->profile.side_samples;
    if (n == 0u) l->profile.side_offset_cdeg = (int16_t)offset;
    else l->profile.side_offset_cdeg = (int16_t)(
        ((int32_t)l->profile.side_offset_cdeg * n + offset) / (n + 1u));
    if (l->profile.side_samples < 65535u) ++l->profile.side_samples;
}

uint8_t cdi_r8_oem_learn_coverage(const cdi_r8_oem_learner_t *l)
{
    unsigned valid = 0u, t, r;
    if (l == NULL) return 0u;
    for (t = 0u; t < l->tps_count; ++t)
        for (r = 0u; r < l->rpm_count; ++r)
            if (l->profile.samples[t][r] >= OEM_MIN_SAMPLES_PER_CELL) ++valid;
    return l->rpm_count&&l->tps_count ?
        (uint8_t)((valid * 100u) / (l->rpm_count*l->tps_count)) : 0u;
}

static int16_t nearest_valid(const cdi_r8_oem_profile_t *p,
                             uint8_t ti, uint8_t ri, int16_t fallback)
{
    unsigned radius;
    for (radius = 0u; radius < CDI_R5_RPM_POINTS; ++radius) {
        unsigned t, r;
        for (t = 0u; t < CDI_R5_TPS_POINTS; ++t) {
            for (r = 0u; r < CDI_R5_RPM_POINTS; ++r) {
                unsigned d = (t > ti ? t - ti : ti - t) +
                             (r > ri ? r - ri : ri - r);
                if (d == radius && p->samples[t][r] >= OEM_MIN_SAMPLES_PER_CELL)
                    return p->advance_cdeg[t][r];
            }
        }
    }
    return fallback;
}

bool cdi_r8_oem_learn_finish(cdi_r8_oem_learner_t *l,
                             cdi_r5_map_t *map, bool enable_side)
{
    uint8_t t, r;
    if (l == NULL || map == NULL || l->state != CDI_R8_LEARN_ACTIVE ||
        l->profile.accepted_pulses < 20u) return false;
    for (t = 0u; t < map->tps_count; ++t)
        for (r = 0u; r < map->rpm_count; ++r)
            map->advance_cdeg[t][r] = nearest_valid(&l->profile, t, r,
                                                    map->advance_cdeg[t][r]);
    ++map->generation;
    l->profile.valid = 1u;
    if (!enable_side) {
        l->profile.side_samples = 0u;
        l->profile.side_offset_cdeg = 0;
    }
    l->state = CDI_R8_LEARN_COMPLETE;
    return true;
}
