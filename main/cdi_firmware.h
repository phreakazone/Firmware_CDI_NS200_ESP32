#ifndef CDI_FIRMWARE_H
#define CDI_FIRMWARE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CDI_PROTOCOL_VERSION 5u
#define CDI_CONFIG_SCHEMA 2u
#define CDI_MAX_RPM_AXIS 32u
#define CDI_MAX_LOAD_AXIS 16u
#define CDI_MAX_MAP_SLOTS 4u
#define CDI_MAX_PULSER_PPR 12u
#define CDI_FORMAT_RPM_MAX 30000u
#define CDI_FORMAT_ADVANCE_MIN_X10 (-300)
#define CDI_FORMAT_ADVANCE_MAX_X10 800

enum {
    CDI_FAULT_NONE = 0u,
    CDI_FAULT_TEMP_SENSOR = 1u << 0,
    CDI_FAULT_ADVANCE_CLIPPED = 1u << 1,
    CDI_FAULT_CONFIG = 1u << 2,
    CDI_FAULT_OTA = 1u << 3
};

typedef enum {
    CDI_FAN_OFF = 0,
    CDI_FAN_ON = 1,
    CDI_FAN_AUTO = 2
} cdi_fan_mode_t;

typedef enum {
    CDI_BOOT_FIRST_START = 0,
    CDI_BOOT_SAFE,
    CDI_BOOT_READY,
    CDI_BOOT_OTA
} cdi_boot_state_t;

typedef struct {
    uint32_t (*micros)(void);
    void (*set_ignition)(bool enabled);
    void (*set_charger)(bool enabled);
    void (*set_fan)(bool enabled);
    void (*set_strobe)(bool enabled);
    uint16_t (*read_raw_tps)(void);
    bool (*load_config)(void *data, size_t size);
    bool (*save_config)(const void *data, size_t size);
    bool (*ota_begin)(uint32_t size, uint32_t crc32);
    bool (*ota_write)(uint32_t offset, const uint8_t *data, size_t size);
    bool (*ota_finish)(void);
    void (*ota_abort)(void);
} cdi_hal_t;

typedef struct {
    char name[20];
    uint16_t rpm_min;
    uint16_t rpm_max;
    int16_t advance_min_x10;
    int16_t advance_max_x10;
    uint8_t pulser_ppr;
    int16_t trigger_angle_x10;
    uint8_t rpm_count;
    uint8_t load_count;
    uint16_t rpm_axis[CDI_MAX_RPM_AXIS];
    uint8_t load_axis[CDI_MAX_LOAD_AXIS];
    int16_t advance_x10[CDI_MAX_RPM_AXIS][CDI_MAX_LOAD_AXIS];
} cdi_profile_t;

typedef struct {
    uint16_t adc;
    int16_t temperature_x10;
} cdi_temp_point_t;

typedef struct {
    uint32_t schema;
    bool setup_complete;
    uint8_t active_map_slot;
    cdi_profile_t profile;
    cdi_profile_t map_slots[CDI_MAX_MAP_SLOTS];
    uint16_t limiter_rpm;
    uint16_t normal_limiter_rpm;
    uint16_t soft_band_rpm;
    uint8_t limiter_type;        /* 0 = SOFT, 1 = HARD */
    cdi_fan_mode_t fan_mode;
    int16_t fan_on_x10;
    int16_t fan_off_x10;
    cdi_temp_point_t temp_cal[3];
    uint8_t firmware_stage;       /* 0=BARU, 1=PULSER, 2=TDC, 3=FIRST_START, 4=READY */
    uint8_t pickup_edge;          /* 0=FALLING, 1=RISING */
    uint16_t gate_us;             /* Gate pulse width in microseconds (40..150) */
    uint16_t tps_closed_adc;      /* ADC raw at throttle closed 0% */
    uint16_t tps_open_adc;        /* ADC raw at throttle wide open 100% */
    int16_t side_offset_cdeg;     /* Side plug offset in centidegrees */
    uint16_t target_hv_volts;     /* Target HV DC bus voltage (220, 285, 345) */
    bool pro_enabled;             /* Pro 345V mode */
    bool diy_unplugged;           /* OEM CDI unplugged confirmed */
    uint8_t run_mode;             /* 0=MANUAL, 1=OEM_LEARN, 2=DIY */
    uint16_t quickshift_cut_ms;   /* Quickshifter cut time (ms) */
    uint16_t launch_limiter_rpm;  /* 2-Step launch limiter */
    bool launch_active;           /* 2-Step active */
    uint8_t spark_channel_mask;   /* Bit 0=Center, Bit 1=Side L, Bit 2=Side R */
} cdi_config_t;

typedef struct {
    uint16_t rpm;
    uint8_t load_pct;
    int16_t advance_x10;
    int16_t temperature_x10;
    uint16_t hv_volts_x10;
    bool ignition_enabled;
    bool charger_enabled;
    bool fan_enabled;
    bool dyno_active;
    bool ota_active;
    uint32_t faults;
} cdi_telemetry_t;

typedef struct {
    bool fire;
    uint32_t delay_us;
    int16_t requested_advance_x10;
    int16_t applied_advance_x10;
} cdi_trigger_result_t;

typedef struct {
    cdi_hal_t hal;
    cdi_config_t config;
    cdi_telemetry_t telemetry;
    cdi_boot_state_t boot_state;
    uint32_t last_pulse_us;
    uint16_t pulse_count;
    int16_t live_trim_x10;
    cdi_profile_t dyno_backup;
    cdi_profile_t map_staging;
    bool map_staging_active;
    uint32_t ota_size;
    uint32_t ota_crc32;
    uint32_t ota_offset;
} cdi_context_t;

void cdi_init(cdi_context_t *ctx, const cdi_hal_t *hal);
void cdi_set_profile_defaults(cdi_profile_t *profile, const char *name);
void cdi_mark_setup_complete(cdi_context_t *ctx);
void cdi_set_inputs(cdi_context_t *ctx, uint8_t load_pct, uint16_t temp_adc, uint16_t hv_x10);
void cdi_tick(cdi_context_t *ctx);
cdi_trigger_result_t cdi_on_reference_pulse(cdi_context_t *ctx, uint32_t now_us);
size_t cdi_handle_command(cdi_context_t *ctx, const char *line, char *reply, size_t reply_size);
size_t cdi_protocol_exchange(cdi_context_t *ctx, const char *frame, char *reply, size_t reply_size);
void cdi_build_telemetry_packet(const cdi_context_t *ctx, uint8_t kind, uint16_t sequence, uint8_t out[20]);
bool cdi_ota_data(cdi_context_t *ctx, uint32_t offset, const uint8_t *data, size_t size);
bool cdi_ota_commit(cdi_context_t *ctx);
void cdi_build_ota_status(const cdi_context_t *ctx, uint8_t out[16]);
uint16_t cdi_crc16(const uint8_t *data, size_t size);
uint32_t cdi_crc32(const uint8_t *data, size_t size);

#ifdef __cplusplus
}
#endif
#endif
