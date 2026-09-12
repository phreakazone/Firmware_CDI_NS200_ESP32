#ifndef CDI_R8_OTA_H
#define CDI_R8_OTA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CDI_R8_HW_ID 0x57423535u /* "WB55" */
#define CDI_R8_BOOT_ADDR 0x08000000u
#define CDI_R8_APP_ADDR 0x08008000u
#define CDI_R8_APP_MAX_SIZE 0x00030000u
#define CDI_R8_STAGE_ADDR 0x08038000u
#define CDI_R8_OTA_META_ADDR 0x0807C000u
#define CDI_R8_OTA_META_MAGIC 0x3854414Fu /* "OAT8" */

typedef enum {
    CDI_R8_OTA_IDLE=0,
    CDI_R8_OTA_ERASING,
    CDI_R8_OTA_RECEIVING,
    CDI_R8_OTA_READY,
    CDI_R8_OTA_ERROR
} cdi_r8_ota_state_t;

typedef struct {
    uint32_t magic;
    uint32_t hardware_id;
    uint32_t image_version;
    uint32_t image_length;
    uint32_t image_crc32;
    uint32_t staged_address;
    uint32_t header_crc32;
    uint32_t inverse_crc32;
} cdi_r8_ota_manifest_t;

typedef bool (*cdi_r8_ota_erase_fn)(void *context);
typedef bool (*cdi_r8_ota_program_fn)(uint32_t offset, const uint8_t *data,
                                      size_t length, void *context);
typedef bool (*cdi_r8_ota_finalize_fn)(const cdi_r8_ota_manifest_t *manifest,
                                       void *context);

typedef struct {
    cdi_r8_ota_state_t state;
    uint32_t image_version;
    uint32_t expected_length;
    uint32_t expected_crc32;
    uint32_t received;
    uint32_t running_crc32;
    uint16_t error_code;
    cdi_r8_ota_erase_fn erase;
    cdi_r8_ota_program_fn program;
    cdi_r8_ota_finalize_fn finalize;
    void *io_context;
} cdi_r8_ota_t;

void cdi_r8_ota_init(cdi_r8_ota_t *ota, cdi_r8_ota_erase_fn erase,
                     cdi_r8_ota_program_fn program,
                     cdi_r8_ota_finalize_fn finalize, void *context);
bool cdi_r8_ota_begin(cdi_r8_ota_t *ota, uint32_t image_version,
                      uint32_t image_length, uint32_t image_crc32,
                      bool engine_stopped, bool outputs_safe);
bool cdi_r8_ota_write(cdi_r8_ota_t *ota, uint32_t offset,
                      const uint8_t *data, size_t length);
bool cdi_r8_ota_commit(cdi_r8_ota_t *ota);
void cdi_r8_ota_abort(cdi_r8_ota_t *ota);
bool cdi_r8_ota_manifest_valid(const cdi_r8_ota_manifest_t *manifest);

#endif
