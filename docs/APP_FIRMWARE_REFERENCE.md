# Referensi Firmware untuk Developer Aplikasi

Dokumen ini adalah kontrak integrasi aplikasi untuk **IgniTra CDI ESP32 R9
Modular**. Sumber aktif berada pada `main/cdi_engine_esp32.c`,
`main/cdi_r5_protocol.c`, `main/cdi_r5_ble.c`, dan `main/cdi_ble_nimble.c`.
`main/cdi_firmware.*` tidak ikut dikompilasi dan bukan referensi implementasi.

## 1. Prinsip integrasi

1. CORE selalu tersedia dan berarti satu kanal pengapian pada
   `J1.12/COIL_CENTER`.
2. Kanal kedua berasal dari modul SIDE dan keluar melalui
   `J1.6/COIL_SIDE`.
3. Firmware tidak memiliki pin identifikasi modul. Status **terpasang** adalah
   konfigurasi pilihan pengguna, bukan deteksi fisik otomatis.
4. Aplikasi wajib membedakan `configured`, `active`, `observed`, dan `fault`.
5. Setup normal adalah **Ganti CDI OEM**, bukan OEM Learn.
6. UUID, paket BLE v3 20 byte, dan perintah lama tetap dipertahankan.

## 2. Identitas dan kompatibilitas

| Item | Nilai |
|---|---|
| Platform | ESP32 klasik/WROOM-32 |
| Generasi | R9 |
| Protokol perintah | 5 |
| Paket BLE | Versi 3, 20 byte |
| Advertising lama | `NS200-CDI` |
| Map | 4 slot |
| Grid maksimum | 32 RPM × 16 TPS |
| RPM format maksimum | 30.000 |
| Advance format | -30,0° sampai +80,0° |
| Schema NVS | 5, ukuran blob tidak berubah |

Tambahan ini bersifat additive: `GET,MODULES`, `GET,COMMISSION`,
`MODULE,SET,...`, `SETUP,INSTALL,...`, serta alias `SETUP,READY,DUAL,...`.
Aplikasi lama tetap dapat memakai query dan perintah sebelumnya.

## 3. Urutan koneksi aplikasi

Setelah BLE tersambung, kirim:

1. `PING`
2. `GET,INFO`
3. `GET,CAPS`
4. `GET,HARDWARE`
5. `GET,MODULES`
6. `GET,COMMISSION`
7. `GET,SETUP`
8. `GET,META`

Jika firmware lama menjawab `ERR,GET` pada `MODULES` atau `COMMISSION`, kembali
ke perilaku lama dan jangan memutus koneksi.

## 4. BLE GATT

| Fungsi | UUID | Properti |
|---|---|---|
| Service | `7a8f1000-6c9d-4e40-a45f-0b4b4e533230` | Primary |
| Telemetry | `7a8f1001-6c9d-4e40-a45f-0b4b4e533230` | Notify |
| Command | `7a8f1002-6c9d-4e40-a45f-0b4b4e533230` | Write |
| Response | `7a8f1003-6c9d-4e40-a45f-0b4b4e533230` | Notify |
| OTA data | `7a8f1004-6c9d-4e40-a45f-0b4b4e533230` | Write/No Response |
| OTA status | `7a8f1005-6c9d-4e40-a45f-0b4b4e533230` | Notify |

Semua integer multibyte paket biner memakai little-endian.

## 5. Paket telemetri lama

Header bersama:

| Offset | Ukuran | Isi |
|---:|---:|---|
| 0 | 2 | Magic `0xCD15` |
| 2 | 1 | Versi `3` |
| 3 | 1 | `0=CORE`, `1=DIAGNOSTIC` |
| 4 | 2 | Sequence |
| 18 | 2 | CRC16-CCITT byte 0–17 |

Kind CORE:

| Offset | Tipe | Isi |
|---:|---|---|
| 6 | `uint16` | RPM |
| 8 | `uint16` | TPS, 0–1000 permille |
| 10 | `int16` | Advance, centidegree |
| 12 | `uint16` | Aki, centivolt |
| 14 | `uint16` | HV CORE/CENTER, volt |
| 16 | `uint16` | HV SIDE, volt; nol bila tidak digunakan |

Kind DIAGNOSTIC:

| Offset | Tipe | Isi |
|---:|---|---|
| 6 | `int16` | Suhu centidegree; `INT16_MIN` tidak valid |
| 8 | `uint8` | Slot map 0–3 |
| 9 | `uint8` | 0 fire, 1 soft cut, 2 hard cut |
| 10 | `uint8` | Flags utama |
| 11 | `uint8` | Flags output |
| 12 | `uint16` | Fault bits |
| 14 | `uint16` | Trigger angle, centidegree |
| 16 | `uint8` | Pickup quality 0–100 |
| 17 | `uint8` | First Start, detik |

Flags output: bit 0 CORE/CENTER, bit 1 SIDE, bit 2 strobe, bit 3 fan. Status
modul diperoleh melalui `GET,MODULES`; paket 20 byte tidak diubah.

## 6. Frame perintah

```text
@sequence,COMMAND,arg1,arg2*CRC16\n
```

CRC16-CCITT memakai polynomial `0x1021`, initial `0xFFFF`, dihitung dari
`sequence` sampai argumen terakhir tanpa `@`, `*CRC`, CR, atau LF.

## 7. Status modul

Query `GET,MODULES` menghasilkan:

```text
MODULES,1,installedMask,activeMask,observedMask,faultMask,coreProfile
```

| Bit | Nilai | Modul |
|---:|---:|---|
| 0 | 1 | SIDE/kanal coil kedua |
| 1 | 2 | THERMAL/FAN |
| 2 | 4 | OEM LEARN |
| 3 | 8 | AUX/strobe; audio masih reserved |
| 4 | 16 | TPS DIAGNOSTIC |

| Field | Arti UI |
|---|---|
| `installedMask` | Dipilih pengguna sebagai modul yang dipasang |
| `activeMask` | Sedang diperintah firmware |
| `observedMask` | Ada bukti sinyal listrik saat ini |
| `faultMask` | Firmware memiliki alasan spesifik menyatakan bermasalah |

`observed=0` bukan otomatis rusak. Contoh modul SIDE saat HV mati tidak dapat
dikonfirmasi. Tampilkan `Belum diuji`, bukan `Tidak ada`.

| `coreProfile` | Label UI |
|---:|---|
| 0 | `IgniTra Core • 1 Coil` |
| 1 | `IgniTra Core + SIDE • Dual Coil (belum aktif)` |
| 2 | `IgniTra Core + SIDE • Dual Coil` |

Konfigurasi modul:

```text
MODULE,SET,SIDE,ON|OFF
MODULE,SET,THERMAL,ON|OFF
MODULE,SET,OEM_LEARN,ON|OFF
MODULE,SET,AUX,ON|OFF
MODULE,SET,TPS_DIAG,ON|OFF
```

Mesin harus berhenti dan HV di bawah 30 V. Menonaktifkan SIDE juga mematikan
output SIDE; menonaktifkan THERMAL mengubah fan menjadi OFF.

## 8. Dashboard setelah setup

| Kondisi | Kartu/gauge |
|---|---|
| Core saja | `HV Core/Center — J1.12` |
| SIDE dipasang, belum aktif | Tambahkan `HV Side — J1.6`, status `Belum aktif` |
| SIDE aktif | Tampilkan kedua HV dan status `Dual Coil` |
| THERMAL tidak dipasang | Sembunyikan suhu dan fan |
| THERMAL dipasang | Tampilkan suhu, mode fan dan output relay |
| OEM Learn tidak dipasang | Sembunyikan menu OEM Learn |
| AUX tidak dipasang | Sembunyikan strobe/audio |

Pin tetap ditulis untuk diagnosis. Nama `CENTER Cap` lama diganti menjadi
`HV Core/Center (J1.12)` dan `SIDE Cap` menjadi `HV Side (J1.6)`.

## 9. Flow setup aplikasi yang baru

Enam tab lama (`Baru`, `Pulser`, `TDC`, `TPS`, `First Start`, `Ready`) diganti
menjadi tiga layar. State firmware tetap dipertahankan.

### Layar 1 — Pemasangan

Pilihan:

1. **Ganti CDI OEM — Core 1 Coil** (default).
2. **Ganti CDI OEM — Dual Coil** jika modul SIDE benar-benar dipasang.
3. **Setup Lanjutan** untuk OEM Learn atau kalibrasi khusus.

Setelah pengguna mengonfirmasi CDI OEM telah dilepas:

```text
SETUP,INSTALL,CORE,OEM_REMOVED
SETUP,INSTALL,DUAL,OEM_REMOVED
```

Perintah tersebut memilih hardware, masuk ke mode pengganti CDI, menyimpan
konfirmasi OEM dilepas, dan tetap menahan gate OFF sampai pemeriksaan selesai.

Jangan meminta pengguna normal memilih OEM Learn, Manual, atau DIY sebagai
tiga pilihan setara. OEM Learn adalah alat lanjutan; Manual berarti output mati
selama kalibrasi; DIY adalah mode internal ketika IgniTra menggantikan OEM.

### Layar 2 — Pemeriksaan

Tampilkan satu daftar kartu:

1. **Pickup:** starter beberapa detik, lepaskan starter, tunggu RPM nol dan HV
   <30 V, lalu `SETUP,PICKUP,CONFIRM`.
2. **TDC:** gunakan strobe dan `SETUP,SAVE_TDC`. Gunakan
   `SETUP,MANUAL_TDC,...,CONFIRM` hanya untuk nilai terverifikasi. Profil
   `UNIVERSAL` bukan preset kendaraan.
3. **TPS:** throttle tertutup `SETUP,TPS,CLOSED`, throttle penuh
   `SETUP,TPS,OPEN`.

Pickup dan TDC wajib. TPS yang belum selesai harus tampil sebagai peringatan
karena map beban tidak bekerja benar.

### Layar 3 — First Start dan Ready

1. Saat mesin berhenti dan HV <30 V, kirim `SETUP,FIRST_START`.
2. Nyalakan mesin tanpa membuka gas berlebihan.
3. Firmware membatasi 3.000 RPM dan advance maksimum 10°.
4. Setelah stabil minimal tiga detik, firmware menyimpan bukti.
5. Matikan mesin dan tunggu HV <30 V.
6. Core: gunakan `SETUP,READY,CENTER` jika belum READY.
7. Dual: gunakan `SETUP,READY,DUAL,sideOffsetCentidegree` setelah offset
   diketahui. `READY,THREE` tetap diterima untuk aplikasi lama.

SIDE tidak boleh aktif hanya berdasarkan model motor; modul dan offset wajib.

## 10. Status commissioning

`GET,COMMISSION` menghasilkan:

```text
COMMISSION,1,stage,nextAction,ready,advisoryMask
```

| `nextAction` | Aksi UI |
|---:|---|
| 1 | Pilih pemasangan Core/Dual |
| 2 | Verifikasi pickup |
| 3 | Kalibrasi TDC |
| 4 | Kalibrasi TPS |
| 5 | Jalankan First Start |
| 6 | Matikan mesin dan konfirmasi Ready |
| 7 | Setup selesai |
| 8 | OEM Learn lanjutan sedang dipilih |

| Bit `advisoryMask` | Arti |
|---:|---|
| 0 | Pickup belum selesai |
| 1 | TDC belum selesai |
| 2 | TPS belum selesai |
| 3 | First Start belum terbukti |
| 4 | SIDE dikonfigurasi tetapi belum aktif |
| 5 | THERMAL dikonfigurasi tetapi suhu belum valid |

Gunakan `nextAction` untuk tombol utama; jangan menebak tahap dari halaman
lokal aplikasi.

## 11. Query lain

| Query | Isi |
|---|---|
| `GET,INFO` | Platform, R9, versi protokol/paket |
| `GET,HARDWARE` | Kemampuan PCB/modul |
| `GET,CAPS` | Batas dan capability |
| `GET,STATUS` | RPM, TPS, HV, map, permission, PRO |
| `GET,META` | Metadata map aktif |
| `GET,SETUP` | Konfigurasi setup lama |
| `GET,PROFILE` | Profil mesin dan rentang |
| `GET,TEMP` | Suhu/fan |
| `GET,ADC` | Nilai raw dan output |
| `GET,MODE` | Mode internal |
| `GET,LEARN` | OEM Learn |
| `GET,OTA` | OTA |

## 12. Buku Petunjuk dalam aplikasi

Menu Wiring/Skematik lama diganti menjadi **Buku Petunjuk**: paket dan modul,
pin J1, pemasangan Core/Dual, Setup Mudah, arti status, dan diagnosis. Sumber
konten pengguna berada di [`BUKU_PETUNJUK_PENGGUNA.md`](BUKU_PETUNJUK_PENGGUNA.md).
Jangan membawa editor skematik/perakitan PCB ke alur pengguna produk jadi.

## 13. Aturan perubahan berikutnya

1. Jangan mengubah UUID, paket v3, atau arti bit lama diam-diam.
2. Tambahkan fitur secara additive dan iklankan melalui `GET,CAPS`.
3. Jangan menyebut modul `terdeteksi` hanya dari `installedMask`.
4. Jangan menampilkan HV SIDE sebagai fault ketika SIDE tidak dipasang.
5. BLE putus tidak boleh mengubah izin output pengapian.
6. Perubahan pin harus memperbarui header board, README, skematik dan dokumen ini.
7. Modul EFI belum menjadi bagian capability R9.
