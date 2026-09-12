#ifndef CDI_TIMEBASE_H
#define CDI_TIMEBASE_H
/*
 * cdi_timebase — satu clock 1 MHz bersama untuk seluruh sistem CDI.
 *
 * STM32 asli memakai TIM2 (32-bit, 4 MHz): CH1 sebagai input capture pickup,
 * CH2/CH3/CH4 sebagai output-compare "tunda lalu toggle" untuk gate CENTER,
 * SIDE, dan strobo — semuanya berbagi SATU counter sehingga "kapan pulsa
 * masuk" dan "kapan busi harus menyala" berada di garis waktu yang identik,
 * tanpa konversi apa pun.
 *
 * ESP32 tidak punya satu timer dengan 4 unit compare seperti itu, jadi modul
 * ini meniru perilakunya di atas SATU hardware gptimer (TIMG) 64-bit free
 * running 1 MHz:
 *   - now = pembacaan langsung counter (dipanggil dari ISR GPIO pickup)
 *   - tiga "slot" software (CENTER, SIDE, STROBE) masing-masing menyimpan
 *     target tick absolut berikutnya
 *   - satu ISR alarm gptimer mengeksekusi slot mana pun yang sudah jatuh
 *     tempo, lalu memuat ulang alarm ke slot pending terdekat
 *
 * Karena hanya ada SATU sumber waktu, tidak ada offset antar channel yang
 * perlu dikompensasi — sama seperti TIM2 di firmware asli. ISR ini di-pin ke
 * IRAM (lihat cdi_timebase.c) dan dijalankan pada level interrupt tinggi,
 * bukan lewat task/queue, agar tidak terkena jitter scheduler FreeRTOS atau
 * cache-miss flash.
 */
#include <stdbool.h>
#include <stdint.h>
#include "esp_attr.h"

typedef enum {
    CDI_TB_SLOT_CENTER = 0,
    CDI_TB_SLOT_SIDE,
    CDI_TB_SLOT_STROBE,
    CDI_TB_SLOT_COUNT
} cdi_tb_slot_t;

/* Dipanggil dari ISR alarm saat slot ini jatuh tempo. Kembalikan true jika
 * slot harus dijadwalkan ulang (mis. fase "buka" -> jadwalkan "tutup"),
 * false jika siklus selesai dan slot boleh nonaktif. Jika true, isi
 * *next_tick dengan target absolut berikutnya. Callback ini berjalan di
 * konteks ISR IRAM: JANGAN memanggil apa pun yang menyentuh flash/NVS/log. */
typedef bool (*cdi_tb_slot_cb_t)(void *ctx, uint64_t due_tick, uint64_t *next_tick);

void cdi_timebase_init(void);

/* Baca counter master. Aman dipanggil dari ISR (IRAM). */
uint64_t IRAM_ATTR cdi_timebase_now(void);

/* Pasang/replace handler untuk sebuah slot (dipanggil sekali saat boot). */
void cdi_timebase_set_slot_handler(cdi_tb_slot_t slot, cdi_tb_slot_cb_t cb, void *ctx);

/* Jadwalkan slot untuk berbunyi pada tick absolut tertentu. Aman dipanggil
 * dari ISR (IRAM) maupun task biasa (dilindungi critical section pendek). */
void IRAM_ATTR cdi_timebase_arm(cdi_tb_slot_t slot, uint64_t target_tick);

/* Matikan slot (dipakai saat force-safe / output_permission hilang). */
void IRAM_ATTR cdi_timebase_disarm(cdi_tb_slot_t slot);

#endif
