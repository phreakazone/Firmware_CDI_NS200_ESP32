# Panduan Lengkap Bench Test CDI ESP32 (Menggunakan GM328A & Self-Test Loopback)

Panduan ini ditujukan untuk menguji logika pengapian (Timing Advance, Map 32x16, Telemetri Android, dan Jitter) menggunakan ESP32 secara mandiri di atas meja, tanpa menyalakan mesin motor sungguhan.

---

## 1. Persiapan Firmware

Sebelum memulai uji meja, ada pengaturan yang harus disiapkan:

**A. Mengaktifkan Bypass Keamanan**
Di dalam file `cdi_engine_esp32.c`, cari baris kode ini di bagian atas:
`#define BENCH_TEST_MODE 1`

Pastikan nilainya `1`. Fitur ini memanipulasi sensor agar ESP32 menganggap aki dan kapasitor HV aman, sehingga CDI mau bekerja meski hanya ditenagai USB.

**B. Memperbesar Memori Bluetooth**
Agar ESP32 tidak *crash* saat sinkronisasi Android:
1. Jalankan perintah `idf.py menuconfig`.
2. Masuk ke **Component config** -> **Bluetooth** -> **NimBLE Options**.
3. Ubah `NimBLE Host task stack size` menjadi **8192**.
4. Simpan (Save) dan Keluar (Quit).

**C. Mengaktifkan Modul Self-Test**
Pastikan file `main.c` Anda memanggil fungsi ini:
`cdi_selftest_init(); // <-- AKTIFKAN UNTUK UJI MEJA`

---

## 2. Pengkabelan (Wiring)

Kita menggunakan alat *LCR Component Tester / GM328A* dan kabel *jumper* fisik.

| Dari (Alat / Pin) | Menuju Pin ESP32 | Keterangan |
|---|---|---|
| **GND** (GM328A) | **GND** (ESP32) | Wajib agar sinyal punya referensi 0V yang sama. |
| **PWM** (GM328A) | **GPIO4** (Pickup) | Input Pulser. **(BACA PERINGATAN TEGANGAN)** |
| **GPIO25** (Center) | **GPIO5** (Loopback)| Kabel jumper langsung untuk mengukur *jitter* koil. |

> **⚠️ PERINGATAN TEGANGAN:**
> Gunakan "Voltage test" di alat GM328A untuk mengukur sinyal PWM-nya sendiri. Jika di atas 3.3V, wajib pasang resistor pembagi tegangan (misal 1kΩ & 1kΩ) secara seri ke GPIO4 agar ESP32 tidak terbakar.

---

## 3. Simulasi & Pembacaan Jitter

Setelah di-flash (`idf.py app-flash`), buka aplikasi Android dan koneksikan Bluetooth. Di GM328A, pilih menu **f-Generator**.

Konversi frekuensi jika PPR = 1:
* **13.3 Hz** = ~800 RPM (Idle)
* **25 Hz** = 1500 RPM
* **50 Hz** = 3000 RPM
* **100 Hz** = 6000 RPM

Buka terminal PC (`idf.py monitor`). Laporan *real-time* akan muncul:
`I (xxxx) cdi_selftest: n=50 rata2=142us min=138us max=147us JITTER(spread)=9us`

* **rata2 (Bias):** Latensi mikrokontroler (offset konstan). Ini wajar dan bisa dihilangkan saat kalibrasi *Timing Light* nanti.
* **JITTER(spread):** Stabilitas sistem. Jika angkanya kecil (di bawah 15us), berarti sistem pengapian sangat akurat.

---

## 4. Persiapan Pasang ke Motor

Jika pengujian selesai, **LAKUKAN 4 HAL INI** sebelum dipasang ke motor sungguhan:

1. **LEPAS** semua kabel *tester* dan *jumper* (GPIO25 ke GPIO5).
2. Di `cdi_engine_esp32.c`, ubah menjadi `#define BENCH_TEST_MODE 0`.
3. Di `main.c`, beri komentar pada fungsinya: `// cdi_selftest_init();`
4. Lakukan *build* dan *flash* ulang ke ESP32.