#ifndef CDI_BLE_NIMBLE_H
#define CDI_BLE_NIMBLE_H
#include <stdbool.h>

/* Panggil sekali dari app_main() setelah cdi_engine_init(). */
void cdi_ble_init(void);

/* Dipanggil internal setelah sync/disconnect; diekspos jika ingin dipicu
 * ulang manual (mis. tombol "mode pairing" fisik). */
void cdi_ble_start_advertising(void);

/* Dipanggil dari cdi_engine_esp32.c saat status sambungan berubah. */
void cdi_engine_set_ble_connected(bool connected);

#endif
