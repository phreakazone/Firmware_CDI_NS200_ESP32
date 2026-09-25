#ifndef CDI_MODULE_IO_H
#define CDI_MODULE_IO_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define CDI_AUX_INPUT_MAGIC 0x32495541u /* AUI2 */
#define CDI_AUX_INPUT_VERSION 1u

enum {
    CDI_REQ_KEYLESS = 1u << 0,
    CDI_REQ_START = 1u << 1,
    CDI_REQ_PIN9 = 1u << 2
};

typedef enum {
    CDI_AUX_PROFILE_NS200 = 0,
    CDI_AUX_PROFILE_UNIVERSAL_MANUAL = 1,
    CDI_AUX_PROFILE_UNIVERSAL_MATIC = 2
} cdi_aux_vehicle_profile_t;

typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t enabled;
    uint8_t vehicle_profile;
    uint8_t reserved;
} cdi_aux_input_config_t;

/* U6 @0x20: P0..P4 DET, P5..P7 EXP outputs.
 * U7 @0x21: P0 KEYLESS, P1 START, P2 MODE/NEUTRAL; all active-low. */
typedef struct {
    uint8_t output_latch;
    uint8_t present_mask;
    uint8_t candidate_mask;
    uint8_t stable_samples;
    uint8_t consecutive_errors;
    uint8_t request_mask;
    uint8_t request_candidate_mask;
    uint8_t request_stable_samples;
    uint8_t request_consecutive_errors;
    bool healthy;
    bool request_healthy;
} cdi_module_io_t;

void cdi_aux_input_config_defaults(cdi_aux_input_config_t *config);
bool cdi_aux_input_config_valid(const cdi_aux_input_config_t *config);
esp_err_t cdi_module_io_init(cdi_module_io_t *io);
bool cdi_module_io_poll(cdi_module_io_t *io);
uint8_t cdi_module_io_present_mask(const cdi_module_io_t *io);
uint8_t cdi_module_io_request_mask(const cdi_module_io_t *io);
bool cdi_module_io_healthy(const cdi_module_io_t *io);
bool cdi_module_io_requests_healthy(const cdi_module_io_t *io);
/* Requires AUX PNP pre-driver: PCF HIGH=OFF, LOW=ON. */
bool cdi_module_io_set_aux(cdi_module_io_t *io, bool keyless_on,
                           bool starter_on, bool exp3_on);

#endif
