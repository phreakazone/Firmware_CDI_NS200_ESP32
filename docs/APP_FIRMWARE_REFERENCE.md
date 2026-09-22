# Referensi Firmware untuk Developer Aplikasi

Dokumen ini adalah kontrak integrasi aplikasi untuk firmware ESP32 IgniTra CDI R9 Modular. Implementasi yang aktif berada pada `main/cdi_engine_esp32.c`, `main/cdi_r5_protocol.c`, `main/cdi_r5_ble.c`, dan `main/cdi_ble_nimble.c`.

## Identitas dan kompatibilitas

| Item | Nilai |
|---|---|
| Platform | ESP32 klasik/WROOM-32 |
| Generasi firmware | R9 |
| Protokol perintah | 5 |
| Paket BLE | versi 3, 20 byte |
| Nama advertising lama | `NS200-CDI` |
| Jumlah map | 4 |
| Grid maksimum | 32 RPM × 16 TPS |
| RPM format maksimum | 30.000 |
| Advance format | -30,0° sampai +80,0° |

Kompatibilitas lama sengaja dipertahankan:

- UUID BLE tidak berubah.
- Nama advertising tidak berubah.
- Paket telemetri tetap versi 3 dan panjangnya tetap 20 byte.
- Format frame ASCII dan CRC16 tidak berubah.
- Perintah lama tidak dihapus atau diganti nama.
- Schema NVS aktif tetap `CDI_R5_STORE_VERSION = 5`.
- `GET,INFO`, `GET,HARDWARE`/`GET,HW`, dan `GET,ADC` hanya tambahan.

Saat koneksi baru, aplikasi disarankan mengirim `PING`, kemudian `GET,INFO`, `GET,CAPS`, `GET,SETUP`, dan `GET,META`. Jika firmware lama menjawab `ERR,GET` untuk query tambahan, aplikasi harus melanjutkan memakai kemampuan lama.

## BLE GATT

| Fungsi | UUID | Properti |
|---|---|---|
| Service | `7a8f1000-6c9d-4e40-a45f-0b4b4e533230` | Primary service |
| Telemetry | `7a8f1001-6c9d-4e40-a45f-0b4b4e533230` | Notify |
| Command | `7a8f1002-6c9d-4e40-a45f-0b4b4e533230` | Write |
| Response | `7a8f1003-6c9d-4e40-a45f-0b4b4e533230` | Notify |
| OTA data | `7a8f1004-6c9d-4e40-a45f-0b4b4e533230` | Write, Write Without Response |
| OTA status | `7a8f1005-6c9d-4e40-a45f-0b4b4e533230` | Notify |

Semua integer multibyte pada paket biner memakai little-endian.

### Paket telemetri 20 byte

Header bersama:

| Offset | Ukuran | Isi |
|---:|---:|---|
| 0 | 2 | Magic `0xCD15` |
| 2 | 1 | Versi `3` |
| 3 | 1 | Kind: `0=CORE`, `1=DIAGNOSTIC` |
| 4 | 2 | Sequence |
| 18 | 2 | CRC16-CCITT atas byte 0–17 |

Kind CORE:

| Offset | Tipe | Isi/satuan |
|---:|---|---|
| 6 | `uint16` | RPM |
| 8 | `uint16` | TPS, 0–1000 permille |
| 10 | `int16` | Advance, centidegree |
| 12 | `uint16` | Aki, centivolt |
| 14 | `uint16` | HV CENTER, volt |
| 16 | `uint16` | HV SIDE, volt; nol saat modul SIDE tidak dipakai |

Kind DIAGNOSTIC:

| Offset | Tipe | Isi |
|---:|---|---|
| 6 | `int16` | Suhu centidegree; `INT16_MIN` berarti tidak valid |
| 8 | `uint8` | Slot map aktif, 0–3 |
| 9 | `uint8` | Limiter: 0 fire, 1 soft cut, 2 hard cut |
| 10 | `uint8` | Flags utama |
| 11 | `uint8` | Flags output |
| 12 | `uint16` | Fault bits |
| 14 | `uint16` | Trigger angle, centidegree |
| 16 | `uint8` | Pickup quality, 0–100 |
| 17 | `uint8` | Durasi First Start, detik |

Flags utama: bit 0 output diizinkan, bit 1 PRO, bit 2 HV aktif, bit 3 terkalibrasi, bit 4 BLE terhubung, bit 5 READY, bit 6 FIRST_START.

Flags output: bit 0 CENTER, bit 1 SIDE, bit 2 strobe, bit 3 fan.

Fault bits: bit 0 hardware clamp, bit 1 aki, bit 2 HV overvoltage, bit 3 HV imbalance, bit 4 pickup timeout, bit 5 kalibrasi, bit 6 CRC map.

## Frame perintah ASCII

Format:

```text
@sequence,COMMAND,arg1,arg2*CRC16\n
```

CRC menggunakan CRC16-CCITT polynomial `0x1021`, initial value `0xFFFF`, dihitung atas teks mulai dari `sequence` sampai argumen terakhir, tanpa `@`, `*CRC`, CR, atau LF.

Respons berhasil berbentuk `@sequence,ACK,...*CRC16`. Respons gagal berbentuk `@sequence,ERR,NAMA*CRC16`.

## Query utama

| Query | Respons utama |
|---|---|
| `PING` | `ACK,PONG_R9` |
| `GET,INFO` | `INFO,ESP32,R9,5,3,IGNITRA_R9_MODULAR` |
| `GET,HARDWARE` atau `GET,HW` | Profil PCB dasar dan daftar modul opsional |
| `GET,CAPS` | Batas format dan fitur firmware |
| `GET,STATUS` | RPM, TPS, HV C/S, slot, mode map, permission, PRO |
| `GET,META` | Metadata map aktif |
| `GET,SETUP` | Tahap setup dan konfigurasi pengapian |
| `GET,PROFILE` | Profil universal, rentang RPM/advance, PPR, trigger |
| `GET,TEMP` | Mode fan, ambang, suhu valid, output fan |
| `GET,ADC` | Raw TPS, suhu, TPS reference, HV C/S, VBAT, fault, fan |
| `GET,MODE` | Mode operasi dan konfirmasi OEM unplugged |
| `GET,LEARN` | Progres OEM Learn |
| `GET,OTA` | Status OTA |
| `GET,CELL,tpsIndex,rpmIndex` | Nilai sel map dalam centidegree |

Urutan `GET,ADC`:

```text
ADC,tpsRaw,tempRaw,tpsRefRaw,hvCenterVolt,hvSideVolt,vbatRaw,faultActive,fanOutput
```

## Perintah konfigurasi

Perintah yang menulis konfigurasi umumnya mensyaratkan mesin berhenti dan HV CENTER/SIDE di bawah 30 V.

- `SETUP,PICKUP,CONFIRM`
- `SETUP,EDGE,FALLING|RISING`
- `SETUP,PPR,1..12`
- `SETUP,GATE_US,40..150`
- `SETUP,STROBE,ON|OFF`
- `SETUP,OFFSET,centidegree`
- `SETUP,SAVE_TDC`
- `SETUP,MANUAL_TDC,centidegree,CONFIRM`
- `SETUP,TPS,CLOSED|OPEN`
- `SETUP,FIRST_START`
- `SETUP,READY,CENTER`
- `SETUP,READY,THREE,sideOffsetCentidegree`
- `SETUP,FAN,OFF|ON|AUTO` — kompatibilitas lama.
- `MODE,MANUAL`
- `MODE,OEM_LEARN`
- `MODE,DIY,OEM_UNPLUGGED`
- `LEARN,START|STOP|ABORT`
- `FEATURE,PRO,ON|OFF`
- `LIMIT,SOFT|HARD,rpm,softBandRpm` — format lama.
- `LOAD,slot` dan `SAVE,slot` — format lama.
- `LIVE,tpsIndex,rpmIndex,advanceCentidegree` — pembaruan sel lama.

Perintah R9 tambahan:

- `MAP,BEGIN,rpmCount,tpsCount`
- `MAP,RPM,index,rpm`
- `MAP,LOAD,index,loadPercent`
- `MAP,CELL,rpmIndex,tpsIndex,advanceX10Degree`
- `MAP,SAVE,slot`
- `MAP,SELECT,slot`
- `SET,LIMIT,rpm`
- `SET,FAN,OFF|ON|AUTO,onX10Degree,offX10Degree`
- `SET,PROFILE,name,rpmMin,rpmMax,advanceMinX10,advanceMaxX10,ppr,triggerX10`
- `TEMP,CAL,adc0,temp0X10,adc1,temp1X10,adc2,temp2X10`
- `DYNO,BEGIN`
- `DYNO,TRIM,trimX10Degree`
- `DYNO,COMMIT|ABORT`

## Kontrak hardware modular yang terlihat oleh aplikasi

- PCB dasar selalu menyediakan satu kanal CENTER.
- Kanal SIDE bersifat opsional. Jika SIDE tidak aktif, nilai HV SIDE boleh nol dan bukan fault imbalance.
- Thermal/fan opsional. `GET,TEMP` menandai validitas suhu; AUTO menyalakan fan secara fail-safe ketika kalibrasi/sensor tidak valid.
- OEM Learn opsional dan memakai input PC817 aktif-rendah.
- TPS diagnostic opsional hanya memantau `TPS_REF_ADC`; jalur TPS utama tetap berada pada PCB dasar.
- `AUDIO_PWM` berada di GPIO23 tetapi masih berstatus reserved dan dipaksa LOW. Aplikasi jangan menampilkan kontrol audio sebagai fitur aktif sampai capability baru ditambahkan.

## Aturan perubahan berikutnya

1. Jangan mengubah UUID, paket v3, arti bit, atau perintah lama secara diam-diam.
2. Tambahkan query/perintah baru secara additive dan sertakan capability.
3. Jika format paket harus berubah, gunakan nomor versi baru dan tetap terima versi lama selama masa migrasi.
4. Jangan memakai ADC2 selama BLE aktif.
5. Setiap perubahan pin harus sekaligus memperbarui `cdi_board_esp32.h`, README, skematik EasyEDA, dan dokumen ini.
6. Jangan mengaktifkan SIDE hanya berdasarkan model motor; gunakan hasil setup atau OEM Learn yang valid.
