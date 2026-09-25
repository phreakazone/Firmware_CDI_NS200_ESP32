# IgniTra CDI ESP32 R9 Modular

Firmware ESP-IDF untuk PCB IgniTra CDI berbasis ESP32-WROOM-32 DevKit 38-pin. Desain dasar menjalankan satu kanal pengapian CENTER; kanal SIDE, thermal/fan, OEM Learn, AUX, dan TPS diagnostic dipasang sebagai modul firmware opsional. Semua modul tersebut menggunakan bit konfigurasi independen dan dapat dipasang bersamaan; pilihan Core/Dual hanya menentukan kanal pengapian. Receiver Bluetooth audio adalah aksesori aplikasi terpisah dan tidak masuk bit firmware.

Firmware mempertahankan protokol aplikasi lama sambil menambahkan informasi hardware modular secara additive. Tidak ada UUID BLE, ukuran paket telemetri, atau perintah lama yang diubah.

## Status dan sumber kode aktif

Entry point build adalah `main/main.c`. Berkas aktif melalui `main/CMakeLists.txt`:

- `cdi_board_esp32.*`: pin, GPIO, ADC1, PWM charger dan akses hardware.
- `cdi_timebase.*`: penjadwalan pengapian berbasis GPTimer.
- `cdi_engine_esp32.*`: integrasi ISR pickup, CENTER/SIDE, charger, fan dan telemetri.
- `cdi_r5.*`: map, limiter dan keputusan pengapian.
- `cdi_r5_protocol.*`: protokol aplikasi versi 5.
- `cdi_r5_ble.*` dan `cdi_ble_nimble.*`: paket BLE versi 3 dan GATT.
- `cdi_r8_oem_learn.*`: perekaman referensi pengapian OEM.
- `cdi_r8_ota.*`: OTA.

`main/cdi_firmware.c` dan `main/cdi_firmware.h` adalah eksperimen portable lama dan **tidak ikut dikompilasi**. Developer tidak boleh memakai keduanya sebagai referensi aplikasi atau hardware.

Referensi aplikasi lengkap: [docs/APP_FIRMWARE_REFERENCE.md](docs/APP_FIRMWARE_REFERENCE.md).
Buku petunjuk produk: [docs/BUKU_PETUNJUK_PENGGUNA.md](docs/BUKU_PETUNJUK_PENGGUNA.md).

## Kontrak pin ESP32 dan skematik

Tabel ini mengikuti simbol U1 pada skematik `SCH_IGNITRA_CDI_ESP32_R9_TEST_2026-09-22(1).json`. Nomor U1 adalah nomor fisik header DevKit, bukan nomor GPIO.

| U1 | GPIO/power | NetLabel | Arah | Fungsi |
|---:|---|---|---|---|
| 1 | 3V3 | `3V3` | Power | Catu logika 3,3 V |
| 3 | GPIO36/ADC1_CH0 | `TPS_ADC` | Input | TPS utama |
| 4 | GPIO39/ADC1_CH3 | `TEMP_ADC` | Input | Suhu opsional |
| 5 | GPIO34/ADC1_CH6 | `TPS_REF_ADC` | Input | Referensi TPS opsional |
| 6 | GPIO35/ADC1_CH7 | `HV_C_ADC` | Input | Feedback HV CENTER |
| 7 | GPIO32/ADC1_CH4 | `HV_S_ADC` | Input | Feedback HV SIDE opsional |
| 8 | GPIO33/ADC1_CH5 | `VBAT_ADC` | Input | Tegangan aki |
| 9 | GPIO25 | `GATE_C` | Output | Gate SCR CENTER |
| 10 | GPIO26 | `GATE_S` | Output | Gate SCR SIDE opsional |
| 11 | GPIO27 | `STROBE` | Output | Strobe opsional |
| 12 | GPIO14 | `FAULT_N` | Input | Fault aktif-rendah |
| 14 | GND | `GND_LOGIC` | Power | Ground logika |
| 15 | GPIO13 | `FAN_CTL` | Output | Driver relay fan opsional |
| 19 | 5V | `ESP_5V` | Power | Masuk 5 V melalui JP1 |
| 20 | GND | `GND_LOGIC` | Power | Ground logika |
| 21 | GPIO23 | `AUDIO_PWM` | Output | Reserved; firmware menahan LOW |
| 26 | GND | `GND_LOGIC` | Power | Ground logika |
| 27 | GPIO19 | `PWM_B` | Output | Charger TC4427 kanal B |
| 28 | GPIO18 | `PWM_A` | Output | Charger TC4427 kanal A |
| 29 | GPIO5 | `BENCH_LOOP` | Input | Test pad loopback, bukan jumper permanen |
| 30 | GPIO17 | `OEM_SIDE` | Input | PC817 SIDE aktif-rendah |
| 31 | GPIO16 | `OEM_CENTER` | Input | PC817 CENTER aktif-rendah |
| 32 | GPIO4 | `PICKUP_DIG` | Input | Keluaran LM339 pickup |

Pin lain pada DevKit tidak dipakai. Khusus ground: skematik memakai U1 pin 14, 20, dan 26; jangan menambahkan pin ground lain.

Semua input analog memakai ADC1 karena ADC2 tidak dapat diandalkan ketika BLE aktif.

## Jalur daya dan harness utama J1

J1 memakai penomoran kolom kiri 1–6 dan kanan 7–12. Pada harness NS200 asli pin 1/8/9 tetap kosong sampai kabel tambahan dipasang.

| Pin J1 | Net/fungsi |
|---:|---|
| 1 | `KEYLESS_REQ`: pulsa +12 V terproteksi, minimum 1,5 s |
| 2 | `TPS_A` |
| 3 | `TEMP_SENSOR` |
| 4 | `TPS_B` |
| 5 | `IGN_12V` |
| 6 | `COIL_SIDE` |
| 7 | `FAN_RELAY` |
| 8 | `START_REQ`: dry-contact/open-collector ke GND_LOGIC |
| 9 | `MODE_REQ`; menjadi `NEUTRAL_IN` pada UNIVERSAL_MANUAL |
| 10 | `PICKUP_RAW` |
| 11 | `GND_STAR` |
| 12 | `COIL_CENTER` |

DKEY_WAKE pada J1.1 hanya membangunkan buck/ESP32. Charger HV tetap tidak mendapat jalur IGN normal sampai relay K1 aktif. GND_LOGIC dan GND_POWER hanya bertemu pada star point Core.

## Header modul

Semua konektor memakai pitch 2,54 mm. Core memakai male THT dan modul memakai female long-tail stackable. Header 2×6 memakai kolom kiri 1–6/kanan 7–12; header 2×3 memakai baris 1–2, 3–4, 5–6.

| Header | Pemetaan pin berurutan |
|---|---|
| JMOD_SIDE | 1 BRIDGE_PLUS; 2 SIDE_DET; 3 COIL_SIDE; 4 MOD_I2C_SDA; 5 GND_POWER; 6 MOD_I2C_SCL; 7 VIN_FILT; 8 GATE_S; 9 HV_S_FB; 10 GND_POWER; 11 3V3; 12 GND_LOGIC |
| JMOD_THERMAL | 1 VIN_PROT; 2 GND_POWER; 3 5V_LOGIC; 4 GND_LOGIC; 5 3V3; 6 TEMP_SENSOR; 7 TEMP_ADC; 8 FAN_CTL; 9 FAN_RELAY; 10 THERMAL_DET; 11 MOD_I2C_SDA; 12 MOD_I2C_SCL |
| JMOD_LEARN | 1 3V3; 2 GND_LOGIC; 3 OEM_CENTER; 4 OEM_SIDE; 5 GND_POWER; 6 LEARN_DET |
| JMOD_AUX | 1 VIN_PROT; 2 GND_POWER; 3 5V_LOGIC; 4 GND_LOGIC; 5 3V3; 6 AUX_DET; 7 STROBE; 8 AUDIO_PWM; 9 MOD_I2C_SDA; 10 MOD_I2C_SCL; 11 EXP_IO1; 12 EXP_IO2 |
| JMOD_TPS | 1 TPS_REF_RAW; 2 3V3; 3 GND_LOGIC; 4 TPS_REF_ADC; 5 TPS_DET; 6 TPS_SIG_RAW |
| JMOD_EXP | 1 3V3; 2 GND_LOGIC; 3 5V_LOGIC; 4 GND_POWER; 5 VIN_PROT; 6 EXP_IO1; 7 MOD_I2C_SDA; 8 MOD_I2C_SCL; 9 EXP_IO2; 10 EXP_IO3; 11 FAULT_N; 12 STROBE |

Setiap modul SIDE/THERMAL/LEARN/AUX/TPS menarik DET masing-masing ke GND_LOGIC melalui 1 kΩ. SIDE/HV tetap memerlukan slot dan keepout; pitch pad 2,54 mm bukan pengganti creepage.

## Perilaku paket dasar dan modul

- Paket dasar bekerja sebagai CENTER-only. Charger hanya mengawasi HV CENTER ketika SIDE tidak diaktifkan; `HV_S_ADC=0` tidak memicu fault imbalance palsu.
- SIDE baru ditembak ketika `side_enabled` aktif melalui READY THREE atau hasil OEM Learn yang valid.
- Input pickup dan OEM tidak memakai pull-down internal karena PCB sudah memiliki pull-up eksternal. PC817 OEM dibaca pada falling edge karena keluarannya aktif-rendah.
- Thermal AUTO memakai kalibrasi tiga titik. Jika kalibrasi/sensor tidak valid, output fan menyala sebagai fail-safe.
- TPS utama tetap berfungsi tanpa modul TPS diagnostic.
- AUDIO_PWM tetap reserved. GPIO23 dibuat output LOW agar tidak mengambang.

### Receiver Bluetooth audio eksternal

Suara mesin dibuat aplikasi Android dari telemetry RPM. Jalurnya terpisah:

    ESP32 CDI --BLE telemetry--> Android
    Android --Classic Bluetooth A2DP--> receiver audio --> amplifier/speaker

Receiver harus mendukung A2DP; modul BLE-only tidak menerima audio media.
Aplikasi menyimpan pilihan bahwa receiver dipasang sebagai AccessoryConfig
lokal per Serial CDI. Status paired/connected/rute aktif berasal dari Android,
bukan firmware atau GET,MODULES.

BLE CDI dan A2DP dapat aktif bersamaan. Untuk mengurangi gangguan, pisahkan
antena receiver dari ESP32/trafo/coil, gunakan decoupling catu lokal, dan
jangan mengambil arus amplifier dari pin 3V3 ESP32.

## Kompatibilitas aplikasi lama

Kontrak berikut tidak berubah:

- Advertising BLE: `NS200-CDI`.
- Service UUID: `7a8f1000-6c9d-4e40-a45f-0b4b4e533230`.
- Telemetry/Command/Response UUID lama tetap sama.
- Telemetri biner versi 3 tetap 20 byte dan bergantian CORE/DIAGNOSTIC.
- Frame tetap `@sequence,COMMAND,args*CRC16`.
- Empat slot map dan perintah `SETUP`, `LIVE`, `LIMIT`, `LOAD`, serta `SAVE` tetap tersedia.

Tambahan untuk aplikasi baru:

- `GET,INFO`
- `GET,VERSION` untuk release, semantic version, build ID dan versi protokol.
- `GET,IDENTITY` untuk serial unik ESP32 dan kebijakan binding aplikasi.
- `GET,HARDWARE` atau `GET,HW`
- `GET,ADC`
- `GET,MODULES` untuk konfigurasi, aktivitas, bukti sinyal dan fault modul.
- `GET,COMMISSION` untuk menentukan langkah setup berikutnya.
- `SETUP,INSTALL,CORE|DUAL,OEM_REMOVED` untuk ganti CDI OEM langsung.

`GET,TEMP` sekarang membaca status suhu/fan aktual yang sama dengan telemetri.

## Build dan flashing ESP-IDF

```bash
idf.py set-target esp32
idf.py build
idf.py -p COMx flash monitor
```

Jika `sdkconfig.defaults` diubah dan konfigurasi lama masih tersimpan, lakukan konfigurasi ulang dari ESP-IDF sebelum build. Folder `build/` bukan sumber kebenaran dan boleh dibuat ulang oleh ESP-IDF.

## Setup minimum

Setup pengguna terdiri dari **Pemasangan**, **Pemeriksaan**, dan **First
Start/Ready**. OEM Learn tidak diperlukan untuk pemasangan normal.

1. Lepas CDI OEM, pasang IgniTra, lalu pilih Core atau Dual memakai
   `SETUP,INSTALL,CORE|DUAL,OEM_REMOVED`.
2. Starter untuk membaca pickup; setelah RPM nol dan HV <30 V, simpan pickup.
3. Kalibrasi TDC nyata. Nilai UNIVERSAL bukan preset kendaraan.
4. Kalibrasi TPS CLOSED dan OPEN.
5. Jalankan FIRST START; firmware membatasi 3.000 RPM dan advance 10°.
6. Setelah mesin dimatikan, pilih READY CENTER.
7. Dual Coil memerlukan modul SIDE dan offset valid sebelum READY DUAL/THREE.
8. Kalibrasi suhu sebelum FAN AUTO.

Panduan bench test berada di [main/INSTRUKSI.md](main/INSTRUKSI.md). `BENCH_TEST_MODE` harus `0` dan self-test loopback harus dilepas untuk pemasangan kendaraan.

## Keselamatan

- Net `BRIDGE_PLUS`, `HV_CENTER`, `HV_SIDE`, `COIL_CENTER`, dan `COIL_SIDE` membawa tegangan/pulsa berbahaya.
- Jangan pernah memasukkan 12 V atau tegangan koil langsung ke GPIO ESP32.
- `FAN_RELAY` hanya mengendalikan kumparan relay; arus motor kipas tidak melewati GPIO atau transistor kecil modul.
- Pisahkan `GND_POWER` dan `GND_LOGIC` sesuai NT1/NT2 pada skematik; jangan mengganti net-tie dengan jumper pengguna.
- Pengujian kendaraan wajib dilakukan bertahap dengan pembatas arus dan pengukuran HV yang sesuai.

## Checklist developer berikutnya

Jika pin, fitur, atau paket aplikasi diubah, perbarui bersama-sama:

1. `main/cdi_board_esp32.h`.
2. Implementasi engine/protocol terkait.
3. Skematik EasyEDA dan pin header modul.
4. Tabel README ini.
5. `docs/APP_FIRMWARE_REFERENCE.md`.

Jangan menggunakan nama pin STM32 (`PA0`, `PB3`, dan sejenisnya) sebagai sumber hardware ESP32. Gunakan NetLabel skematik dan konstanta `CDI_PIN_*` native.


## Rev C module detection and timing presets (R9.3)

- GPIO21/GPIO22 operate PCF8574P U6 at 0x20.
- P0..P4 read SIDE/THERMAL/OEM_LEARN/AUX/TPS DET, active-low and debounced.
- Removing a module clears its runtime installed/active state and disables the related output.
- P5/P6 drive AUX keyless/starter through mandatory active-low PNP pre-drivers; power-up HIGH is OFF.
- Timing presets are STANDARD, SOFT, RESPONSIVE and KUDA. KUDA is restricted to the configured low-RPM/low-TPS window.
- Firmware source was updated only; no build artifact was generated.

See [hardware block/BOM guide](docs/HARDWARE_REV_C_BLOCKS_BOM.md) and [app/firmware reference](docs/APP_FIRMWARE_REFERENCE.md).

## Rev C Freeze 2 — contact/keyless/starter

- U6 PCF8574P alamat 0x20 menangani DET dan output AUX; U7 alamat 0x21 membaca KEYLESS_REQ, START_REQ, serta MODE_REQ/NEUTRAL_IN.
- `AUX,KEYLESS,ON` memberi izin pengapian dan mengaktifkan K1. `AUX,START,PULSE,100..3000` mengaktifkan K2 sementara; K2 lepas saat RPM ≥500 atau terjadi fault.
- `AUX,KEYLESS,OFF` dan `AUX,ALL,OFF` menjalankan CONTACT OFF: K2 OFF, spark/HV OFF, lalu K1 OFF.
- K2 juga dibatasi U8 NE555 one-shot: 2.42 s nominal dan sekitar 2.93 s worst-case, sehingga PCF/I²C yang macet LOW tidak dapat menahan starter terus-menerus.
- UNIVERSAL_MANUAL menolak starter tanpa NEUTRAL_IN. UNIVERSAL_MATIC tidak mewajibkan netral. Interlock OEM tetap harus dipertahankan.
- Source firmware saja yang diperbarui; belum dibuild dan belum dijadikan artefak produksi.
