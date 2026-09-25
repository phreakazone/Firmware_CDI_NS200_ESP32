#ifndef CDI_R5_PROTOCOL_H
#define CDI_R5_PROTOCOL_H

#include "cdi_r5.h"
#include "cdi_r8_oem_learn.h"
#include "cdi_r8_ota.h"
#include "cdi_timing_modes.h"
#include "cdi_module_io.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CDI_FW_RELEASE "R9"
#define CDI_FW_SEMVER "9.6.2"
#define CDI_FW_BUILD_ID "20260925"
#define CDI_DEVICE_SERIAL_LEN 32u

typedef bool (*cdi_r5_persist_fn)(const cdi_r5_store_image_t *image,
                                  void *context);
typedef bool (*cdi_r9_timing_persist_fn)(const cdi_timing_config_t *config,
                                         void *context);
typedef bool (*cdi_r9_aux_control_fn)(uint8_t channel, bool enable,
                                      uint16_t duration_ms, void *context);
typedef bool (*cdi_r9_aux_input_persist_fn)(
    const cdi_aux_input_config_t *config, void *context);

enum {
    CDI_R9_AUX_KEYLESS = 0,
    CDI_R9_AUX_STARTER = 1,
    CDI_R9_AUX_ALL = 2
};

enum {
    CDI_R9_CONTACT_OFF = 0,
    CDI_R9_CONTACT_MECHANICAL = 1,
    CDI_R9_CONTACT_KEYLESS = 2
};

typedef struct {
    cdi_r5_store_image_t *store;
    cdi_r5_map_t working;
    cdi_r5_map_t staging;
    cdi_r5_map_t dyno_backup;
    uint32_t rpm;
    uint16_t tps_permille, tps_raw, temp_raw, tps_ref_raw;
    uint16_t hv_center, hv_side, vbat_raw;
    uint16_t setup_trigger_cdeg, pickup_quality, strobe_samples, first_start_seconds;
    int16_t temperature_cdeg, live_trim_cdeg;
    bool temperature_valid, fan_output, hardware_fault;
    bool hv_enabled, output_permission, pro_enabled, strobe_active;
    bool firmware_update_active, map_staging_active, dyno_active;
    uint8_t module_present_mask;
    bool module_io_ok, aux_keyless_on, aux_starter_on;
    bool aux_mechanical_on, aux_ignition_allowed, aux_engine_running;
    uint8_t aux_contact_source;
    cdi_timing_config_t *timing;
    cdi_r9_timing_persist_fn timing_persist;
    void *timing_persist_context;
    cdi_r9_aux_control_fn aux_control;
    void *aux_control_context;
    cdi_aux_input_config_t *aux_input_config;
    cdi_r9_aux_input_persist_fn aux_input_persist;
    void *aux_input_persist_context;
    uint8_t aux_request_mask;
    bool aux_request_io_ok;
    cdi_r8_oem_learner_t *oem_learner;
    cdi_r8_ota_t *ota;
    cdi_r5_persist_fn persist;
    void *persist_context;
    char device_serial[CDI_DEVICE_SERIAL_LEN];
} cdi_r5_protocol_t;

uint16_t cdi_r5_crc16(const void *data, size_t length);
void cdi_r5_protocol_init(cdi_r5_protocol_t *protocol,
                          cdi_r5_store_image_t *store);
void cdi_r5_protocol_set_persist(cdi_r5_protocol_t *protocol,
                                 cdi_r5_persist_fn persist,
                                 void *context);
void cdi_r5_protocol_set_identity(cdi_r5_protocol_t *protocol,
                                  const char *device_serial);
void cdi_r8_protocol_attach_oem_learner(cdi_r5_protocol_t *protocol,
                                        cdi_r8_oem_learner_t *learner);
void cdi_r8_protocol_attach_ota(cdi_r5_protocol_t *protocol,
                                cdi_r8_ota_t *ota);
void cdi_r9_protocol_attach_timing(cdi_r5_protocol_t *protocol,
                                   cdi_timing_config_t *config,
                                   cdi_r9_timing_persist_fn persist,
                                   void *context);
void cdi_r9_protocol_attach_aux(cdi_r5_protocol_t *protocol,
                                cdi_r9_aux_control_fn control,
                                void *context);
void cdi_r9_protocol_attach_aux_inputs(
    cdi_r5_protocol_t *protocol, cdi_aux_input_config_t *config,
    cdi_r9_aux_input_persist_fn persist, void *context);
/* Frame: @sequence,COMMAND,args*CRC16. Returns response length, zero on overflow. */
size_t cdi_r5_protocol_handle(cdi_r5_protocol_t *protocol,
                              const char *frame,
                              char *response,
                              size_t response_size);

#endif
