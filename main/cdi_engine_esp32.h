#ifndef CDI_ENGINE_ESP32_H
#define CDI_ENGINE_ESP32_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

void cdi_engine_init(void);
void cdi_engine_tick_1ms(void);
size_t cdi_engine_handle_command(const uint8_t *data, size_t length, uint8_t *reply, size_t reply_size);
void cdi_engine_set_ble_connected(bool connected);
bool cdi_ble_is_connected(void);

/* Handler BLE OTA */
void cdi_engine_handle_ota_data(const uint8_t *data, size_t len);
void cdi_ble_notify_ota_status(const uint8_t *data, uint16_t len);

/* Notifikasi Telemetry & Command */
void cdi_ble_notify_telemetry(const uint8_t *d, uint16_t n);
void cdi_ble_notify_response(const uint8_t *d, uint16_t n);

#endif