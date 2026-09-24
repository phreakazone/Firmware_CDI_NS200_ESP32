#ifndef CDI_MODULE_IO_H
#define CDI_MODULE_IO_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/* PCF8574P U6 @0x20: P0..P4 DET active-low, P5..P7 EXP active-low. */
typedef struct {
    uint8_t output_latch;
    uint8_t present_mask;
    uint8_t candidate_mask;
    uint8_t stable_samples;
    uint8_t consecutive_errors;
    bool healthy;
} cdi_module_io_t;

esp_err_t cdi_module_io_init(cdi_module_io_t *io);
bool cdi_module_io_poll(cdi_module_io_t *io);
uint8_t cdi_module_io_present_mask(const cdi_module_io_t *io);
bool cdi_module_io_healthy(const cdi_module_io_t *io);
/* Requires AUX PNP pre-driver: PCF HIGH=OFF, LOW=ON. */
bool cdi_module_io_set_aux(cdi_module_io_t *io, bool keyless_on,
                           bool starter_on, bool exp3_on);

#endif
