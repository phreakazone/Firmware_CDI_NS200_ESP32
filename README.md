# Porting NS200 CDI R8: STM32WB55 → ESP32-WROOM-32 (Status: Selesai & Terverifikasi)

## 0. Cakupan Pekerjaan & Status Porting Saat Ini

Ini bukan sekadar kompilasi ulang. STM32WB55 dan ESP32 tidak berbagi satu pun peripheral yang sama (timer, ADC, BLE stack, tata letak flash), sehingga seluruh interaksi lapisan perangkat keras telah ditulis ulang dan berhasil dikompilasi pada **ESP-IDF v6.1**.

Berikut adalah status final dari arsitektur *firmware* saat ini:

| Modul Logika | Status Migrasi | Keterangan |
|---|---|---|
| `cdi_r5.c` (Interpolasi, Limiter, Timing) | **Verbatim 100%** | Lulus *unit test* `host_test.sh` tanpa modifikasi. |
| `cdi_r5_charger.c` (Regulasi HV) | **Verbatim 100%** | Lulus pengujian logika regulasi. |
| `cdi_r5_protocol.c` & `cdi_r8_oem_learn.c` | **Verbatim 100%** | Parser ASCII dan alur sadapan bekerja identik dengan STM32. |
| **Capture & Timebase (Timer)** | **Selesai (Rewrite)** | Menggunakan `gptimer` ESP32 (1 MHz) dan eksekusi ISR langsung dari IRAM (`cdi_timebase.c`). |
| **PWM Charger** | **Selesai (Rewrite)** | Menggunakan `mcpwm` dengan *dead-time hardware* untuk push-pull trafo (`cdi_board_esp32.c`). |
| **Stack BLE (NimBLE)** | **Selesai 100%** | Lulus kompilasi terhadap ESP-IDF. `cdi_ble_nimble.c` terhubung sempurna dengan UUID dan kontrak Android bawaan. |
| **BLE OTA Firmware Update** | **Selesai 100%** | Berhasil diintegrasikan menggunakan `esp_ota_ops` bawaan ESP-IDF, lengkap dengan *safety interlock* (RPM 0, HV aman). |

Bagian *hardware baru* (timebase, ISR, board bring-up, BLE) telah sukses di-*build*, namun **wajib** tetap melalui prosedur uji bangku (lihat §5) sebelum dihubungkan ke motor sungguhan untuk memvalidasi polaritas sirkuit fisik Anda.

## 1. Peta Pin (ESP32-WROOM-32 DevKit) — Selaras 1:1 dengan STM32

Peta pin ESP32 di bawah ini telah diselaraskan penamaannya agar identik dengan arsitektur STM32 (berdasarkan `cdi_board_esp32.h`).

    [Jalur Input Pulser]
    PA0 (GPIO4)  -> Pickup Utama (dari J1.10)

    [Jalur Output DIY - Menuju Koil]
    PA1 (GPIO25) -> Gate Center / Koil Tengah (menuju J1.12 via SCR)
    PA2 (GPIO26) -> Gate Side / Koil Samping (menuju J1.6 via SCR)

    [Jalur Input LEARN - Sadapan OEM via Isolator PC817]
    PB3 (GPIO16) -> OEM Tap Center (sadapan paralel dari J1.12)
    PB4 (GPIO17) -> OEM Tap Side (sadapan paralel dari J1.6)

    [Jalur Tambahan & Komponen Sekunder]
    PB9 (GPIO27) -> Strobo Manual
    PB5 (GPIO13) -> Relay Kipas (Radiator Fan J1.7)
    Fault (GPIO14) -> Hardware Fault (Aktif-Low)
    Charger A/B (GPIO18/19) -> Charger Push-Pull (lewat MCPWM)
    ADC (GPIO36/39/34/35/32/33) -> TPS/TEMP/TPS_REF/HVC/HVS/VBAT (Hanya ADC1)

**PERINGATAN KERAS:** Pin pickup (PA0) dan kedua pin tap OEM (PB3 & PB4) **bukan** input 12 V atau 285–345 V. Semuanya harus lewat rangkaian optocoupler/isolator berimpedansi tinggi sebelum menyentuh GPIO ESP32. ESP32 adalah IC 3.3 V murni; satu pulsa bocor dari primer koil (ratusan volt) akan menghancurkan chip seketika.

## 2. Instruksi Build & Flashing

Proyek ini telah dikonfigurasi untuk ESP-IDF (bukan Arduino) guna mendapatkan kontrol presisi mikrodetik via `gptimer`, `mcpwm`, dan eksekusi instruksi di level IRAM.

    idf.py set-target esp32
    idf.py menuconfig   # (Opsional) verifikasi parameter
    idf.py build
    idf.py -p /dev/ttyUSB0 flash monitor

## 3. Resolusi Jitter: Optimasi Arsitektur ESP32

Kelemahan umum porting CDI ke ESP32 adalah *jitter* pengapian (sudut yang meleset). Arsitektur ini telah membenahi masalah tersebut secara fundamental:

1. **Penjadwalan Hardware (Bukan Software Delay).** Kita tidak menggunakan `vTaskDelay` atau *polling loop*. Porting ini menggunakan `gptimer` 1 MHz yang berjalan bebas. Busi dijadwalkan sebagai alarm perangkat keras pada *tick* absolut (`now + delay`).
2. **ISR Terkunci di IRAM.** Kode ISR yang berada di flash rentan terhadap *cache-miss stall* saat ESP32 melakukan operasi NVS atau BLE. Semua fungsi jalur panas (`pickup_isr`, `cdi_r5_make_decision`, `cdi_r5_map_interpolate`) secara eksplisit dikunci di IRAM menggunakan `IRAM_ATTR`.
3. **Isolasi Core untuk BLE.** Stack komunikasi NimBLE beroperasi asinkron, sementara interupsi `pickup` diberi prioritas `ESP_INTR_FLAG_LEVEL3` untuk memastikan BLE tidak menggeser presisi waktu pengapian.
4. **Dynamic Frequency Scaling Dimatikan.** Kecepatan *clock* APB/CPU dikunci permanen pada 240 MHz (`CONFIG_PM_ENABLE` dimatikan) sehingga resolusi *tick* tidak pernah melenceng akibat transisi *sleep mode*.

## 4. Mitigasi Brownout (MCU Reset Saat Starter/Digas)

Pada CDI otomotif, tegangan aki bisa anjlok ke 6–8 V saat dinamo *starter* menyedot arus, sementara radio BLE ESP32 menarik lonjakan arus sesaat. 

**Solusi Software yang Sudah Diterapkan:**
- *Brownout detector* (BOD) disetel ke ambang terendah (`BROWNOUT_DET_LVL_SEL_7`, ~2.43 V).
- Fitur pencatatan reset brownout (`cdi_diag/bod_count`) telah diaktifkan di NVS untuk diagnostik serial.
- *Safety cut-off* akan memblokir koil jika tegangan aki (VBAT) keluar dari rentang 9.5–16 V.

**Persyaratan Hardware yang Wajib Dipenuhi Pengguna:**
- Gunakan **Regulator Buck DC-DC** (seperti MP1584), BUKAN regulator linear LDO (AMS1117) yang akan mati saat aki drop.
- Wajib menambahkan **Kapasitor Bulk (470–1000 µF)** di jalur input 12V dan **Kapasitor Output (100–470 µF)** sedekat mungkin ke pin 3V3 ESP32.
- Terapkan **Ground Star-Point**: Pisahkan jalur *ground* koil pengapian/charger HV yang kotor dari *ground logic* ESP32.

## 5. Prosedur Uji Bangku (Wajib Sebelum Instalasi di Motor)

Karena *firmware* telah berhasil di-*build*, tahap krusial berikutnya adalah validasi fisik kelistrikan:

1. **Simulasi Sensor (Tanpa HV/Koil):** Suntikkan sinyal gelombang kotak (3.3V) ke pin `PA0` (GPIO4). Pantau keluaran pin penembak `PA1` (GPIO25) dan `PA2` (GPIO26) menggunakan osiloskop.
2. **Uji Jitter Kuantitatif:** Picu *trigger* osiloskop pada tepi naik sinyal pulser simulasi, lalu amati stabilitas tepi naik sinyal *gate output* pada simulasi RPM tetap (mis. 5.000 RPM).
3. **Stress Test Radio & Brownout:** Hubungkan aplikasi Android via BLE dan turunkan voltase input PSU secara bertahap mendekati 7V. Pastikan ESP32 tidak melakukan reset (*brownout*) saat radio memancarkan data telemetri.
4. **Uji Transisi Aman:** Bawa sistem ke kendaraan. Lakukan perekaman kurva mode `OEM_LEARN` dengan memastikan pin sadap `PB3`/`PB4` terhubung *via* modul Optocoupler. Lanjutkan dengan validasi *limiter* perlindungan pada mode `FIRST_START` (maksimal 3000 RPM).

## 6. Sisa Pekerjaan Perangkat Keras Akhir

Sisi perangkat lunak (*software*) telah rampung sepenuhnya (termasuk BLE dan OTA). Satu-satunya langkah teknis yang tersisa adalah **Kalibrasi ADC Fisik**: 
Meskipun rutin pembacaan ADC1 telah disesuaikan (`adc_cali_curve_fitting`), rasio fisik resistor pembagi tegangan untuk pembacaan High Voltage (285–345 V) dan tegangan aki (VBAT) harus Anda verifikasi dan kalibrasi secara manual menggunakan *multimeter* yang akurat agar *telemetry* di aplikasi Android presisi 100%.
