# Uji Jitter Tanpa Osiloskop — Self-Test Loopback

## 1. Taruh file

Copy `cdi_selftest.h` dan `cdi_selftest.c` ke folder `main/` proyekmu (satu
level dengan `cdi_engine_esp32.c`). `main/CMakeLists.txt` biasanya sudah
otomatis mengambil semua `.c` di folder itu (cek `SRCS`/`GLOB` di sana) —
kalau daftar SRCS-nya eksplisit satu-satu, tambahkan `"cdi_selftest.c"`.

## 2. Patch 2 baris ke `cdi_engine_esp32.c`

Tambahkan include di bagian atas:
```c
#include "cdi_selftest.h"
```

Lalu di dalam `center_slot_cb`, tambahkan SATU baris (letak persis tidak
penting, sebelum atau sesudah `gpio_set_level` sama saja karena keduanya
tetap dalam ISR yang sama):

```c
static bool IRAM_ATTR center_slot_cb(void *ctx, uint64_t due, uint64_t *next)
{
    (void)ctx;
    if (!s_center_high) {
        cdi_selftest_mark_center_due(due);      // <-- BARIS BARU
        gpio_set_level(CDI_PIN_GATE_CENTER, 1);
        s_center_high = true;
        *next = due + s_center_width;
        return true;
    }
    gpio_set_level(CDI_PIN_GATE_CENTER, 0);
    s_center_high = false;
    return false;
}
```

Tidak ada file lain yang perlu disentuh — `cdi_r5.c`, `cdi_timebase.c`, dan
seluruh algoritma inti tetap seperti apa adanya.

## 3. Patch `main.c`

Tambahkan include:
```c
#include "cdi_selftest.h"
```

Panggil setelah `cdi_engine_init()` (urutan di bawah `cdi_ble_init()` juga
boleh, tidak kritis):
```c
    cdi_engine_init();
    cdi_selftest_init();   // <-- BARIS BARU, hapus/comment lagi kalau sudah selesai uji
    cdi_ble_init();
```

Build ulang seperti biasa (`idf.py build`).

## 4. Sambungan kabel

| Dari | Ke | Keterangan |
|---|---|---|
| GPIO25 (`CDI_PIN_GATE_CENTER`) | GPIO5 (loopback) | Jumper langsung, tidak perlu resistor — sama-sama 3.3V logic ESP32 |
| Output "Square wave PWM" tester | GPIO4 (`CDI_PIN_PA0_PICKUP`) | **CEK TEGANGAN DULU, lihat langkah 5** |
| GND tester | GND ESP32 | Wajib, referensi bersama |

**Jangan** menyambungkan apa pun ke port HV/charger/koil selama uji ini —
cukup pickup dan gate saja.

## 5. WAJIB: cek tegangan keluaran tester sebelum disambung ke GPIO4

ESP32 tidak tahan >3.3V di pin GPIO. Alat kamu punya fungsi "Voltage test"
sendiri — pakai itu (atau multimeter) untuk mengukur amplitudo keluaran
"Square wave PWM" SEBELUM disambung ke ESP32:

- Kalau terukur ≤3.3V: sambung langsung.
- Kalau lebih tinggi (banyak modul seperti ini ikut tegangan suplai —
  bisa 5V dari USB-C atau bahkan ~9V kalau kamu pakai jepitan baterai 9V):
  pasang pembagi tegangan sederhana 2 resistor sama besar (mis. 10kΩ dan
  10kΩ) dari keluaran tester ke GND, ambil sinyal dari titik tengah ke
  GPIO4. Itu membagi dua tegangannya — ukur lagi hasilnya dengan
  "Voltage test" sampai benar-benar ≤3.3V baru disambung ke ESP32.

## 6. Atur frekuensi = RPM simulasi

```
frekuensi (Hz) = RPM_simulasi × pulses_per_revolution / 60
```

`pulses_per_revolution` ada di `store.setup` kamu (biasanya 1 untuk trigger
sekali per putaran). Contoh kalau nilainya 1:

| RPM simulasi | Frekuensi di tester |
|---|---|
| 800 (idle) | 13.3 Hz |
| 3000 | 50 Hz |
| 6000 | 100 Hz |
| 10000 | 167 Hz |

Atur "Frequency test" pada tester (kalau modelnya bisa menampilkan
frekuensi keluarannya sendiri secara simultan) untuk cross-check angka
yang benar-benar keluar, karena osilator murah kadang meleset dari angka
yang di-set di layar.

## 7. Jalankan & baca hasil

`idf.py -p PORT monitor`, tunggu ~1 detik, akan muncul baris seperti:

```
I (xxxx) cdi_selftest: n=50  rata2=142us  min=138us  max=147us  JITTER(spread)=9us
```

**Cara baca:**
- `rata2` (bias) — offset tetap dari latensi ISR alarm + `gpio_set_level()`.
  Wajar ada, biasanya puluhan-ratusan mikrodetik pada ESP32 native (bukan
  Arduino). Ini BUKAN masalah — ini konstan, bisa "dikalibrasi habis" lewat
  `trigger_angle_cdeg` saat setup timing dengan timing light nanti.
- `JITTER(spread)` — inilah angka yang sebenarnya kamu cari. Idealnya
  **sub-mikrodetik sampai beberapa mikrodetik saja** kalau arsitektur
  `cdi_timebase` bekerja seperti dirancang. Kalau angka ini naik jadi
  puluhan/ratusan mikrodetik ATAU ikut naik seiring RPM simulasi
  dinaikkan, itu tanda ada sumber jitter nyata yang perlu diselidiki
  (kandidat: BLE aktif & terhubung saat pengukuran — coba ulangi test
  tanpa HP terkoneksi BLE untuk membandingkan, sesuatu memblokir
  interrupt terlalu lama).
- Ulangi di beberapa titik RPM (idle, tengah, dekat redline) — jitter yang
  konsisten kecil di semua titik jauh lebih meyakinkan daripada satu
  angka saja.

## 8. Selesai uji

Hapus/comment baris `cdi_selftest_init();` di `main.c` (atau biarkan kalau
mau, asal kabel jumpernya dilepas) sebelum firmware dipasang permanen ke
motor — modul ini tidak berbahaya kalau tertinggal aktif tanpa kabel
jumper (GPIO5 idle, tidak ada interrupt yang trigger), tapi lebih rapi
dihapus untuk build produksi.

## Bonus: pakai fungsi lain di tester

- **Voltage test**: sambungkan ke pembagi tegangan VBAT (sebelum masuk
  ADC ESP32) dengan sumber DC yang diketahui (power supply bench atau
  baterai yang sudah diukur multimeter terpisah), lalu bandingkan dengan
  `battery_centivolts` yang dilaporkan lewat telemetry BLE / log serial.
  Ini memverifikasi konstanta skala `16000/4095` di kode charger benar
  untuk pembagi resistor board kamu -- bukan soal jitter, tapi sama
  pentingnya sebelum percaya angka VBAT yang dipakai untuk interlock
  keselamatan.
