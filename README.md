# Porting NS200 CDI R9: STM32WB55 → ESP32-WROOM-32

**Status saat ini: Build terverifikasi sukses (ESP-IDF v6.1), Uji Bangku (GM328A Simulator) Berhasil.**

> Catatan jujur soal status: versi README sebelumnya menyatakan proyek ini
> "Selesai & Terverifikasi" termasuk BLE "Lulus kompilasi... Selesai 100%".
> Setelah diaudit ulang dan diuji langsung menggunakan simulator perangkat keras,
> ditemukan beberapa *bug* fatal (Stack Overflow, Memory Leak, dan ISR Flooding) 
> yang memicu *Crash/Panic*. Semua isu tersebut **telah diperbaiki** pada revisi 
> saat ini. Komunikasi GATT dua arah dengan Android telah diverifikasi berjalan 
> *real-time* tanpa *lag*. Lihat §7 (Log Verifikasi) untuk riwayat lengkapnya.

## 0. Cakupan Pekerjaan & Status Migrasi

STM32WB55 dan ESP32 tidak berbagi satu pun peripheral yang sama (timer, ADC,
BLE stack, tata letak flash), sehingga seluruh lapisan perangkat keras
ditulis ulang. Status per modul:

| Modul | Status | Bukti |
|---|---|---|
| `cdi_r5.c` (interpolasi, limiter, timing) | **Verbatim, hanya `IRAM_ATTR` ditambahkan** | Diff langsung ke sumber STM32: cuma 2 baris atribut berubah. |
| `cdi_engine_esp32.c` (Inti Mesin & Telemetri) | **Rewrite & Diperbaiki, Terverifikasi** | Telah ditambahkan *Hardware Debounce Filter* untuk mencegah *crash* akibat *noise* PWM eksternal. Laju telemetri disetel ke 20Hz (`50u`) untuk mencegah *Bluetooth congestion* di aplikasi Android. Telah dilengkapi fitur `BENCH_TEST_MODE`. |
| **Capture & Timebase** (`cdi_timebase.c`) | **Rewrite, compile sukses** | Satu `gptimer` bersama untuk seluruh sistem. Jitter telah diukur secara internal. |
| **PWM Charger** (`cdi_board_esp32.c`) | **Rewrite, compile sukses** | `mcpwm` dengan dead-time hardware. Belum diverifikasi dengan osiloskop di keluaran fisik. |
| **Stack BLE (NimBLE)** (`cdi_ble_nimble.c`) | **Compile & Terverifikasi dengan Android** | Konektivitas GATT dua arah terbukti stabil. Bug *Out of Memory* (Mbuf) dan hilangnya fungsi `notify_telemetry` telah ditambal. |
| **BLE OTA** (`esp_ota_ops`) | Kode terpasang, compile sukses | Belum pernah dicoba kirim data OTA sungguhan. Uji dengan data dummy dulu. |
| **Brownout detector** | **Bug ditemukan & diperbaiki** | Versi sebelumnya salah level, sekarang sudah benar (Level 0) dan terverifikasi lewat isi `sdkconfig`. |
| **Self-test jitter** (`cdi_selftest.c`) | **Aktif & terintegrasi penuh** | Sudah dipakai di atas meja, melaporkan *jitter* yang stabil saat diuji dengan simulator frekuensi. |

## 1. Peta Pin Lengkap (ESP32-WROOM-32 DevKit)

Sistem ini membagi pin menjadi beberapa kategori fungsional. Pastikan pengkabelan di PCB/Hardware Anda sesuai dengan tabel di bawah ini:

### A. Sinyal Input & Sensor Digital
| Fungsi | Pin ESP32 | Pin STM32 Asli | Keterangan |
|---|---|---|---|
| **Pickup Utama** | **GPIO4** (D4) | PA0 | Sensor Pulser. (Batas aman 3.3V, wajib lewat rangkaian *conditioner*) |
| **OEM Tap Center** | **GPIO16** (RX2) | PB3 | Sadapan Koil Tengah untuk Mode OEM Learn (Wajib via Optocoupler) |
| **OEM Tap Side** | **GPIO17** (TX2) | PB4 | Sadapan Koil Samping untuk Mode OEM Learn (Wajib via Optocoupler) |
| **Hardware Fault** | **GPIO14** (D14) | - | Input pengaman Aktif-Low (Biasanya terhubung ke sirkuit HV *overvoltage*) |

### B. Keluaran Daya & Pengapian (Output)
| Fungsi | Pin ESP32 | Pin STM32 Asli | Keterangan |
|---|---|---|---|
| **Gate Center** | **GPIO25** (D25) | PA1 | Pemicu SCR/IGBT Koil Tengah |
| **Gate Side** | **GPIO26** (D26) | PA2 | Pemicu SCR/IGBT Koil Samping |
| **Charger A** | **GPIO18** (D18) | - | Output PWM Push-Pull A (Untuk trafo inverter DC-DC CDI) |
| **Charger B** | **GPIO19** (D19) | - | Output PWM Push-Pull B (Komplemen dengan *Dead-time*) |
| **Fan Relay** | **GPIO13** (D13) | PB5 | Output kontrol Relay Kipas Radiator (Aktif jika suhu mesin panas) |
| **Strobo** | **GPIO27** (D27) | PB9 | Output Lampu Strobe (Untuk *Timing Light* manual) |

### C. Sensor Analog (Wajib di ADC1)
*Catatan: Semua sensor analog menggunakan modul ADC1 ESP32 agar dapat bekerja bersamaan dengan transmisi Bluetooth secara stabil.*

| Fungsi | Pin ESP32 | Channel | Keterangan |
|---|---|---|---|
| **TPS Input** | **GPIO36** (VP) | ADC1_CH0 | Sensor Posisi Gas (Throttle Position Sensor) |
| **Temp Input** | **GPIO39** (VN) | ADC1_CH3 | Sensor Suhu NTC (*Engine Temperature*) |
| **TPS Reference** | **GPIO34** (D34) | ADC1_CH6 | Tegangan Referensi TPS |
| **HV Center** | **GPIO35** (D35) | ADC1_CH7 | Pantauan Tegangan Tinggi (HV) Kapasitor Tengah |
| **HV Side** | **GPIO32** (D32) | ADC1_CH4 | Pantauan Tegangan Tinggi (HV) Kapasitor Samping |
| **VBAT Input** | **GPIO33** (D33) | ADC1_CH5 | Pantauan Tegangan Aki/Baterai 12V |

### D. Pin Khusus Uji Meja (Bench Test)
| Fungsi | Pin ESP32 | Keterangan |
|---|---|---|
| **Loopback Test** | **GPIO5** (D5) | Hanya dipakai saat *Bench Test*. Dijumper langsung ke **GPIO25** untuk mengukur Jitter MCU tanpa Osiloskop. **(Lepas saat dipasang ke motor!)** |

**⚠️ PERINGATAN KERAS KEAMANAN HARDWARE:**
ESP32 adalah IC dengan toleransi logika murni **3.3V**. Jalur Pickup, OEM Tap, maupun input ADC **tidak boleh** disuapkan tegangan 12V dari aki atau ratusan volt dari koil secara langsung. Semuanya harus melewati sirkuit *optocoupler* (isolator) atau resistor pembagi tegangan (*voltage divider*) yang dihitung secara presisi. Kesalahan pada batas 3.3V akan langsung menyebabkan *chip* ESP32 terbakar (*magic smoke*).

**PERINGATAN KERAS:** ESP32 adalah IC 3.3V murni. Sinyal dari generator PWM eksternal (seperti GM328A) seringkali bertegangan 5V. **Selalu ukur amplitudo sebelum menyambung ke GPIO4.** Gunakan resistor pembagi tegangan (misal 1kΩ/1kΩ) untuk menurunkan tegangan 5V menjadi aman.

## 2. Instruksi Build, Flashing & Wajib Menuconfig

Diverifikasi jalan di **ESP-IDF v6.1**, target ESP32 klasik (WROOM-32).

Sangat krusial untuk **memperbesar memori (Stack Size) NimBLE** sebelum melakukan *build*, karena aplikasi Android mengirimkan perintah penulisan yang berat ke memori Flash ESP32, yang dapat memicu *Stack Overflow* (Crash/Panic).

    idf.py set-target esp32
    idf.py menuconfig   # WAJIB: Ke Bluetooth -> NimBLE Options -> Set "NimBLE Host task stack size" menjadi 8192
    idf.py build
    idf.py -p COMx flash monitor

## 3. Arsitektur Anti-Jitter

1. **Penjadwalan hardware, bukan software delay.** Satu `gptimer` 1 MHz berjalan bebas untuk seluruh sistem (`cdi_timebase.c`).
2. **ISR dikunci cache-safe.** `CONFIG_GPTIMER_ISR_CACHE_SAFE` dan `CONFIG_MCPWM_ISR_CACHE_SAFE` melindungi ISR pengapian.
3. **Isolasi prioritas interrupt.** Level 3 (`ESP_INTR_FLAG_LEVEL3`).
4. **Filter Noise Ekstrim (Debounce).** Input pulser difilter di dalam `pickup_isr` untuk mengabaikan sinyal liar (di bawah 2000us) yang sering terjadi pada kabel uji meja agar CPU tidak kebanjiran *interrupt*.
5. **Dynamic Frequency Scaling dimatikan total** (`CONFIG_PM_ENABLE=n`).

## 4. Prosedur Uji Bangku Menggunakan GM328A (Bench Test)

Untuk menguji logika pengapian dengan simulator PWM (seperti LCR GM328A), Anda harus "membodohi" *firmware* agar mengizinkan pengapian tanpa CDI pabrikan dan tanpa Aki 12V.

1. **Aktifkan Mode Uji:** Buka `cdi_engine_esp32.c`, pastikan `#define BENCH_TEST_MODE 1`. Ini akan mem-*bypass* syarat tegangan HV dan Aki.
2. **Wiring Eksternal:** Hubungkan **GND GM328A** ke **GND ESP32**. Hubungkan sinyal **Square wave PWM** ke **GPIO4** (Gunakan pembagi tegangan jika > 3.3V). Pasang jumper dari **GPIO25** ke **GPIO5** untuk deteksi *Jitter*.
3. **Bypass Mode OEM Learn:** Sistem tidak bisa menggunakan mode *OEM Learn* di atas meja hanya dengan satu sinyal. Buka Aplikasi Android, masuk ke pengaturan, isi **Manual TDC / Offset** (misal: 60), lalu simpan.
4. **Ubah ke Mode DIY:** Setelah TDC tersimpan (kalibrasi terpenuhi), ubah mode ke **DIY Mode** di aplikasi Android. Pesan "Menunggu pulsa..." di terminal akan hilang dan digantikan oleh laporan `JITTER`.

**SETELAH UJI BANGKU SELESAI:** Jangan lupa mengembalikan `#define BENCH_TEST_MODE 0` dan melepas komentar `cdi_selftest_init()` di `main.c` sebelum melakukan *Flash* untuk pemasangan permanen di motor.

## 5. Mitigasi Brownout
*Telah diperbaiki ke level 0 (2.43V - Paling toleran) di `sdkconfig`*. Wajib menggunakan Buck Converter DC-DC (MP1584), Kapasitor Bulk (470-1000uF), dan pemisahan jalur Ground sirkuit tegangan tinggi.

## 6. Sisa Pekerjaan
- **Kalibrasi ADC fisik**: rasio resistor pembagi untuk HV (285–345V) dan VBAT harus diverifikasi manual pakai multimeter.
- **Uji charger HV dengan osiloskop**.
- **Merge branch `codex/r9-universal-firmware` ke `main`**.

## 7. Log Verifikasi (Update Terakhir)

- **[FIX] Stack Overflow Bluetooth:** Memperbesar stack `nimble_host` ke 8192 sukses menghentikan *crash* (Sebab Reset 4 / Panic) saat berpindah mode via Android.
- **[FIX] Android Lag / Menggantung:** Mengurangi frekuensi transmisi notifikasi telemetri menjadi 20Hz (`telemetry_divider = 50u`) terbukti menyinkronkan data *real-time* ke HP tanpa membuat antrean GATT menumpuk.
- **[FIX] Fatal Memory Leak BLE:** Menambahkan pengecekan `om != NULL` pada `ble_hs_mbuf_from_flat` untuk mencegah sistem *crash* akibat kelaparan RAM (*Out of Memory*) saat tumpukan BLE penuh.
- **[FIX] Fungsi Hilang:** Mengembalikan fungsi `cdi_ble_notify_telemetry` yang tidak tertulis pada porting awal.
- **[FIX] ISR Noise Flood:** Menambahkan filter *debounce* `< 2000us` di `pickup_isr` mencegah MCU mereset dirinya (Watchdog Timeout) saat dihubungkan ke sumber sinyal murah (GM328A) yang memiliki *ringing* elektromagnetik tinggi.
- Nilai `sdkconfig` hasil generate dicek langsung (`findstr`) dan dikonfirmasi `CONFIG_ESP_BROWNOUT_DET_LVL_SEL_0=y` aktif.
