#include "cdi_r5_ble.h"

#include <string.h>

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xffu);
    p[1] = (uint8_t)(v >> 8u);
}

static uint16_t get16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8u);
}

size_t cdi_r5_ble_encode_telemetry(const cdi_r5_ble_telemetry_t *in,
                                   uint8_t *out, size_t out_size)
{
    uint16_t crc;
    if (in == NULL || out == NULL || out_size < CDI_R5_BLE_TELEMETRY_SIZE)
        return 0u;
    put16(out + 0u, CDI_R5_BLE_MAGIC);
    out[2] = CDI_R5_BLE_VERSION;
    out[3] = (uint8_t)(in->sequence & 1u); /* alternate CORE/DIAGNOSTIC */
    put16(out + 4u, in->sequence);
    if (out[3] == CDI_R5_BLE_KIND_CORE) {
        put16(out + 6u, in->rpm);
        put16(out + 8u, in->tps_permille);
        put16(out + 10u, (uint16_t)in->advance_cdeg);
        put16(out + 12u, in->battery_centivolts);
        put16(out + 14u, in->hv_center_volts);
        put16(out + 16u, in->hv_side_volts);
    } else {
        put16(out + 6u, (uint16_t)in->temperature_cdeg);
        out[8] = in->active_slot;
        out[9] = in->limiter_state;
        out[10] = in->flags;
        out[11] = in->output_flags;
        put16(out + 12u, in->fault_bits);
        put16(out + 14u, in->trigger_angle_cdeg);
        out[16] = in->pickup_quality;
        out[17] = in->first_start_seconds;
    }
    crc = cdi_r5_crc16(out, 18u); put16(out + 18u, crc);
    return CDI_R5_BLE_TELEMETRY_SIZE;
}

bool cdi_r5_ble_validate_telemetry(const uint8_t *packet, size_t length)
{
    return packet != NULL && length == CDI_R5_BLE_TELEMETRY_SIZE &&
           get16(packet) == CDI_R5_BLE_MAGIC &&
           packet[2] == CDI_R5_BLE_VERSION && packet[3] <= CDI_R5_BLE_KIND_DIAGNOSTIC &&
           get16(packet + 18u) == cdi_r5_crc16(packet, 18u);
}

size_t cdi_r5_ble_handle_command(cdi_r5_protocol_t *protocol,
                                 const uint8_t *data, size_t length,
                                 uint8_t *reply, size_t reply_size)
{
    char frame[256];
    if (protocol == NULL || data == NULL || reply == NULL ||
        length == 0u || length >= sizeof(frame))
        return 0u;
    memcpy(frame, data, length);
    frame[length] = '\0';
    return cdi_r5_protocol_handle(protocol, frame, (char *)reply, reply_size);
}
