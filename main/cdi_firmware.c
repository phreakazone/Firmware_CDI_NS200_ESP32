#include "cdi_firmware.h"

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#define CDI_FIRST_START_LIMITER_RPM 3000u
#define CDI_FIRST_START_ADVANCE_MAX_X10 100
#define CDI_TEMP_FAILSAFE_X10 1200
#define CDI_MIN_FIRE_DELAY_US 80u

static int16_t clamp_i16(int32_t value, int16_t low, int16_t high) {
    if (value < low) return low;
    if (value > high) return high;
    return (int16_t)value;
}

static uint16_t clamp_u16(uint32_t value, uint16_t low, uint16_t high) {
    if (value < low) return low;
    if (value > high) return high;
    return (uint16_t)value;
}

static uint8_t clamp_u8(uint32_t value, uint8_t low, uint8_t high) {
    if (value < low) return low;
    if (value > high) return high;
    return (uint8_t)value;
}

static void safe_copy(char *dst, size_t dst_size, const char *src) {
    if (dst_size == 0u) return;
    strncpy(dst, src ? src : "", dst_size - 1u);
    dst[dst_size - 1u] = '\0';
}

static bool config_valid(const cdi_config_t *cfg) {
    return cfg->schema == CDI_CONFIG_SCHEMA &&
           cfg->profile.rpm_count >= 2u && cfg->profile.rpm_count <= CDI_MAX_RPM_AXIS &&
           cfg->profile.load_count >= 1u && cfg->profile.load_count <= CDI_MAX_LOAD_AXIS &&
           cfg->active_map_slot < CDI_MAX_MAP_SLOTS &&
           cfg->profile.pulser_ppr >= 1u && cfg->profile.pulser_ppr <= CDI_MAX_PULSER_PPR &&
           cfg->profile.rpm_max <= CDI_FORMAT_RPM_MAX &&
           cfg->fan_off_x10 < cfg->fan_on_x10;
}

void cdi_set_profile_defaults(cdi_profile_t *profile, const char *name) {
    static const uint16_t rpm_axis[] = {500u, 1000u, 2000u, 3000u, 4500u, 6000u, 8000u, 10000u, 13000u, 16000u, 19000u, 22000u};
    static const uint8_t load_axis[] = {0u, 20u, 40u, 60u, 80u, 100u};
    memset(profile, 0, sizeof(*profile));
    safe_copy(profile->name, sizeof(profile->name), name ? name : "UNIVERSAL");
    profile->rpm_min = 300u;
    profile->rpm_max = 22000u;
    profile->advance_min_x10 = -150;
    profile->advance_max_x10 = 600;
    profile->pulser_ppr = 1u;
    profile->trigger_angle_x10 = 350;
    profile->rpm_count = (uint8_t)(sizeof(rpm_axis) / sizeof(rpm_axis[0]));
    profile->load_count = (uint8_t)(sizeof(load_axis) / sizeof(load_axis[0]));
    memcpy(profile->rpm_axis, rpm_axis, sizeof(rpm_axis));
    memcpy(profile->load_axis, load_axis, sizeof(load_axis));
    for (uint8_t r = 0u; r < profile->rpm_count; ++r) {
        int16_t base = (int16_t)(50 + (int16_t)r * 25);
        if (base > 320) base = 320;
        for (uint8_t l = 0u; l < profile->load_count; ++l) {
            profile->advance_x10[r][l] = (int16_t)(base - (int16_t)l * 8);
        }
    }
}

static void set_default_config(cdi_config_t *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->schema = CDI_CONFIG_SCHEMA;
    cdi_set_profile_defaults(&cfg->profile, "UNIVERSAL_BASE");
    for (uint8_t slot = 0u; slot < CDI_MAX_MAP_SLOTS; ++slot) {
        cfg->map_slots[slot] = cfg->profile;
    }
    cfg->setup_complete = false;
    cfg->active_map_slot = 0u;
    cfg->normal_limiter_rpm = 22000u;
    cfg->limiter_rpm = CDI_FIRST_START_LIMITER_RPM;
    cfg->soft_band_rpm = 500u;
    cfg->limiter_type = 0u; /* 0=SOFT, 1=HARD */
    cfg->fan_mode = CDI_FAN_AUTO;
    cfg->fan_on_x10 = 900;
    cfg->fan_off_x10 = 850;
    cfg->temp_cal[0] = (cdi_temp_point_t){3500u, 200};
    cfg->temp_cal[1] = (cdi_temp_point_t){1800u, 800};
    cfg->temp_cal[2] = (cdi_temp_point_t){600u, 1200};
    cfg->firmware_stage = 0u;      /* 0=BARU */
    cfg->pickup_edge = 0u;         /* 0=FALLING */
    cfg->gate_us = 80u;            /* 80us SCR gate duration */
    cfg->tps_closed_adc = 0u;
    cfg->tps_open_adc = 4095u;
    cfg->side_offset_cdeg = 0;
    cfg->target_hv_volts = 285u;
    cfg->pro_enabled = false;
    cfg->diy_unplugged = false;
    cfg->run_mode = 0u;            /* 0=MANUAL */
    cfg->quickshift_cut_ms = 60u;
    cfg->launch_limiter_rpm = 4500u;
    cfg->launch_active = false;
    cfg->spark_channel_mask = 3u;  /* Center + Side plugs */
}

static void outputs_safe(cdi_context_t *ctx) {
    ctx->telemetry.ignition_enabled = false;
    ctx->telemetry.charger_enabled = false;
    if (ctx->hal.set_ignition) ctx->hal.set_ignition(false);
    if (ctx->hal.set_charger) ctx->hal.set_charger(false);
}

void cdi_init(cdi_context_t *ctx, const cdi_hal_t *hal) {
    memset(ctx, 0, sizeof(*ctx));
    if (hal) ctx->hal = *hal;
    bool loaded = ctx->hal.load_config && ctx->hal.load_config(&ctx->config, sizeof(ctx->config));
    if (!loaded || !config_valid(&ctx->config)) {
        set_default_config(&ctx->config);
        ctx->boot_state = CDI_BOOT_FIRST_START;
        ctx->telemetry.faults |= CDI_FAULT_CONFIG;
        if (ctx->hal.save_config) ctx->hal.save_config(&ctx->config, sizeof(ctx->config));
    } else if (ctx->config.setup_complete) {
        ctx->boot_state = CDI_BOOT_READY;
        ctx->config.limiter_rpm = ctx->config.normal_limiter_rpm;
        ctx->config.firmware_stage = 4u;
    } else {
        ctx->boot_state = CDI_BOOT_FIRST_START;
        ctx->config.limiter_rpm = CDI_FIRST_START_LIMITER_RPM;
    }
    outputs_safe(ctx);
}

void cdi_mark_setup_complete(cdi_context_t *ctx) {
    if (!ctx) return;
    ctx->config.normal_limiter_rpm = clamp_u16(ctx->config.normal_limiter_rpm, 1000u, CDI_FORMAT_RPM_MAX);
    ctx->config.setup_complete = true;
    ctx->config.firmware_stage = 4u;
    ctx->config.limiter_rpm = ctx->config.normal_limiter_rpm;
    ctx->boot_state = CDI_BOOT_READY;
    ctx->telemetry.faults &= ~CDI_FAULT_CONFIG;
    if (ctx->hal.save_config) ctx->hal.save_config(&ctx->config, sizeof(ctx->config));
}

static int16_t temperature_from_adc(const cdi_config_t *cfg, uint16_t adc, bool *valid) {
    const cdi_temp_point_t *p = cfg->temp_cal;
    if (adc == 0u || adc >= 4095u) {
        *valid = false;
        return CDI_TEMP_FAILSAFE_X10;
    }
    *valid = true;
    uint8_t a = 0u;
    uint8_t b = 1u;
    if ((adc < p[1].adc && p[0].adc > p[1].adc) || (adc > p[1].adc && p[0].adc < p[1].adc)) {
        a = 1u;
        b = 2u;
    }
    int32_t dx = (int32_t)p[b].adc - (int32_t)p[a].adc;
    if (dx == 0) {
        *valid = false;
        return CDI_TEMP_FAILSAFE_X10;
    }
    int32_t value = p[a].temperature_x10 +
        ((int32_t)adc - (int32_t)p[a].adc) *
        ((int32_t)p[b].temperature_x10 - (int32_t)p[a].temperature_x10) / dx;
    return clamp_i16(value, -400, 1800);
}

void cdi_set_inputs(cdi_context_t *ctx, uint8_t load_pct, uint16_t temp_adc, uint16_t hv_x10) {
    if (!ctx) return;
    ctx->telemetry.load_pct = clamp_u8(load_pct, 0u, 100u);
    ctx->telemetry.hv_volts_x10 = hv_x10;
    bool temp_valid = false;
    ctx->telemetry.temperature_x10 = temperature_from_adc(&ctx->config, temp_adc, &temp_valid);
    if (temp_valid) ctx->telemetry.faults &= ~CDI_FAULT_TEMP_SENSOR;
    else ctx->telemetry.faults |= CDI_FAULT_TEMP_SENSOR;
}

void cdi_tick(cdi_context_t *ctx) {
    if (!ctx) return;
    bool fan = false;
    if (ctx->config.fan_mode == CDI_FAN_ON) fan = true;
    else if (ctx->config.fan_mode == CDI_FAN_AUTO) {
        if ((ctx->telemetry.faults & CDI_FAULT_TEMP_SENSOR) != 0u) fan = true;
        else if (ctx->telemetry.temperature_x10 >= ctx->config.fan_on_x10) fan = true;
        else if (ctx->telemetry.temperature_x10 > ctx->config.fan_off_x10) fan = ctx->telemetry.fan_enabled;
    }
    ctx->telemetry.fan_enabled = fan;
    if (ctx->hal.set_fan) ctx->hal.set_fan(fan);

    bool ready = ctx->boot_state == CDI_BOOT_READY && !ctx->telemetry.ota_active;
    ctx->telemetry.charger_enabled = ready && ctx->telemetry.rpm > 0u;
    if (ctx->hal.set_charger) ctx->hal.set_charger(ctx->telemetry.charger_enabled);
}

static uint8_t lower_index_u16(const uint16_t *axis, uint8_t count, uint16_t value) {
    uint8_t i = 0u;
    while ((uint8_t)(i + 1u) < count && value > axis[i + 1u]) ++i;
    return i;
}

static uint8_t lower_index_u8(const uint8_t *axis, uint8_t count, uint8_t value) {
    uint8_t i = 0u;
    while ((uint8_t)(i + 1u) < count && value > axis[i + 1u]) ++i;
    return i;
}

static int16_t map_lookup(const cdi_profile_t *p, uint16_t rpm, uint8_t load) {
    rpm = clamp_u16(rpm, p->rpm_axis[0], p->rpm_axis[p->rpm_count - 1u]);
    load = clamp_u8(load, p->load_axis[0], p->load_axis[p->load_count - 1u]);
    uint8_t ri = lower_index_u16(p->rpm_axis, p->rpm_count, rpm);
    uint8_t li = lower_index_u8(p->load_axis, p->load_count, load);
    uint8_t rj = (uint8_t)(ri + 1u < p->rpm_count ? ri + 1u : ri);
    uint8_t lj = (uint8_t)(li + 1u < p->load_count ? li + 1u : li);
    uint32_t r0 = p->rpm_axis[ri], r1 = p->rpm_axis[rj];
    uint32_t l0 = p->load_axis[li], l1 = p->load_axis[lj];
    int32_t q00 = p->advance_x10[ri][li], q10 = p->advance_x10[rj][li];
    int32_t q01 = p->advance_x10[ri][lj], q11 = p->advance_x10[rj][lj];
    uint32_t rf = r1 == r0 ? 0u : ((uint32_t)(rpm - (uint16_t)r0) * 1024u / (r1 - r0));
    uint32_t lf = l1 == l0 ? 0u : ((uint32_t)(load - (uint8_t)l0) * 1024u / (l1 - l0));
    int32_t a = q00 + (q10 - q00) * (int32_t)rf / 1024;
    int32_t b = q01 + (q11 - q01) * (int32_t)rf / 1024;
    return (int16_t)(a + (b - a) * (int32_t)lf / 1024);
}

cdi_trigger_result_t cdi_on_reference_pulse(cdi_context_t *ctx, uint32_t now_us) {
    cdi_trigger_result_t result = {false, 0u, 0, 0};
    if (!ctx || ctx->telemetry.ota_active || ctx->boot_state == CDI_BOOT_OTA) return result;
    uint32_t period_us = now_us - ctx->last_pulse_us;
    ctx->last_pulse_us = now_us;
    if (period_us == 0u) return result;
    uint32_t rpm = 60000000u / (period_us * ctx->config.profile.pulser_ppr);
    ctx->telemetry.rpm = clamp_u16(rpm, 0u, CDI_FORMAT_RPM_MAX);
    if (ctx->telemetry.rpm < ctx->config.profile.rpm_min ||
        ctx->telemetry.rpm >= ctx->config.limiter_rpm) {
        outputs_safe(ctx);
        return result;
    }

    int16_t requested = map_lookup(&ctx->config.profile, ctx->telemetry.rpm, ctx->telemetry.load_pct);
    requested = (int16_t)(requested + ctx->live_trim_x10);
    if (ctx->boot_state != CDI_BOOT_READY && requested > CDI_FIRST_START_ADVANCE_MAX_X10) {
        requested = CDI_FIRST_START_ADVANCE_MAX_X10;
    }
    requested = clamp_i16(requested, ctx->config.profile.advance_min_x10, ctx->config.profile.advance_max_x10);
    int16_t applied = requested;
    if (applied > ctx->config.profile.trigger_angle_x10) {
        applied = ctx->config.profile.trigger_angle_x10;
        ctx->telemetry.faults |= CDI_FAULT_ADVANCE_CLIPPED;
    } else {
        ctx->telemetry.faults &= ~CDI_FAULT_ADVANCE_CLIPPED;
    }

    int32_t travel_x10 = (int32_t)ctx->config.profile.trigger_angle_x10 - applied;
    uint64_t delay = ((uint64_t)period_us * (uint32_t)travel_x10) / 3600u;
    if (delay < CDI_MIN_FIRE_DELAY_US) delay = CDI_MIN_FIRE_DELAY_US;
    if (delay >= period_us) return result;

    ctx->telemetry.advance_x10 = applied;
    ctx->telemetry.ignition_enabled = true;
    /* The port raises the gate only when the scheduled delay expires. */
    result.fire = true;
    result.delay_us = (uint32_t)delay;
    result.requested_advance_x10 = requested;
    result.applied_advance_x10 = applied;
    return result;
}

static size_t replyf(char *reply, size_t size, const char *fmt, ...) {
    if (!reply || size == 0u) return 0u;
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(reply, size, fmt, args);
    va_end(args);
    return n < 0 ? 0u : (size_t)(n < (int)size ? n : (int)size - 1);
}

static bool command_safe(const cdi_context_t *ctx) {
    return ctx->telemetry.rpm == 0u && !ctx->telemetry.charger_enabled && !ctx->telemetry.ota_active;
}

static uint8_t hex_value(char c) {
    if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
    if (c >= 'A' && c <= 'F') return (uint8_t)(c - 'A' + 10);
    if (c >= 'a' && c <= 'f') return (uint8_t)(c - 'a' + 10);
    return 255u;
}

static size_t decode_hex(const char *src, uint8_t *dst, size_t capacity) {
    size_t n = 0u;
    while (src[0] && src[1] && n < capacity) {
        uint8_t hi = hex_value(src[0]), lo = hex_value(src[1]);
        if (hi > 15u || lo > 15u) break;
        dst[n++] = (uint8_t)((hi << 4) | lo);
        src += 2;
    }
    return n;
}

size_t cdi_handle_command(cdi_context_t *ctx, const char *line, char *reply, size_t reply_size) {
    if (!ctx || !line || !reply || reply_size == 0u) return 0u;
    if (strcmp(line, "PING") == 0) {
        return replyf(reply, reply_size, "ACK,PONG_R7_2",0,0,0,0,0,0);
    }
    if (strcmp(line, "GET,CAPS") == 0) {
        return replyf(reply, reply_size, "CAPS,5,30000,-300,800,32,16,4,12,FAN|TEMP3|DYNO|PROFILE|OTA", 0,0,0,0,0,0);
    }
    if (strcmp(line, "GET,STATUS") == 0) {
        return replyf(reply, reply_size, "STATUS,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld",
            (long)ctx->telemetry.rpm,
            (long)ctx->telemetry.load_pct,
            (long)(ctx->telemetry.hv_volts_x10 / 10u),
            (long)(ctx->telemetry.hv_volts_x10 / 10u),
            (long)ctx->config.active_map_slot,
            (long)ctx->telemetry.faults,
            (long)ctx->config.firmware_stage,
            (long)(ctx->config.pro_enabled ? 1 : 0));
    }
    if (strcmp(line, "GET,META") == 0) {
        return replyf(reply, reply_size, "META,%ld,0,%ld,%ld,%ld,%ld,0,%ld,%ld",
            (long)ctx->config.active_map_slot,
            (long)ctx->config.limiter_type,
            (long)ctx->config.normal_limiter_rpm,
            (long)(ctx->config.soft_band_rpm ? ctx->config.soft_band_rpm : 500u),
            (long)(ctx->config.target_hv_volts ? ctx->config.target_hv_volts : 285u),
            (long)ctx->config.profile.rpm_count,
            (long)ctx->config.profile.load_count, 0);
    }
    if (strcmp(line, "GET,SETUP") == 0) {
        int n = snprintf(reply, reply_size, "SETUP,%u,%u,%ld,%ld,%u,%u,%u,%u,%u,%u,%u,%u,%u",
            (unsigned)ctx->config.firmware_stage,
            (unsigned)ctx->config.pickup_edge,
            (long)ctx->config.profile.trigger_angle_x10 * 10,
            (long)ctx->config.side_offset_cdeg,
            (unsigned)ctx->config.profile.pulser_ppr,
            (unsigned)(ctx->config.gate_us ? ctx->config.gate_us : 80u),
            (unsigned)ctx->config.tps_closed_adc,
            (unsigned)ctx->config.tps_open_adc,
            (unsigned)(ctx->config.target_hv_volts ? ctx->config.target_hv_volts : 220u),
            (unsigned)((ctx->config.spark_channel_mask & 1u) ? 1u : 0u),
            (unsigned)((ctx->config.spark_channel_mask & 2u) ? 1u : 0u),
            (unsigned)ctx->config.fan_mode,
            (unsigned)(ctx->telemetry.rpm > 0 ? 100u : 0u));
        return n < 0 ? 0u : (size_t)(n < (int)reply_size ? n : (int)reply_size - 1);
    }
    if (strcmp(line, "GET,MODE") == 0) {
        return replyf(reply, reply_size, "MODE,%s,%ld,%ld,%ld",
            ctx->config.run_mode == 1 ? "OEM_LEARN" : (ctx->config.run_mode == 2 ? "DIY" : "MANUAL"),
            (long)(ctx->config.diy_unplugged ? 1 : 0),
            (long)(ctx->config.pro_enabled ? 1 : 0),
            (long)(ctx->config.setup_complete ? 1 : 0), 0, 0);
    }
    if (strcmp(line, "GET,LEARN") == 0) {
        return replyf(reply, reply_size, "LEARN,%s,%ld,%ld,%ld,%ld,%ld",
            ctx->config.run_mode == 1 ? "ACTIVE" : "IDLE", 0, 0, 0, 0, (long)ctx->config.side_offset_cdeg);
    }
    if (strncmp(line, "GET,CELL,", 9) == 0) {
        unsigned tps_row = 0, rpm_idx = 0;
        if (sscanf(line + 9, "%u,%u", &tps_row, &rpm_idx) == 2) {
            if (tps_row < ctx->config.profile.load_count && rpm_idx < ctx->config.profile.rpm_count) {
                int32_t cdeg = (int32_t)ctx->config.profile.advance_x10[rpm_idx][tps_row] * 10;
                return replyf(reply, reply_size, "CELL,%ld,%ld,%ld", (long)tps_row, (long)rpm_idx, (long)cdeg, 0, 0, 0);
            }
        }
        return replyf(reply, reply_size, "ERR,CELL_ARGS", 0, 0, 0, 0, 0, 0);
    }
    if (strcmp(line, "GET,OTA") == 0)
        return replyf(reply,reply_size,"OTA,%ld,%ld,%ld,0",
            ctx->telemetry.ota_active?2:0,ctx->ota_offset,ctx->ota_size,0,0,0);
    if (strcmp(line, "GET,TELEM") == 0) {
        return replyf(reply, reply_size, "TELEM,%ld,%ld,%ld,%ld,%ld,%ld",
            ctx->telemetry.rpm, ctx->telemetry.load_pct, ctx->telemetry.advance_x10,
            ctx->telemetry.temperature_x10, ctx->telemetry.hv_volts_x10, ctx->telemetry.faults);
    }
    if (strcmp(line, "GET,TEMP") == 0) {
        return replyf(reply, reply_size, "TEMP,%ld,%ld,%ld,%ld,%ld,%ld",
            ctx->telemetry.temperature_x10, ctx->config.fan_mode, ctx->config.fan_on_x10,
            ctx->config.fan_off_x10, ctx->telemetry.fan_enabled, ctx->telemetry.faults);
    }
    if (strcmp(line, "GET,PROFILE") == 0) {
        int n = snprintf(reply, reply_size, "PROFILE,%s,%u,%u,%d,%d,%u,%d",
            ctx->config.profile.name, ctx->config.profile.rpm_min, ctx->config.profile.rpm_max,
            ctx->config.profile.advance_min_x10, ctx->config.profile.advance_max_x10,
            ctx->config.profile.pulser_ppr, ctx->config.profile.trigger_angle_x10);
        return n < 0 ? 0u : (size_t)(n < (int)reply_size ? n : (int)reply_size - 1);
    }
    if (strcmp(line, "SETUP,DONE") == 0) {
        cdi_mark_setup_complete(ctx);
        return replyf(reply, reply_size, "OK,SETUP",0,0,0,0,0,0);
    }

    char copy[196];
    safe_copy(copy, sizeof(copy), line);
    char *token = strtok(copy, ",");
    if (!token) return replyf(reply, reply_size, "ERR,EMPTY",0,0,0,0,0,0);

    if (strcmp(token, "SET") == 0) {
        char *kind = strtok(NULL, ",");
        if (kind && strcmp(kind, "FAN") == 0) {
            char *mode = strtok(NULL, ","), *on = strtok(NULL, ","), *off = strtok(NULL, ",");
            if (!mode || !on || !off) return replyf(reply, reply_size, "ERR,FAN_ARGS",0,0,0,0,0,0);
            int32_t on_x10 = strtol(on, NULL, 10), off_x10 = strtol(off, NULL, 10);
            if (on_x10 < 530 || on_x10 > 1500 || off_x10 < 500 || off_x10 > on_x10 - 30)
                return replyf(reply, reply_size, "ERR,FAN_RANGE",0,0,0,0,0,0);
            ctx->config.fan_mode = strcmp(mode, "ON") == 0 ? CDI_FAN_ON :
                                   strcmp(mode, "AUTO") == 0 ? CDI_FAN_AUTO : CDI_FAN_OFF;
            ctx->config.fan_on_x10 = (int16_t)on_x10;
            ctx->config.fan_off_x10 = (int16_t)off_x10;
            if (ctx->hal.save_config) ctx->hal.save_config(&ctx->config, sizeof(ctx->config));
            return replyf(reply, reply_size, "OK,FAN",0,0,0,0,0,0);
        }
        if (kind && strcmp(kind, "LIMIT") == 0) {
            char *value = strtok(NULL, ",");
            if (!value || !command_safe(ctx)) return replyf(reply, reply_size, "ERR,UNSAFE",0,0,0,0,0,0);
            uint16_t limit = clamp_u16(strtoul(value, NULL, 10), 1000u, CDI_FORMAT_RPM_MAX);
            ctx->config.normal_limiter_rpm = limit;
            ctx->config.limiter_rpm = ctx->boot_state == CDI_BOOT_READY ? limit : CDI_FIRST_START_LIMITER_RPM;
            if (ctx->hal.save_config) ctx->hal.save_config(&ctx->config, sizeof(ctx->config));
            return replyf(reply, reply_size, "OK,LIMIT,%ld",limit,0,0,0,0,0);
        }
        if (kind && strcmp(kind, "PROFILE") == 0) {
            if (!command_safe(ctx)) return replyf(reply, reply_size, "ERR,UNSAFE",0,0,0,0,0,0);
            char *name = strtok(NULL, ","), *rmin = strtok(NULL, ","), *rmax = strtok(NULL, ",");
            char *amin = strtok(NULL, ","), *amax = strtok(NULL, ","), *ppr = strtok(NULL, ","), *trigger = strtok(NULL, ",");
            if (!name || !rmin || !rmax || !amin || !amax || !ppr || !trigger)
                return replyf(reply, reply_size, "ERR,PROFILE_ARGS",0,0,0,0,0,0);
            safe_copy(ctx->config.profile.name, sizeof(ctx->config.profile.name), name);
            ctx->config.profile.rpm_min = clamp_u16(strtoul(rmin,NULL,10),100u,CDI_FORMAT_RPM_MAX);
            ctx->config.profile.rpm_max = clamp_u16(strtoul(rmax,NULL,10),ctx->config.profile.rpm_min,CDI_FORMAT_RPM_MAX);
            ctx->config.profile.advance_min_x10 = clamp_i16(strtol(amin,NULL,10),CDI_FORMAT_ADVANCE_MIN_X10,CDI_FORMAT_ADVANCE_MAX_X10);
            ctx->config.profile.advance_max_x10 = clamp_i16(strtol(amax,NULL,10),ctx->config.profile.advance_min_x10,CDI_FORMAT_ADVANCE_MAX_X10);
            ctx->config.profile.pulser_ppr = clamp_u8(strtoul(ppr,NULL,10),1u,CDI_MAX_PULSER_PPR);
            ctx->config.profile.trigger_angle_x10 = clamp_i16(strtol(trigger,NULL,10),0,CDI_FORMAT_ADVANCE_MAX_X10);
            ctx->config.map_slots[ctx->config.active_map_slot] = ctx->config.profile;
            if (ctx->hal.save_config) ctx->hal.save_config(&ctx->config, sizeof(ctx->config));
            return replyf(reply, reply_size, "OK,PROFILE",0,0,0,0,0,0);
        }
    }

    if (strcmp(token, "MAP") == 0) {
        char *op = strtok(NULL, ",");
        if (op && strcmp(op, "SELECT") == 0) {
            char *slot_text=strtok(NULL,",");
            uint32_t slot=slot_text?strtoul(slot_text,NULL,10):CDI_MAX_MAP_SLOTS;
            if(slot>=CDI_MAX_MAP_SLOTS||!command_safe(ctx))
                return replyf(reply,reply_size,"ERR,UNSAFE",0,0,0,0,0,0);
            ctx->config.active_map_slot=(uint8_t)slot;
            ctx->config.profile=ctx->config.map_slots[slot];
            if(ctx->hal.save_config)ctx->hal.save_config(&ctx->config,sizeof(ctx->config));
            return replyf(reply,reply_size,"ACK,LOAD%ld",slot,0,0,0,0,0);
        }
        if (op && strcmp(op, "BEGIN") == 0) {
            char *rpm_count = strtok(NULL, ","), *load_count = strtok(NULL, ",");
            if (!rpm_count || !load_count || !command_safe(ctx))
                return replyf(reply, reply_size, "ERR,UNSAFE",0,0,0,0,0,0);
            uint8_t rn = clamp_u8(strtoul(rpm_count,NULL,10),2u,CDI_MAX_RPM_AXIS);
            uint8_t ln = clamp_u8(strtoul(load_count,NULL,10),1u,CDI_MAX_LOAD_AXIS);
            ctx->map_staging = ctx->config.profile;
            ctx->map_staging.rpm_count = rn;
            ctx->map_staging.load_count = ln;
            ctx->map_staging_active = true;
            return replyf(reply,reply_size,"OK,MAP_BEGIN,%ld,%ld",rn,ln,0,0,0,0);
        }
        if (!ctx->map_staging_active)
            return replyf(reply,reply_size,"ERR,MAP_STATE",0,0,0,0,0,0);
        if (op && strcmp(op, "RPM") == 0) {
            char *index=strtok(NULL,","), *value=strtok(NULL,",");
            uint32_t i=index?strtoul(index,NULL,10):CDI_MAX_RPM_AXIS;
            if(!value||i>=ctx->map_staging.rpm_count) return replyf(reply,reply_size,"ERR,MAP_INDEX",0,0,0,0,0,0);
            ctx->map_staging.rpm_axis[i]=clamp_u16(strtoul(value,NULL,10),ctx->map_staging.rpm_min,ctx->map_staging.rpm_max);
            return replyf(reply,reply_size,"OK,MAP_RPM,%ld",i,0,0,0,0,0);
        }
        if (op && strcmp(op, "LOAD") == 0) {
            char *index=strtok(NULL,","), *value=strtok(NULL,",");
            uint32_t i=index?strtoul(index,NULL,10):CDI_MAX_LOAD_AXIS;
            if(!value||i>=ctx->map_staging.load_count) return replyf(reply,reply_size,"ERR,MAP_INDEX",0,0,0,0,0,0);
            ctx->map_staging.load_axis[i]=clamp_u8(strtoul(value,NULL,10),0u,100u);
            return replyf(reply,reply_size,"OK,MAP_LOAD,%ld",i,0,0,0,0,0);
        }
        if (op && (strcmp(op, "CELL") == 0 || strcmp(op, "SET") == 0)) {
            char *a1 = strtok(NULL, ","), *a2 = strtok(NULL, ","), *value = strtok(NULL, ",");
            uint32_t r = CDI_MAX_RPM_AXIS, l = CDI_MAX_LOAD_AXIS;
            if (!value || !a1 || !a2)
                return replyf(reply, reply_size, "ERR,MAP_INDEX", 0, 0, 0, 0, 0, 0);
            if (strcmp(op, "SET") == 0) {
                /* Android sends MAP,SET,loadIdx,rpmIdx,val */
                l = strtoul(a1, NULL, 10);
                r = strtoul(a2, NULL, 10);
            } else {
                /* Android sends MAP,CELL,rpmIdx,loadIdx,val */
                r = strtoul(a1, NULL, 10);
                l = strtoul(a2, NULL, 10);
            }
            if (r >= ctx->map_staging.rpm_count || l >= ctx->map_staging.load_count)
                return replyf(reply, reply_size, "ERR,MAP_INDEX", 0, 0, 0, 0, 0, 0);
            ctx->map_staging.advance_x10[r][l] = clamp_i16(strtol(value, NULL, 10),
                ctx->map_staging.advance_min_x10, ctx->map_staging.advance_max_x10);
            return replyf(reply, reply_size, "OK,MAP_CELL,%ld,%ld", r, l, 0, 0, 0, 0);
        }
        if (op && strcmp(op, "SAVE") == 0) {
            char *slot_text=strtok(NULL,",");
            uint32_t slot=slot_text?strtoul(slot_text,NULL,10):CDI_MAX_MAP_SLOTS;
            if(slot>=CDI_MAX_MAP_SLOTS||!command_safe(ctx))
                return replyf(reply,reply_size,"ERR,UNSAFE",0,0,0,0,0,0);
            for(uint8_t i=1u;i<ctx->map_staging.rpm_count;++i)
                if(ctx->map_staging.rpm_axis[i]<=ctx->map_staging.rpm_axis[i-1u])
                    return replyf(reply,reply_size,"ERR,RPM_AXIS",0,0,0,0,0,0);
            for(uint8_t i=1u;i<ctx->map_staging.load_count;++i)
                if(ctx->map_staging.load_axis[i]<=ctx->map_staging.load_axis[i-1u])
                    return replyf(reply,reply_size,"ERR,LOAD_AXIS",0,0,0,0,0,0);
            ctx->config.map_slots[slot]=ctx->map_staging;
            ctx->config.profile=ctx->map_staging;
            ctx->config.active_map_slot=(uint8_t)slot;
            ctx->map_staging_active=false;
            if(ctx->hal.save_config)ctx->hal.save_config(&ctx->config,sizeof(ctx->config));
            return replyf(reply,reply_size,"OK,MAP_SAVE,%ld",slot,0,0,0,0,0);
        }
        if (op && strcmp(op, "ABORT") == 0) {
            ctx->map_staging_active=false;
            return replyf(reply,reply_size,"OK,MAP_ABORT",0,0,0,0,0,0);
        }
        return replyf(reply,reply_size,"ERR,MAP_OP",0,0,0,0,0,0);
    }

    if (strcmp(token, "MODE") == 0) {
        char *mode = strtok(NULL, ",");
        if (!mode || !command_safe(ctx)) return replyf(reply, reply_size, "ERR,UNSAFE", 0, 0, 0, 0, 0, 0);
        if (strcmp(mode, "OEM_LEARN") == 0 || strcmp(mode, "LEARN") == 0) {
            ctx->config.run_mode = 1u;
            return replyf(reply, reply_size, "ACK,MODE,OEM_LEARN", 0, 0, 0, 0, 0, 0);
        }
        if (strcmp(mode, "MANUAL") == 0) {
            ctx->config.run_mode = 0u;
            return replyf(reply, reply_size, "ACK,MODE,MANUAL", 0, 0, 0, 0, 0, 0);
        }
        if (strcmp(mode, "DIY") == 0) {
            char *unplug = strtok(NULL, ",");
            ctx->config.run_mode = 2u;
            if (unplug && strcmp(unplug, "OEM_UNPLUGGED") == 0) ctx->config.diy_unplugged = true;
            return replyf(reply, reply_size, "ACK,MODE,DIY", 0, 0, 0, 0, 0, 0);
        }
        return replyf(reply, reply_size, "ACK,MODE", 0, 0, 0, 0, 0, 0);
    }

    if (strcmp(token, "LEARN") == 0) {
        char *op = strtok(NULL, ",");
        if (op && strcmp(op, "START") == 0) {
            ctx->config.run_mode = 1u;
            return replyf(reply, reply_size, "ACK,LEARN_START", 0, 0, 0, 0, 0, 0);
        }
        if (op && strcmp(op, "STOP") == 0) {
            return replyf(reply, reply_size, "ACK,LEARN_STOP", 0, 0, 0, 0, 0, 0);
        }
        return replyf(reply, reply_size, "ACK,LEARN", 0, 0, 0, 0, 0, 0);
    }

    if (strcmp(token, "FEATURE") == 0) {
        char *feat = strtok(NULL, ",");
        if (feat && strcmp(feat, "PRO") == 0) {
            char *state = strtok(NULL, ",");
            bool en = (state && strcmp(state, "ON") == 0);
            ctx->config.pro_enabled = en;
            ctx->config.target_hv_volts = en ? 345u : 285u;
            if (ctx->hal.save_config) ctx->hal.save_config(&ctx->config, sizeof(ctx->config));
            return replyf(reply, reply_size, en ? "ACK,PRO_ON" : "ACK,PRO_OFF", 0, 0, 0, 0, 0, 0);
        }
        return replyf(reply, reply_size, "ACK,FEATURE", 0, 0, 0, 0, 0, 0);
    }

    if (strcmp(token, "SETUP") == 0) {
        char *op=strtok(NULL,",");
        if(!op||!command_safe(ctx))return replyf(reply,reply_size,"ERR,UNSAFE",0,0,0,0,0,0);
        if (strcmp(op, "STROBE") == 0) {
            char *state = strtok(NULL, ",");
            bool on = (state && strcmp(state, "ON") == 0);
            if (ctx->hal.set_strobe) ctx->hal.set_strobe(on);
            return replyf(reply, reply_size, on ? "ACK,STROBE_ON" : "ACK,STROBE_OFF", 0, 0, 0, 0, 0, 0);
        }
        if(strcmp(op,"PPR")==0){
            char *value=strtok(NULL,",");
            ctx->config.profile.pulser_ppr=clamp_u8(value?strtoul(value,NULL,10):1u,1u,CDI_MAX_PULSER_PPR);
            ctx->config.map_slots[ctx->config.active_map_slot]=ctx->config.profile;
            if(ctx->hal.save_config)ctx->hal.save_config(&ctx->config,sizeof(ctx->config));
            return replyf(reply,reply_size,"ACK,PPR",0,0,0,0,0,0);
        }
        if(strcmp(op,"MANUAL_TDC")==0){
            char *value=strtok(NULL,",");
            ctx->config.profile.trigger_angle_x10=clamp_i16((value?strtol(value,NULL,10):3500)/10,0,CDI_FORMAT_ADVANCE_MAX_X10);
            ctx->config.firmware_stage = 2u;
            ctx->config.map_slots[ctx->config.active_map_slot]=ctx->config.profile;
            if(ctx->hal.save_config)ctx->hal.save_config(&ctx->config,sizeof(ctx->config));
            return replyf(reply,reply_size,"ACK,TDC_MANUAL_SAVED",0,0,0,0,0,0);
        }
        if(strcmp(op,"FIRST_START")==0){
            ctx->config.setup_complete=false;
            ctx->config.firmware_stage = 3u;
            ctx->config.limiter_rpm=CDI_FIRST_START_LIMITER_RPM;
            ctx->config.target_hv_volts = 220u;
            ctx->boot_state=CDI_BOOT_FIRST_START;
            if(ctx->hal.save_config)ctx->hal.save_config(&ctx->config,sizeof(ctx->config));
            return replyf(reply,reply_size,"ACK,FIRST_START",0,0,0,0,0,0);
        }
        if(strcmp(op,"READY")==0){
            char *kind=strtok(NULL,",");
            cdi_mark_setup_complete(ctx);
            if (kind && strcmp(kind, "THREE") == 0) {
                char *so = strtok(NULL, ",");
                if (so) ctx->config.side_offset_cdeg = (int16_t)strtol(so, NULL, 10);
                ctx->config.spark_channel_mask = 3u;
                if (ctx->hal.save_config) ctx->hal.save_config(&ctx->config, sizeof(ctx->config));
                return replyf(reply, reply_size, "ACK,READY_THREE", 0, 0, 0, 0, 0, 0);
            }
            ctx->config.spark_channel_mask = 1u;
            if (ctx->hal.save_config) ctx->hal.save_config(&ctx->config, sizeof(ctx->config));
            return replyf(reply, reply_size, "ACK,READY_CENTER", 0, 0, 0, 0, 0, 0);
        }
        if(strcmp(op,"FAN")==0){
            char *mode=strtok(NULL,",");
            ctx->config.fan_mode=mode&&strcmp(mode,"ON")==0?CDI_FAN_ON:mode&&strcmp(mode,"AUTO")==0?CDI_FAN_AUTO:CDI_FAN_OFF;
            if(ctx->hal.save_config)ctx->hal.save_config(&ctx->config,sizeof(ctx->config));
            return replyf(reply,reply_size,"ACK,FAN",0,0,0,0,0,0);
        }
        if(strcmp(op,"RESET")==0){
            set_default_config(&ctx->config);ctx->boot_state=CDI_BOOT_FIRST_START;outputs_safe(ctx);
            if(ctx->hal.save_config)ctx->hal.save_config(&ctx->config,sizeof(ctx->config));
            return replyf(reply,reply_size,"ACK,SETUP_RESET",0,0,0,0,0,0);
        }
        if(strcmp(op,"EDGE")==0) {
            char *edge = strtok(NULL, ",");
            ctx->config.pickup_edge = (edge && strcmp(edge, "RISING") == 0) ? 1u : 0u;
            if(ctx->hal.save_config) ctx->hal.save_config(&ctx->config, sizeof(ctx->config));
            return replyf(reply,reply_size,"ACK,EDGE",0,0,0,0,0,0);
        }
        if(strcmp(op,"GATE_US")==0) {
            char *us = strtok(NULL, ",");
            if (us) ctx->config.gate_us = clamp_u16(strtoul(us, NULL, 10), 40u, 150u);
            if(ctx->hal.save_config) ctx->hal.save_config(&ctx->config, sizeof(ctx->config));
            return replyf(reply,reply_size,"ACK,GATE_US",0,0,0,0,0,0);
        }
        if(strcmp(op,"PICKUP")==0) {
            ctx->config.firmware_stage = 1u;
            if(ctx->hal.save_config) ctx->hal.save_config(&ctx->config, sizeof(ctx->config));
            return replyf(reply,reply_size,"ACK,PICKUP_OK",0,0,0,0,0,0);
        }
        if(strcmp(op,"SAVE_TDC")==0) {
            ctx->config.firmware_stage = 2u;
            ctx->config.map_slots[ctx->config.active_map_slot] = ctx->config.profile;
            if(ctx->hal.save_config) ctx->hal.save_config(&ctx->config, sizeof(ctx->config));
            return replyf(reply,reply_size,"ACK,TDC_SAVED",0,0,0,0,0,0);
        }
        if(strcmp(op,"TPS")==0) {
            char *pos = strtok(NULL, ",");
            if (pos && strcmp(pos, "CLOSED") == 0) {
                uint16_t adc = ctx->hal.read_raw_tps ? ctx->hal.read_raw_tps() : 0u;
                ctx->config.tps_closed_adc = adc;
                if (ctx->hal.save_config) ctx->hal.save_config(&ctx->config, sizeof(ctx->config));
                return replyf(reply, reply_size, "ACK,TPS_CLOSED", 0, 0, 0, 0, 0, 0);
            }
            if (pos && strcmp(pos, "OPEN") == 0) {
                uint16_t adc = ctx->hal.read_raw_tps ? ctx->hal.read_raw_tps() : 4095u;
                ctx->config.tps_open_adc = adc;
                ctx->config.firmware_stage = 3u;
                if (ctx->hal.save_config) ctx->hal.save_config(&ctx->config, sizeof(ctx->config));
                return replyf(reply, reply_size, "ACK,TPS_OPEN", 0, 0, 0, 0, 0, 0);
            }
            return replyf(reply,reply_size,"ACK,TPS",0,0,0,0,0,0);
        }
        if(strcmp(op,"OFFSET")==0) {
            char *val = strtok(NULL, ",");
            if (val) ctx->config.profile.trigger_angle_x10 = clamp_i16((int32_t)strtol(val, NULL, 10) / 10, 0, CDI_FORMAT_ADVANCE_MAX_X10);
            return replyf(reply,reply_size,"ACK,OFFSET",0,0,0,0,0,0);
        }
        return replyf(reply,reply_size,"ERR,SETUP_OP",0,0,0,0,0,0);
    }

    /* Support legacy R8 slot and live remap commands */
    if (strcmp(token, "LOAD") == 0) {
        char *slot_text = strtok(NULL, ",");
        uint32_t slot = slot_text ? strtoul(slot_text, NULL, 10) : 0u;
        if (slot < CDI_MAX_MAP_SLOTS) {
            ctx->config.active_map_slot = (uint8_t)slot;
            ctx->config.profile = ctx->config.map_slots[slot];
            if (ctx->hal.save_config) ctx->hal.save_config(&ctx->config, sizeof(ctx->config));
            return replyf(reply, reply_size, "ACK,LOAD%ld", slot, 0, 0, 0, 0, 0);
        }
        return replyf(reply, reply_size, "ERR,SLOT", 0, 0, 0, 0, 0, 0);
    }

    if (strcmp(token, "SAVE") == 0) {
        char *slot_text = strtok(NULL, ",");
        uint32_t slot = slot_text ? strtoul(slot_text, NULL, 10) : 0u;
        if (slot < CDI_MAX_MAP_SLOTS) {
            ctx->config.map_slots[slot] = ctx->config.profile;
            if (ctx->hal.save_config) ctx->hal.save_config(&ctx->config, sizeof(ctx->config));
            return replyf(reply, reply_size, "ACK,SAVE%ld", slot, 0, 0, 0, 0, 0);
        }
        return replyf(reply, reply_size, "ERR,SLOT", 0, 0, 0, 0, 0, 0);
    }

    if (strcmp(token, "LIVE") == 0) {
        char *tp = strtok(NULL, ","), *rp = strtok(NULL, ","), *val = strtok(NULL, ",");
        if (tp && rp && val) {
            uint32_t t = strtoul(tp, NULL, 10), r = strtoul(rp, NULL, 10);
            int32_t cd = strtol(val, NULL, 10);
            if (r < ctx->config.profile.rpm_count && t < ctx->config.profile.load_count) {
                ctx->config.profile.advance_x10[r][t] = clamp_i16((int16_t)(cd / 10),
                    ctx->config.profile.advance_min_x10, ctx->config.profile.advance_max_x10);
                return replyf(reply, reply_size, "ACK,LIVE", 0, 0, 0, 0, 0, 0);
            }
        }
        return replyf(reply, reply_size, "ERR,LIVE_ARGS", 0, 0, 0, 0, 0, 0);
    }

    if (strcmp(token, "LIMIT") == 0) {
        char *type = strtok(NULL, ","), *rpm = strtok(NULL, ","), *band = strtok(NULL, ",");
        if (type && rpm) {
            uint16_t r = clamp_u16(strtoul(rpm, NULL, 10), 1000u, CDI_FORMAT_RPM_MAX);
            ctx->config.normal_limiter_rpm = r;
            ctx->config.limiter_type = (strcmp(type, "HARD") == 0 ? 1u : 0u);
            if (band) ctx->config.soft_band_rpm = clamp_u16(strtoul(band, NULL, 10), 50u, 3000u);
            ctx->config.limiter_rpm = ctx->boot_state == CDI_BOOT_READY ? r : CDI_FIRST_START_LIMITER_RPM;
            if (ctx->hal.save_config) ctx->hal.save_config(&ctx->config, sizeof(ctx->config));
            return replyf(reply, reply_size, "ACK,LIMIT", 0, 0, 0, 0, 0, 0);
        }
    }

    if (strcmp(token, "TEMP") == 0) {
        char *op = strtok(NULL, ",");
        if (op && strcmp(op, "CAL") == 0) {
            if (!command_safe(ctx)) return replyf(reply, reply_size, "ERR,UNSAFE",0,0,0,0,0,0);
            for (uint8_t i=0u;i<3u;++i) {
                char *adc=strtok(NULL,","), *temp=strtok(NULL,",");
                if (!adc || !temp) return replyf(reply, reply_size, "ERR,CAL_ARGS",0,0,0,0,0,0);
                ctx->config.temp_cal[i].adc=clamp_u16(strtoul(adc,NULL,10),1u,4094u);
                ctx->config.temp_cal[i].temperature_x10=clamp_i16(strtol(temp,NULL,10),-400,1800);
            }
            if (ctx->hal.save_config) ctx->hal.save_config(&ctx->config,sizeof(ctx->config));
            return replyf(reply,reply_size,"OK,TEMP_CAL",0,0,0,0,0,0);
        }
    }

    if (strcmp(token, "DYNO") == 0) {
        char *op = strtok(NULL, ",");
        if (op && strcmp(op,"BEGIN")==0 && command_safe(ctx)) {
            ctx->dyno_backup=ctx->config.profile; ctx->live_trim_x10=0; ctx->telemetry.dyno_active=true;
            return replyf(reply,reply_size,"OK,DYNO_BEGIN",0,0,0,0,0,0);
        }
        if (op && strcmp(op,"TRIM")==0 && ctx->telemetry.dyno_active) {
            char *value=strtok(NULL,",");
            ctx->live_trim_x10=clamp_i16(value?strtol(value,NULL,10):0,-100,100);
            return replyf(reply,reply_size,"OK,DYNO_TRIM,%ld",ctx->live_trim_x10,0,0,0,0,0);
        }
        if (op && strcmp(op,"COMMIT")==0 && ctx->telemetry.dyno_active && command_safe(ctx)) {
            for(uint8_t r=0;r<ctx->config.profile.rpm_count;++r)
                for(uint8_t l=0;l<ctx->config.profile.load_count;++l)
                    ctx->config.profile.advance_x10[r][l]=clamp_i16(
                        ctx->config.profile.advance_x10[r][l]+ctx->live_trim_x10,
                        ctx->config.profile.advance_min_x10,ctx->config.profile.advance_max_x10);
            ctx->live_trim_x10=0; ctx->telemetry.dyno_active=false;
            if(ctx->hal.save_config) ctx->hal.save_config(&ctx->config,sizeof(ctx->config));
            return replyf(reply,reply_size,"OK,DYNO_COMMIT",0,0,0,0,0,0);
        }
        if (op && strcmp(op,"ABORT")==0 && ctx->telemetry.dyno_active) {
            ctx->config.profile=ctx->dyno_backup; ctx->live_trim_x10=0; ctx->telemetry.dyno_active=false;
            return replyf(reply,reply_size,"OK,DYNO_ABORT",0,0,0,0,0,0);
        }
        return replyf(reply,reply_size,"ERR,DYNO_STATE",0,0,0,0,0,0);
    }

    if (strcmp(token, "OTA") == 0) {
        char *op=strtok(NULL,",");
        if(op && strcmp(op,"BEGIN")==0) {
            char *first=strtok(NULL,","), *second=strtok(NULL,","), *third=strtok(NULL,",");
            char *size=third?second:first;
            char *crc=third?third:second;
            if(!size||!crc||!command_safe(ctx)) return replyf(reply,reply_size,"ERR,UNSAFE",0,0,0,0,0,0);
            ctx->ota_size=strtoul(size,NULL,10); ctx->ota_crc32=strtoul(crc,NULL,10); ctx->ota_offset=0u;
            if(!ctx->hal.ota_begin || !ctx->hal.ota_begin(ctx->ota_size,ctx->ota_crc32))
                return replyf(reply,reply_size,"ERR,OTA_BEGIN",0,0,0,0,0,0);
            ctx->telemetry.ota_active=true; ctx->boot_state=CDI_BOOT_OTA; outputs_safe(ctx);
            return replyf(reply,reply_size,"OK,OTA_BEGIN",0,0,0,0,0,0);
        }
        if(op && strcmp(op,"DATA")==0 && ctx->telemetry.ota_active) {
            char *offset=strtok(NULL,","), *hex=strtok(NULL,","); uint8_t data[96];
            if(!offset||!hex||strtoul(offset,NULL,10)!=ctx->ota_offset) return replyf(reply,reply_size,"ERR,OTA_OFFSET",0,0,0,0,0,0);
            size_t count=decode_hex(hex,data,sizeof(data));
            if(count==0u||!cdi_ota_data(ctx,ctx->ota_offset,data,count))
                return replyf(reply,reply_size,"ERR,OTA_WRITE",0,0,0,0,0,0);
            return replyf(reply,reply_size,"OK,OTA_DATA,%ld",ctx->ota_offset,0,0,0,0,0);
        }
        if(op && (strcmp(op,"END")==0||strcmp(op,"COMMIT")==0) && ctx->telemetry.ota_active) {
            if(!cdi_ota_commit(ctx)) return replyf(reply,reply_size,"ERR,OTA_END",0,0,0,0,0,0);
            return replyf(reply,reply_size,"OK,OTA_END",0,0,0,0,0,0);
        }
        if(op && strcmp(op,"ABORT")==0) {
            if(ctx->hal.ota_abort) ctx->hal.ota_abort();
            ctx->telemetry.ota_active=false; ctx->boot_state=CDI_BOOT_SAFE; outputs_safe(ctx);
            return replyf(reply,reply_size,"OK,OTA_ABORT",0,0,0,0,0,0);
        }
    }

    return replyf(reply, reply_size, "ERR,UNKNOWN",0,0,0,0,0,0);
}

static void put_u16(uint8_t *out,uint16_t value){out[0]=(uint8_t)value;out[1]=(uint8_t)(value>>8);}
static void put_u32(uint8_t *out,uint32_t value){
    out[0]=(uint8_t)value;out[1]=(uint8_t)(value>>8);out[2]=(uint8_t)(value>>16);out[3]=(uint8_t)(value>>24);
}

bool cdi_ota_data(cdi_context_t *ctx,uint32_t offset,const uint8_t *data,size_t size){
    if(!ctx||!ctx->telemetry.ota_active||offset!=ctx->ota_offset||!data||size==0u||
       offset+size>ctx->ota_size||!ctx->hal.ota_write)return false;
    if(!ctx->hal.ota_write(offset,data,size)){ctx->telemetry.faults|=CDI_FAULT_OTA;return false;}
    ctx->ota_offset+=(uint32_t)size;
    return true;
}

bool cdi_ota_commit(cdi_context_t *ctx){
    if(!ctx||!ctx->telemetry.ota_active||ctx->ota_offset!=ctx->ota_size||!ctx->hal.ota_finish||
       !ctx->hal.ota_finish()){
        if(ctx)ctx->telemetry.faults|=CDI_FAULT_OTA;
        return false;
    }
    ctx->telemetry.ota_active=false;
    ctx->boot_state=CDI_BOOT_SAFE;
    return true;
}

void cdi_build_telemetry_packet(const cdi_context_t *ctx,uint8_t kind,uint16_t sequence,uint8_t out[20]){
    memset(out,0,20u);
    put_u16(out,0xcd15u);out[2]=4u;out[3]=(uint8_t)(kind?1u:0u);put_u16(out+4,sequence);
    if(kind==0u){
        put_u16(out+6,ctx->telemetry.rpm);
        put_u16(out+8,(uint16_t)ctx->telemetry.load_pct*10u);
        put_u16(out+10,(uint16_t)((int32_t)ctx->telemetry.advance_x10*10));
        put_u16(out+12,1260u); /* 12.60V DC battery voltage */
        put_u16(out+14,ctx->telemetry.hv_volts_x10/10u);
        put_u16(out+16,ctx->telemetry.hv_volts_x10/10u);
    }else{
        put_u16(out+6,(uint16_t)((int32_t)ctx->telemetry.temperature_x10*10));
        out[8]=ctx->config.active_map_slot;
        out[9]=(ctx->telemetry.rpm>=ctx->config.limiter_rpm)?1u:0u;
        out[10]=(ctx->boot_state==CDI_BOOT_READY?0x20u:0u)|
            (ctx->telemetry.charger_enabled?0x04u:0u)|0x10u|
            (ctx->config.pro_enabled?0x40u:0u)|
            (ctx->config.diy_unplugged?0x80u:0u);
        out[11]=((ctx->config.spark_channel_mask&1u)?0x01u:0u)|
            ((ctx->config.spark_channel_mask&2u)?0x02u:0u)|
            (ctx->telemetry.fan_enabled?0x08u:0u);
        put_u16(out+12,(uint16_t)ctx->telemetry.faults);
        put_u16(out+14,(uint16_t)((int32_t)ctx->config.profile.trigger_angle_x10*10));
        out[16]=ctx->telemetry.rpm?100u:0u;
        out[17]=0u;
    }
    put_u16(out+18,cdi_crc16(out,18u));
}

void cdi_build_ota_status(const cdi_context_t *ctx,uint8_t out[16]){
    memset(out,0,16u);put_u16(out,0xcd18u);out[2]=1u;
    out[3]=(ctx->telemetry.faults&CDI_FAULT_OTA)?4u:
        ctx->telemetry.ota_active?2u:ctx->ota_size&&ctx->ota_offset==ctx->ota_size?3u:0u;
    put_u32(out+4,ctx->ota_offset);put_u32(out+8,ctx->ota_size);
    put_u16(out+12,(ctx->telemetry.faults&CDI_FAULT_OTA)?1u:0u);
    put_u16(out+14,cdi_crc16(out,14u));
}

uint16_t cdi_crc16(const uint8_t *data,size_t size){
    uint16_t crc=0xffffu;
    for(size_t i=0u;i<size;++i){
        crc^=(uint16_t)data[i]<<8;
        for(uint8_t bit=0u;bit<8u;++bit)
            crc=(uint16_t)((crc<<1)^((crc&0x8000u)?0x1021u:0u));
    }
    return crc;
}

size_t cdi_protocol_exchange(cdi_context_t *ctx,const char *frame,char *reply,size_t reply_size){
    if(!frame||frame[0]!='@')return cdi_handle_command(ctx,frame,reply,reply_size);
    const char *star=strrchr(frame,'*');
    const char *comma=strchr(frame,',');
    if(!star||!comma||comma>star)return replyf(reply,reply_size,"ERR,FRAME",0,0,0,0,0,0);
    size_t payload_len=(size_t)(star-(frame+1));
    char supplied_text[5]={0};
    if(strlen(star+1)<4u)return replyf(reply,reply_size,"ERR,FRAME",0,0,0,0,0,0);
    memcpy(supplied_text,star+1,4u);
    uint16_t supplied=(uint16_t)strtoul(supplied_text,NULL,16);
    if(cdi_crc16((const uint8_t*)(frame+1),payload_len)!=supplied)
        return replyf(reply,reply_size,"ERR,CRC",0,0,0,0,0,0);
    char sequence[16];
    size_t sequence_len=(size_t)(comma-(frame+1));
    if(sequence_len==0u||sequence_len>=sizeof(sequence))return replyf(reply,reply_size,"ERR,SEQ",0,0,0,0,0,0);
    memcpy(sequence,frame+1,sequence_len);sequence[sequence_len]='\0';
    char body[196],body_reply[220];
    size_t body_len=(size_t)(star-(comma+1));
    if(body_len>=sizeof(body))return replyf(reply,reply_size,"ERR,SIZE",0,0,0,0,0,0);
    memcpy(body,comma+1,body_len);body[body_len]='\0';
    size_t n=cdi_handle_command(ctx,body,body_reply,sizeof(body_reply));
    if(!n)return 0u;
    char payload[244];
    int pn=snprintf(payload,sizeof(payload),"%s,%s",sequence,body_reply);
    if(pn<0||(size_t)pn>=sizeof(payload))return 0u;
    uint16_t crc=cdi_crc16((const uint8_t*)payload,(size_t)pn);
    int out=snprintf(reply,reply_size,"@%s*%04X\n",payload,crc);
    return out<0?0u:(size_t)(out<(int)reply_size?out:(int)reply_size-1);
}

uint32_t cdi_crc32(const uint8_t *data, size_t size) {
    uint32_t crc=0xffffffffu;
    for(size_t i=0;i<size;++i) {
        crc^=data[i];
        for(uint8_t bit=0;bit<8u;++bit) crc=(crc>>1)^((0u-(crc&1u))&0xedb88320u);
    }
    return ~crc;
}
