#ifndef CDI_SELFTEST_H
#define CDI_SELFTEST_H
/*
 * cdi_selftest — mengukur jitter TANPA osiloskop, dengan menjadikan ESP32
 * sendiri sebagai alat ukur.
 *
 * IDE DASAR:
 * cdi_timebase.c sudah tahu persis kapan CENTER "seharusnya" menyala
 * (nilai `due` yang dikirim ke center_slot_cb, dalam satuan tick gptimer
 * 1 MHz yang sama untuk seluruh sistem). Kalau kita sambung kabel jumper
 * dari CDI_PIN_GATE_CENTER kembali ke satu pin input bebas, lalu tangkap
 * edge itu dengan GPIO ISR yang langsung membaca cdi_timebase_now(), kita
 * dapat selisih (tick_aktual - tick_seharusnya) TANPA instrumen eksternal
 * apa pun -- karena keduanya diukur dari jam yang sama persis.
 *
 * Bias tetap (rata-rata selisih) mencerminkan latensi ISR alarm + gpio_set_level
 * yang memang selalu ada (biasanya kecil, sub-mikrodetik s/d beberapa
 * mikrodetik) -- ini NORMAL, bukan jitter. Yang perlu diperhatikan adalah
 * SEBARAN-nya (selisih antara sampel min dan max, atau stddev): itulah
 * jitter yang sesungguhnya.
 *
 * PEMASANGAN (bench saja, JANGAN dipasang saat firmware sungguhan jalan
 * di motor -- pin loopback ini tidak dipakai sama sekali untuk operasi
 * normal):
 *   1. Kabel jumper: CDI_PIN_GATE_CENTER (GPIO25) -> CDI_PIN_SELFTEST_LOOPBACK (GPIO5)
 *   2. Simulasikan pulsa pickup dari alat kamu (lihat cdi_selftest.c untuk
 *      rumus frekuensi) ke CDI_PIN_PA0_PICKUP (GPIO4) -- CEK DULU tegangan
 *      keluaran alatnya dengan fungsi "Voltage test"-nya sendiri sebelum
 *      disambung; ESP32 GPIO TIDAK tahan >3.3V. Kalau alat kamu keluar 5V
 *      atau ikut tegangan baterai 9V, pasang pembagi tegangan (mis. 10k/10k)
 *      dulu sebelum masuk ke GPIO4.
 *   3. Buka serial monitor, tunggu ringkasan statistik tercetak tiap ~1 detik.
 */
#include <stdint.h>

/* Panggil sekali di app_main(), SETELAH cdi_engine_init() (supaya GPIO gate
 * sudah dikonfigurasi) dan SETELAH kamu menambahkan baris
 * cdi_selftest_mark_center_due(due); di center_slot_cb (lihat instruksi
 * patch). Aman dibiarkan terpasang di kode; cukup jangan sambungkan kabel
 * jumper-nya saat firmware dipasang ke motor sungguhan. */
void cdi_selftest_init(void);

/* Dipanggil dari dalam center_slot_cb (konteks ISR IRAM) tepat sebelum atau
 * sesudah gpio_set_level(CDI_PIN_GATE_CENTER, 1). Hanya menyimpan nilai,
 * tidak melakukan apa pun yang berat. */
void cdi_selftest_mark_center_due(uint64_t due_tick);

#endif
