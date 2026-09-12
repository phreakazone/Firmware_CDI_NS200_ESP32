#ifndef CDI_R5_PROTOCOL_H
#define CDI_R5_PROTOCOL_H

#include "cdi_r5.h"
#include "cdi_r8_oem_learn.h"
#include "cdi_r8_ota.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef bool (*cdi_r5_persist_fn)(const cdi_r5_store_image_t *image,
                                  void *context);

typedef struct {
    cdi_r5_store_image_t *store;
    cdi_r5_map_t working;
    uint32_t rpm;
    uint16_t tps_permille, tps_raw, hv_center, hv_side;
    uint16_t setup_trigger_cdeg, pickup_quality, strobe_samples, first_start_seconds;
    bool hv_enabled, output_permission, pro_enabled, strobe_active;
    bool firmware_update_active;
    cdi_r8_oem_learner_t *oem_learner;
    cdi_r8_ota_t *ota;
    cdi_r5_persist_fn persist;
    void *persist_context;
} cdi_r5_protocol_t;

uint16_t cdi_r5_crc16(const void *data, size_t length);
void cdi_r5_protocol_init(cdi_r5_protocol_t *protocol,
                          cdi_r5_store_image_t *store);
void cdi_r5_protocol_set_persist(cdi_r5_protocol_t *protocol,
                                 cdi_r5_persist_fn persist,
                                 void *context);
void cdi_r8_protocol_attach_oem_learner(cdi_r5_protocol_t *protocol,
                                        cdi_r8_oem_learner_t *learner);
void cdi_r8_protocol_attach_ota(cdi_r5_protocol_t *protocol,
                                cdi_r8_ota_t *ota);
/* Frame: @sequence,COMMAND,args*CRC16. Returns response length, zero on overflow. */
size_t cdi_r5_protocol_handle(cdi_r5_protocol_t *protocol,
                              const char *frame,
                              char *response,
                              size_t response_size);

#endif
