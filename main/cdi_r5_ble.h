#ifndef CDI_R5_BLE_H
#define CDI_R5_BLE_H

#include "cdi_r5_protocol.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CDI_R5_BLE_TELEMETRY_SIZE 20u
#define CDI_R5_BLE_MAGIC 0xCD15u
#define CDI_R5_BLE_VERSION 3u
#define CDI_R5_BLE_KIND_CORE 0u
#define CDI_R5_BLE_KIND_DIAGNOSTIC 1u

/* Custom service UUIDs, byte order as printed in Android. */
#define CDI_R5_BLE_SERVICE_UUID  "7a8f1000-6c9d-4e40-a45f-0b4b4e533230"
#define CDI_R5_BLE_TELEM_UUID    "7a8f1001-6c9d-4e40-a45f-0b4b4e533230"
#define CDI_R5_BLE_COMMAND_UUID  "7a8f1002-6c9d-4e40-a45f-0b4b4e533230"
#define CDI_R5_BLE_RESPONSE_UUID "7a8f1003-6c9d-4e40-a45f-0b4b4e533230"
#define CDI_R8_BLE_OTA_DATA_UUID "7a8f1004-6c9d-4e40-a45f-0b4b4e533230"
#define CDI_R8_BLE_OTA_STATUS_UUID "7a8f1005-6c9d-4e40-a45f-0b4b4e533230"
#define CDI_R8_BLE_OTA_STATUS_SIZE 16u

enum {
    CDI_R5_TF_ARM         = 1u << 0,
    CDI_R5_TF_PRO_JUMPER  = 1u << 1,
    CDI_R5_TF_HV_ENABLED  = 1u << 2,
    CDI_R5_TF_CALIBRATED  = 1u << 3,
    CDI_R5_TF_BLE_LINK=1u<<4, CDI_R5_TF_READY=1u<<5, CDI_R5_TF_FIRST_START=1u<<6
};
enum { CDI_R7_OF_CENTER=1u<<0, CDI_R7_OF_SIDE=1u<<1, CDI_R7_OF_STROBE=1u<<2, CDI_R7_OF_FAN=1u<<3 };

enum {
    CDI_R5_FAULT_HW_CLAMP       = 1u << 0,
    CDI_R5_FAULT_BATTERY        = 1u << 1,
    CDI_R5_FAULT_HV_OVERVOLT    = 1u << 2,
    CDI_R5_FAULT_HV_IMBALANCE   = 1u << 3,
    CDI_R5_FAULT_PICKUP_TIMEOUT = 1u << 4,
    CDI_R5_FAULT_CALIBRATION    = 1u << 5,
    CDI_R5_FAULT_MAP_CRC        = 1u << 6
};

typedef struct {
    uint16_t sequence;
    uint16_t rpm;
    uint16_t tps_permille;
    int16_t advance_cdeg;
    uint16_t battery_centivolts;
    uint16_t hv_center_volts;
    uint16_t hv_side_volts;
    int16_t temperature_cdeg;
    uint8_t active_slot;
    uint8_t limiter_state; /* 0 fire, 1 soft cut, 2 hard cut */
    uint8_t flags;
    uint16_t fault_bits;
    uint8_t setup_stage, output_flags;
    uint16_t trigger_angle_cdeg;
    uint8_t pickup_quality, first_start_seconds;
} cdi_r5_ble_telemetry_t;

size_t cdi_r5_ble_encode_telemetry(const cdi_r5_ble_telemetry_t *in,
                                   uint8_t *out, size_t out_size);
bool cdi_r5_ble_validate_telemetry(const uint8_t *packet, size_t length);

/*
 * COMMAND characteristic transports exactly one existing ASCII frame
 * @sequence,command,args*CRC16. RESPONSE is notified with the handler reply.
 * This bridge cannot create ARM/FIRE/HV-ON commands because the protocol
 * deliberately implements none.
 */
size_t cdi_r5_ble_handle_command(cdi_r5_protocol_t *protocol,
                                 const uint8_t *data, size_t length,
                                 uint8_t *reply, size_t reply_size);

#endif
