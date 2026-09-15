#include "cdi_r5.h"

#include <string.h>

static const uint16_t RPM_NORMAL[8] =
    {500, 1000, 1500, 2500, 4000, 6000, 8000, 10000};
static const uint16_t TPS_NORMAL[4] = {0, 333, 666, 1000};
static const uint16_t RPM_PRO[16] =
    {500, 750, 1000, 1500, 2000, 2500, 3000, 4000,
     5000, 6000, 7000, 8000, 9000, 10000, 11000, 11500};
static const uint16_t TPS_PRO[8] = {0, 100, 250, 400, 550, 700, 850, 1000};

static int16_t base_advance(uint32_t rpm, uint16_t tps, unsigned profile)
{
    int32_t a;
    if (rpm <= 500u) a = 0;
    else if (rpm <= 1000u) a = (int32_t)(rpm - 500u) * 200 / 500;
    else if (rpm <= 1500u) a = 200 + (int32_t)(rpm - 1000u) * 300 / 500;
    else if (rpm <= 2500u) a = 500 + (int32_t)(rpm - 1500u) * 500 / 1000;
    else if (rpm <= 4000u) a = 1000 + (int32_t)(rpm - 2500u) * 800 / 1500;
    else if (rpm <= 6000u) a = 1800 + (int32_t)(rpm - 4000u) * 700 / 2000;
    else if (rpm <= 8000u) a = 2500 + (int32_t)(rpm - 6000u) * 600 / 2000;
    else if (rpm <= 10000u) a = 3100 + (int32_t)(rpm - 8000u) * 500 / 2000;
    else a = 3600;
    a -= (int32_t)tps * 250 / 1000;
    if (profile == 0u) a -= 100;
    if (profile == 2u) a -= 200;
    if (profile == 3u) a += 50;
    if (a < 0) a = 0;
    if (a > 3600) a = 3600;
    return (int16_t)a;
}

static void init_map(cdi_r5_map_t *map, const char *name,
                     cdi_r5_mode_t mode, unsigned profile)
{
    size_t r, t;
    memset(map, 0, sizeof(*map));
    strncpy(map->name, name, CDI_R5_NAME_LEN - 1u);
    map->mode = (uint8_t)mode;
    map->limiter_type = (uint8_t)CDI_R5_LIMITER_SOFT;
    map->rpm_limit = mode == CDI_R5_MODE_PRO ? 11000u : 9500u;
    map->soft_band_rpm = 400u;
    map->hv_target_volts = mode == CDI_R5_MODE_PRO ? 345u : 285u;
    map->generation = 1u;
    map->rpm_count = mode == CDI_R5_MODE_PRO ? 16u : 8u;
    map->tps_count = mode == CDI_R5_MODE_PRO ? 8u : 4u;
    memcpy(map->rpm_axis, mode == CDI_R5_MODE_PRO ? RPM_PRO : RPM_NORMAL,
           map->rpm_count * sizeof(map->rpm_axis[0]));
    memcpy(map->tps_axis, mode == CDI_R5_MODE_PRO ? TPS_PRO : TPS_NORMAL,
           map->tps_count * sizeof(map->tps_axis[0]));
    for (t = 0u; t < map->tps_count; ++t)
        for (r = 0u; r < map->rpm_count; ++r)
            map->advance_cdeg[t][r] = base_advance(
                map->rpm_axis[r], map->tps_axis[t], profile);
}

uint32_t cdi_r5_crc32(const void *data, size_t length)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFu;
    size_t i;
    for (i = 0u; i < length; ++i) {
        unsigned bit;
        crc ^= p[i];
        for (bit = 0u; bit < 8u; ++bit)
            crc = (crc >> 1u) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

void cdi_r5_store_seal(cdi_r5_store_image_t *image)
{
    if (image != NULL)
        image->crc32 = cdi_r5_crc32(image, offsetof(cdi_r5_store_image_t, crc32));
}

void cdi_r5_load_defaults(cdi_r5_store_image_t *image)
{
    if (image == NULL) return;
    memset(image, 0, sizeof(*image));
    image->magic = CDI_R5_STORE_MAGIC;
    image->version = CDI_R5_STORE_VERSION;
    image->active_slot = 1u;
    init_map(&image->slots[0], "ECO", CDI_R5_MODE_NORMAL, 0u);
    init_map(&image->slots[1], "STREET", CDI_R5_MODE_NORMAL, 1u);
    init_map(&image->slots[2], "RAIN", CDI_R5_MODE_NORMAL, 2u);
    init_map(&image->slots[3], "PRO", CDI_R5_MODE_PRO, 3u);
    image->setup = (cdi_r7_setup_t){
        .magic=CDI_R7_SETUP_MAGIC, .version=CDI_R7_SETUP_VERSION,
        .stage=CDI_R7_STAGE_NEW, .pickup_edge=CDI_R7_EDGE_FALLING,
        .trigger_angle_cdeg=6000u, .side_offset_cdeg=0,
        .pulses_per_revolution=1u, .gate_pulse_us=80u,
        .first_start_hv_volts=220u, .first_start_rpm_limit=3000u,
        .first_start_advance_cap_cdeg=1000u, .fan_mode=CDI_R7_FAN_ON,
        .operating_mode=CDI_R8_OP_MANUAL_SETUP, .pro_enabled=1u,
        .diy_oem_unplug_confirmed=0u, .first_start_proven=0u,
        .profile_name="UNIVERSAL", .profile_rpm_min=300u,
        .profile_rpm_max=CDI_R5_ABSOLUTE_RPM_CAP,
        .profile_advance_min_cdeg=CDI_R9_ADVANCE_MIN_CDEG,
        .profile_advance_max_cdeg=CDI_R9_ADVANCE_MAX_CDEG,
        .fan_on_cdeg=9500u, .fan_off_cdeg=9000u
    };
    image->oem_profile.magic = 0x384D454Fu; /* "OEM8" */
    image->oem_profile.version = 1u;
    cdi_r5_store_seal(image);
}

cdi_r5_status_t cdi_r7_setup_validate(const cdi_r7_setup_t *s)
{
    bool temp_empty, temp_up, temp_down;
    if (s == NULL) return CDI_R5_ERR_ARGUMENT;
    temp_empty = s->temp_adc[0] == 0u && s->temp_adc[1] == 0u &&
                 s->temp_adc[2] == 0u;
    temp_up = s->temp_adc[0] < s->temp_adc[1] &&
              s->temp_adc[1] < s->temp_adc[2];
    temp_down = s->temp_adc[0] > s->temp_adc[1] &&
                s->temp_adc[1] > s->temp_adc[2];
    if (s->magic != CDI_R7_SETUP_MAGIC || s->version != CDI_R7_SETUP_VERSION ||
        s->stage > CDI_R7_STAGE_READY || s->pickup_edge > CDI_R7_EDGE_RISING ||
        s->trigger_angle_cdeg >= 36000u || s->side_offset_cdeg < -3000 ||
        s->side_offset_cdeg > 3000 || s->pulses_per_revolution < 1u ||
        s->pulses_per_revolution > CDI_R9_MAX_PPR || s->gate_pulse_us < 40u ||
        s->gate_pulse_us > 150u || s->first_start_hv_volts < 180u ||
        s->first_start_hv_volts > 250u || s->first_start_rpm_limit != 3000u ||
        s->first_start_advance_cap_cdeg > 1000u || s->center_enabled > 1u ||
        s->side_enabled > 1u || s->fan_mode > CDI_R7_FAN_AUTO ||
        s->operating_mode > CDI_R8_OP_DIY || s->pro_enabled > 1u ||
        s->diy_oem_unplug_confirmed > 1u || s->first_start_proven > 1u ||
        s->profile_name[CDI_R5_NAME_LEN - 1u] != '\0' ||
        s->profile_rpm_min < 100u ||
        s->profile_rpm_min >= s->profile_rpm_max ||
        s->profile_rpm_max > CDI_R5_ABSOLUTE_RPM_CAP ||
        s->profile_advance_min_cdeg < CDI_R9_ADVANCE_MIN_CDEG ||
        s->profile_advance_max_cdeg > CDI_R9_ADVANCE_MAX_CDEG ||
        s->profile_advance_min_cdeg >= s->profile_advance_max_cdeg ||
        s->fan_off_cdeg >= s->fan_on_cdeg || s->fan_on_cdeg > 20000u ||
        (!temp_empty && !temp_up && !temp_down))
        return CDI_R5_ERR_MAP;
    if (!temp_empty) {
        unsigned i;
        for (i = 0u; i < 3u; ++i)
            if (s->temp_cdeg[i] < -4000 || s->temp_cdeg[i] > 20000)
                return CDI_R5_ERR_MAP;
    }
    if (s->tps_open_adc != 0u && s->tps_open_adc <= s->tps_closed_adc + 50u)
        return CDI_R5_ERR_MAP;
    if (s->stage < CDI_R7_STAGE_TDC_SAVED && (s->center_enabled || s->side_enabled))
        return CDI_R5_ERR_MAP;
    if (s->side_enabled && !s->center_enabled) return CDI_R5_ERR_MAP;
    if (s->stage == CDI_R7_STAGE_READY && !s->center_enabled) return CDI_R5_ERR_MAP;
    return CDI_R5_OK;
}

static bool axis_valid(const uint16_t *axis, uint8_t count, uint16_t max)
{
    uint8_t i;
    if (count < 2u || axis[0] > max) return false;
    for (i = 1u; i < count; ++i)
        if (axis[i] <= axis[i - 1u] || axis[i] > max) return false;
    return true;
}

cdi_r5_status_t cdi_r5_map_validate(const cdi_r5_map_t *map, bool pro_unlocked)
{
    uint8_t r, t;
    (void)pro_unlocked; /* R9 has no artificial PRO firmware lock. */
    if (map == NULL) return CDI_R5_ERR_ARGUMENT;
    if (map->mode > CDI_R5_MODE_PRO ||
        map->limiter_type > CDI_R5_LIMITER_HARD ||
        map->rpm_count < 2u || map->rpm_count > CDI_R5_RPM_POINTS ||
        map->tps_count < 2u || map->tps_count > CDI_R5_TPS_POINTS ||
        map->rpm_limit < 500u || map->rpm_limit > CDI_R5_ABSOLUTE_RPM_CAP ||
        map->soft_band_rpm > 3000u ||
        map->hv_target_volts < 180u || map->hv_target_volts > 400u ||
        !axis_valid(map->rpm_axis, map->rpm_count, CDI_R5_ABSOLUTE_RPM_CAP) ||
        !axis_valid(map->tps_axis, map->tps_count, 1000u))
        return CDI_R5_ERR_MAP;
    for (t = 0u; t < map->tps_count; ++t)
        for (r = 0u; r < map->rpm_count; ++r)
            if (map->advance_cdeg[t][r] < CDI_R9_ADVANCE_MIN_CDEG ||
                map->advance_cdeg[t][r] > CDI_R9_ADVANCE_MAX_CDEG)
                return CDI_R5_ERR_MAP;
    return CDI_R5_OK;
}

cdi_r5_status_t cdi_r5_store_validate(const cdi_r5_store_image_t *image)
{
    uint8_t i;
    if (image == NULL) return CDI_R5_ERR_ARGUMENT;
    if (image->magic != CDI_R5_STORE_MAGIC ||
        image->version != CDI_R5_STORE_VERSION ||
        image->active_slot >= CDI_R5_MAP_SLOTS) return CDI_R5_ERR_MAP;
    if (cdi_r5_crc32(image, offsetof(cdi_r5_store_image_t, crc32)) != image->crc32)
        return CDI_R5_ERR_CRC;
    if (cdi_r7_setup_validate(&image->setup) != CDI_R5_OK) return CDI_R5_ERR_MAP;
    if (image->oem_profile.magic != 0x384D454Fu ||
        image->oem_profile.version != 1u || image->oem_profile.valid > 1u)
        return CDI_R5_ERR_MAP;
    for (i = 0u; i < CDI_R5_MAP_SLOTS; ++i)
        if (cdi_r5_map_validate(&image->slots[i], true) != CDI_R5_OK)
            return CDI_R5_ERR_MAP;
    return CDI_R5_OK;
}

static uint8_t lower_segment(const uint16_t *axis, uint8_t count, uint32_t x)
{
    uint8_t i;
    if (x <= axis[0]) return 0u;
    for (i = 0u; i + 1u < count; ++i) if (x <= axis[i + 1u]) return i;
    return (uint8_t)(count - 2u);
}

static int32_t lerp(int32_t y0, int32_t y1,
                    uint32_t x, uint32_t x0, uint32_t x1)
{
    if (x <= x0) return y0;
    if (x >= x1) return y1;
    return y0 + (int32_t)(((int64_t)(y1 - y0) * (x - x0)) / (x1 - x0));
}

int16_t cdi_r5_map_interpolate(const cdi_r5_map_t *map,
                               uint32_t rpm, uint16_t tps_permille)
{
    uint8_t ri = lower_segment(map->rpm_axis, map->rpm_count, rpm);
    uint8_t ti = lower_segment(map->tps_axis, map->tps_count, tps_permille);
    int32_t a = lerp(map->advance_cdeg[ti][ri], map->advance_cdeg[ti][ri + 1u],
                     rpm, map->rpm_axis[ri], map->rpm_axis[ri + 1u]);
    int32_t b = lerp(map->advance_cdeg[ti + 1u][ri],
                     map->advance_cdeg[ti + 1u][ri + 1u], rpm,
                     map->rpm_axis[ri], map->rpm_axis[ri + 1u]);
    return (int16_t)lerp(a, b, tps_permille,
                         map->tps_axis[ti], map->tps_axis[ti + 1u]);
}

static cdi_r5_spark_action_t limiter_action(const cdi_r5_map_t *map,
                                            uint32_t rpm, uint8_t *phase)
{
    uint32_t soft_start = (uint32_t)map->rpm_limit - map->soft_band_rpm;
    if (rpm < soft_start) return CDI_R5_SPARK_FIRE;
    if (map->limiter_type == CDI_R5_LIMITER_HARD ||
        rpm >= (uint32_t)map->rpm_limit + 300u) return CDI_R5_SPARK_HARD_CUT;
    ++*phase;
    if (rpm < map->rpm_limit)
        return ((*phase & 3u) == 0u) ? CDI_R5_SPARK_SOFT_CUT : CDI_R5_SPARK_FIRE;
    return ((*phase & 1u) == 0u) ? CDI_R5_SPARK_SOFT_CUT : CDI_R5_SPARK_FIRE;
}

cdi_r5_status_t cdi_r5_make_decision(const cdi_r5_engine_config_t *engine,
                                     const cdi_r5_map_t *map,
                                     uint32_t period_ticks,
                                     uint16_t tps_permille,
                                     uint8_t *soft_phase,
                                     cdi_r5_decision_t *decision)
{
    uint64_t rpm_num, revolution_den;
    int32_t delay_cdeg, side_cdeg;
    cdi_r5_status_t valid;
    cdi_r5_map_t effective;
    if (engine == NULL || map == NULL || soft_phase == NULL || decision == NULL ||
        engine->timer_hz == 0u || engine->pulses_per_revolution == 0u ||
        period_ticks == 0u) return CDI_R5_ERR_ARGUMENT;
    if (!engine->calibrated || !engine->output_permission)
        return CDI_R5_ERR_DISARMED;
    effective = *map;
    if (engine->rpm_limit_override != 0u) effective.rpm_limit = engine->rpm_limit_override;
    valid = cdi_r5_map_validate(&effective, engine->pro_enabled);
    if (valid != CDI_R5_OK) return valid;
    if (tps_permille > 1000u) tps_permille = 1000u;
    rpm_num = (uint64_t)engine->timer_hz * 60u;
    decision->rpm = (uint32_t)(rpm_num /
        ((uint64_t)period_ticks * engine->pulses_per_revolution));
    if (decision->rpm == 0u || decision->rpm > CDI_R5_ABSOLUTE_RPM_CAP + 1000u)
        return CDI_R5_ERR_PERIOD;
    decision->action = limiter_action(&effective, decision->rpm, soft_phase);
    {
        int32_t advance = (int32_t)cdi_r5_map_interpolate(
            &effective, decision->rpm, tps_permille) + engine->advance_trim_cdeg;
        if (engine->advance_min_cdeg < engine->advance_max_cdeg) {
            if (advance < engine->advance_min_cdeg) advance = engine->advance_min_cdeg;
            if (advance > engine->advance_max_cdeg) advance = engine->advance_max_cdeg;
        }
        if (advance < CDI_R9_ADVANCE_MIN_CDEG) advance = CDI_R9_ADVANCE_MIN_CDEG;
        if (advance > CDI_R9_ADVANCE_MAX_CDEG) advance = CDI_R9_ADVANCE_MAX_CDEG;
        /* A BTDC request cannot be earlier than the configured pickup angle. */
        if (advance > (int32_t)engine->trigger_angle_cdeg)
            advance = engine->trigger_angle_cdeg;
        decision->advance_cdeg = (int16_t)advance;
    }
    if (engine->advance_cap_cdeg &&
        decision->advance_cdeg > (int16_t)engine->advance_cap_cdeg)
        decision->advance_cdeg = (int16_t)engine->advance_cap_cdeg;
    decision->hv_target_volts = engine->hv_target_override ? engine->hv_target_override : effective.hv_target_volts;
    decision->gate_width_ticks = (uint32_t)(((uint64_t)engine->timer_hz *
        engine->gate_pulse_us) / 1000000u);
    if (decision->gate_width_ticks == 0u) decision->gate_width_ticks = 1u;
    if (decision->action != CDI_R5_SPARK_FIRE) {
        decision->center_delay_ticks = 0u;
        decision->side_delay_ticks = 0u;
        return CDI_R5_OK;
    }
    delay_cdeg = (int32_t)engine->trigger_angle_cdeg - decision->advance_cdeg;
    side_cdeg = delay_cdeg + engine->side_offset_cdeg;
    if (delay_cdeg < 0 || side_cdeg < 0 || delay_cdeg >= 36000 ||
        side_cdeg >= 36000) return CDI_R5_ERR_ADVANCE;
    revolution_den = 36000u;
    decision->center_delay_ticks = (uint32_t)(
        ((uint64_t)period_ticks * engine->pulses_per_revolution * (uint32_t)delay_cdeg) / revolution_den);
    decision->side_delay_ticks = engine->side_enabled ? (uint32_t)(
        ((uint64_t)period_ticks * engine->pulses_per_revolution * (uint32_t)side_cdeg) / revolution_den) : 0u;
    return CDI_R5_OK;
}

cdi_r5_status_t cdi_r5_live_set_cell(cdi_r5_map_t *map, uint8_t tps_index,
                                     uint8_t rpm_index, int16_t advance_cdeg,
                                     bool engine_running, bool pro_unlocked)
{
    int16_t old;
    cdi_r5_status_t valid;
    if (map == NULL || tps_index >= map->tps_count || rpm_index >= map->rpm_count)
        return CDI_R5_ERR_ARGUMENT;
    old = map->advance_cdeg[tps_index][rpm_index];
    if (engine_running && (advance_cdeg > old + 200 || advance_cdeg < old - 200))
        return CDI_R5_ERR_LIVE_STEP;
    map->advance_cdeg[tps_index][rpm_index] = advance_cdeg;
    valid = cdi_r5_map_validate(map, pro_unlocked);
    if (valid != CDI_R5_OK) {
        map->advance_cdeg[tps_index][rpm_index] = old;
        return valid;
    }
    ++map->generation;
    return CDI_R5_OK;
}

cdi_r5_status_t cdi_r5_save_slot(cdi_r5_store_image_t *image, uint8_t slot,
                                 const cdi_r5_map_t *map, uint32_t engine_rpm,
                                 bool hv_enabled, bool pro_unlocked)
{
    cdi_r5_status_t valid;
    char slot_name[CDI_R5_NAME_LEN];
    if (image == NULL || map == NULL || slot >= CDI_R5_MAP_SLOTS)
        return CDI_R5_ERR_ARGUMENT;
    if (engine_rpm != 0u) return CDI_R5_ERR_ENGINE_RUNNING;
    if (hv_enabled) return CDI_R5_ERR_HV_ACTIVE;
    valid = cdi_r5_map_validate(map, pro_unlocked);
    if (valid != CDI_R5_OK) return valid;
    memcpy(slot_name, image->slots[slot].name, sizeof(slot_name));
    image->slots[slot] = *map;
    memcpy(image->slots[slot].name, slot_name, sizeof(slot_name));
    image->slots[slot].name[CDI_R5_NAME_LEN - 1u] = '\0';
    image->active_slot = slot;
    cdi_r5_store_seal(image);
    return CDI_R5_OK;
}


static int16_t temperature_lerp(uint16_t x, uint16_t x0, int16_t y0,
                                uint16_t x1, int16_t y1)
{
    int32_t dx = (int32_t)x1 - x0;
    if (dx == 0) return y0;
    return (int16_t)((int32_t)y0 +
        ((int32_t)(y1 - y0) * ((int32_t)x - x0)) / dx);
}

bool cdi_r9_temperature_from_adc(const cdi_r7_setup_t *s, uint16_t adc,
                                 int16_t *temperature_cdeg)
{
    bool up;
    unsigned lo;
    if (s == NULL || temperature_cdeg == NULL ||
        s->temp_adc[0] == 0u || s->temp_adc[1] == 0u ||
        s->temp_adc[2] == 0u)
        return false;
    up = s->temp_adc[0] < s->temp_adc[1] &&
         s->temp_adc[1] < s->temp_adc[2];
    if (!up && !(s->temp_adc[0] > s->temp_adc[1] &&
                 s->temp_adc[1] > s->temp_adc[2]))
        return false;
    if ((up && adc <= s->temp_adc[0]) || (!up && adc >= s->temp_adc[0])) {
        *temperature_cdeg = s->temp_cdeg[0];
        return true;
    }
    if ((up && adc >= s->temp_adc[2]) || (!up && adc <= s->temp_adc[2])) {
        *temperature_cdeg = s->temp_cdeg[2];
        return true;
    }
    lo = ((up && adc <= s->temp_adc[1]) ||
          (!up && adc >= s->temp_adc[1])) ? 0u : 1u;
    *temperature_cdeg = temperature_lerp(adc, s->temp_adc[lo],
        s->temp_cdeg[lo], s->temp_adc[lo + 1u], s->temp_cdeg[lo + 1u]);
    return true;
}

bool cdi_r9_fan_update(const cdi_r7_setup_t *s, int16_t temperature_cdeg,
                       bool temperature_valid, bool previous_output)
{
    if (s == NULL || s->fan_mode == CDI_R7_FAN_OFF) return false;
    if (s->fan_mode == CDI_R7_FAN_ON) return true;
    if (!temperature_valid) return true; /* AUTO fails safe on sensor/calibration loss. */
    if (previous_output)
        return temperature_cdeg > (int16_t)s->fan_off_cdeg;
    return temperature_cdeg >= (int16_t)s->fan_on_cdeg;
}
