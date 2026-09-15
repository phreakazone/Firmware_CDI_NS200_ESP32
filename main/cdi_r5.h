#ifndef CDI_R5_H
#define CDI_R5_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CDI_R5_RPM_POINTS 32u
#define CDI_R5_TPS_POINTS 16u
#define CDI_R5_MAP_SLOTS 4u
#define CDI_R5_NAME_LEN 20u
#define CDI_R5_STORE_MAGIC 0x37494443u /* "CDI7" little-endian */
#define CDI_R5_STORE_VERSION 5u
#define CDI_R5_ABSOLUTE_RPM_CAP 30000u
#define CDI_R9_ADVANCE_MIN_CDEG (-3000)
#define CDI_R9_ADVANCE_MAX_CDEG 8000
#define CDI_R9_MAX_PPR 12u
#define CDI_R7_SETUP_MAGIC 0x37505553u
#define CDI_R7_SETUP_VERSION 3u

typedef enum { CDI_R5_MODE_NORMAL = 0, CDI_R5_MODE_PRO = 1 } cdi_r5_mode_t;
typedef enum { CDI_R5_LIMITER_SOFT = 0, CDI_R5_LIMITER_HARD = 1 } cdi_r5_limiter_t;
typedef enum {
    CDI_R5_OK = 0, CDI_R5_ERR_ARGUMENT, CDI_R5_ERR_MAP,
    CDI_R5_ERR_PRO_LOCKED, CDI_R5_ERR_DISARMED, CDI_R5_ERR_PERIOD,
    CDI_R5_ERR_ADVANCE, CDI_R5_ERR_LIVE_STEP, CDI_R5_ERR_ENGINE_RUNNING,
    CDI_R5_ERR_HV_ACTIVE, CDI_R5_ERR_CRC
} cdi_r5_status_t;
typedef enum {
    CDI_R5_SPARK_FIRE = 0, CDI_R5_SPARK_SOFT_CUT, CDI_R5_SPARK_HARD_CUT
} cdi_r5_spark_action_t;
typedef enum { CDI_R7_STAGE_NEW=0, CDI_R7_STAGE_PICKUP_OK, CDI_R7_STAGE_TDC_SAVED,
    CDI_R7_STAGE_FIRST_START, CDI_R7_STAGE_READY } cdi_r7_setup_stage_t;
typedef enum { CDI_R7_EDGE_FALLING=0, CDI_R7_EDGE_RISING=1 } cdi_r7_pickup_edge_t;
typedef enum { CDI_R7_FAN_OFF=0, CDI_R7_FAN_ON=1, CDI_R7_FAN_AUTO=2 } cdi_r7_fan_mode_t;
typedef enum {
    CDI_R8_OP_MANUAL_SETUP = 0,
    CDI_R8_OP_OEM_LEARN = 1,
    CDI_R8_OP_DIY = 2
} cdi_r8_operating_mode_t;

#define CDI_R8_LEARN_CELLS (CDI_R5_RPM_POINTS * CDI_R5_TPS_POINTS)

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t accepted_pulses;
    uint16_t rejected_pulses;
    uint16_t side_samples;
    int16_t side_offset_cdeg;
    int16_t advance_cdeg[CDI_R5_TPS_POINTS][CDI_R5_RPM_POINTS];
    uint8_t samples[CDI_R5_TPS_POINTS][CDI_R5_RPM_POINTS];
    uint8_t valid;
    uint8_t reserved[3];
} cdi_r8_oem_profile_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint8_t stage, pickup_edge;
    uint16_t trigger_angle_cdeg;
    int16_t side_offset_cdeg;
    uint16_t pulses_per_revolution, gate_pulse_us;
    uint16_t tps_closed_adc, tps_open_adc;
    uint16_t first_start_hv_volts, first_start_rpm_limit;
    uint16_t first_start_advance_cap_cdeg;
    uint8_t center_enabled, side_enabled, fan_mode, operating_mode;
    uint8_t pro_enabled, diy_oem_unplug_confirmed, first_start_proven, reserved;
    char profile_name[CDI_R5_NAME_LEN];
    uint16_t profile_rpm_min, profile_rpm_max;
    int16_t profile_advance_min_cdeg, profile_advance_max_cdeg;
    uint16_t fan_on_cdeg, fan_off_cdeg;
    uint16_t temp_adc[3];
    int16_t temp_cdeg[3];
} cdi_r7_setup_t;

typedef struct {
    char name[CDI_R5_NAME_LEN];
    uint8_t mode, limiter_type, rpm_count, tps_count;
    uint16_t rpm_limit, soft_band_rpm, hv_target_volts, generation;
    uint16_t rpm_axis[CDI_R5_RPM_POINTS];
    uint16_t tps_axis[CDI_R5_TPS_POINTS];
    int16_t advance_cdeg[CDI_R5_TPS_POINTS][CDI_R5_RPM_POINTS];
} cdi_r5_map_t;

typedef struct {
    uint32_t timer_hz;
    uint16_t pulses_per_revolution;
    uint16_t trigger_angle_cdeg;
    int16_t side_offset_cdeg;
    uint16_t gate_pulse_us;
    uint16_t rpm_limit_override, advance_cap_cdeg, hv_target_override;
    int16_t advance_trim_cdeg, advance_min_cdeg, advance_max_cdeg;
    bool calibrated, output_permission, pro_enabled, center_enabled, side_enabled;
} cdi_r5_engine_config_t;

typedef struct {
    uint32_t rpm;
    int16_t advance_cdeg;
    uint16_t hv_target_volts;
    uint32_t center_delay_ticks, side_delay_ticks, gate_width_ticks;
    cdi_r5_spark_action_t action;
} cdi_r5_decision_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint8_t active_slot, reserved;
    cdi_r5_map_t slots[CDI_R5_MAP_SLOTS];
    cdi_r7_setup_t setup;
    cdi_r8_oem_profile_t oem_profile;
    uint32_t crc32;
} cdi_r5_store_image_t;

void cdi_r5_load_defaults(cdi_r5_store_image_t *image);
uint32_t cdi_r5_crc32(const void *data, size_t length);
void cdi_r5_store_seal(cdi_r5_store_image_t *image);
cdi_r5_status_t cdi_r7_setup_validate(const cdi_r7_setup_t *setup);
cdi_r5_status_t cdi_r5_store_validate(const cdi_r5_store_image_t *image);
cdi_r5_status_t cdi_r5_map_validate(const cdi_r5_map_t *map, bool pro_unlocked);
int16_t cdi_r5_map_interpolate(const cdi_r5_map_t *map, uint32_t rpm,
                               uint16_t tps_permille);
cdi_r5_status_t cdi_r5_make_decision(const cdi_r5_engine_config_t *engine,
                                     const cdi_r5_map_t *map,
                                     uint32_t period_ticks,
                                     uint16_t tps_permille,
                                     uint8_t *soft_phase,
                                     cdi_r5_decision_t *decision);
cdi_r5_status_t cdi_r5_live_set_cell(cdi_r5_map_t *map, uint8_t tps_index,
                                     uint8_t rpm_index, int16_t advance_cdeg,
                                     bool engine_running, bool pro_unlocked);
cdi_r5_status_t cdi_r5_save_slot(cdi_r5_store_image_t *image, uint8_t slot,
                                 const cdi_r5_map_t *map, uint32_t engine_rpm,
                                 bool hv_enabled, bool pro_unlocked);
bool cdi_r9_temperature_from_adc(const cdi_r7_setup_t *setup, uint16_t adc,
                                 int16_t *temperature_cdeg);
bool cdi_r9_fan_update(const cdi_r7_setup_t *setup, int16_t temperature_cdeg,
                       bool temperature_valid, bool previous_output);

#endif
