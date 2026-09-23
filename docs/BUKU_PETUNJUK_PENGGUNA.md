# Buku Petunjuk Pengguna IgniTra CDI R9 Modular

## 1. Fungsi produk

IgniTra adalah CDI programmable pengganti CDI OEM. Paket **Core** menjalankan
satu coil melalui `J1.12`. Modul **SIDE** menambahkan coil kedua melalui
`J1.6`. Modul lain menambah suhu/fan, OEM Learn, AUX, atau diagnosis TPS.

IgniTra bukan ECU injeksi; modul EFI belum digunakan pada versi ini.

## 2. Pilih susunan hardware

**Core selalu menjadi dasar. Semua modul opsional dapat dipasang bersamaan;**
SIDE, THERMAL, OEM Learn, AUX, dan TPS Diagnostic bukan paket yang saling
menggantikan. Pilihan `Core 1 Coil` atau `Dual Coil` hanya menentukan jumlah
kanal pengapian, bukan membatasi modul tambahan.

| Bagian | Fungsi | Dampak di aplikasi | Syarat penggunaan |
|---|---|---|---|
| Core | Pengapian utama J1.12 | Selalu ada | Wajib |
| SIDE | Coil kedua J1.6 | Mengubah profil menjadi Dual Coil | Offset SIDE harus valid |
| THERMAL | Sensor suhu dan relay kipas | Kartu suhu serta FAN OFF/ON/AUTO | Sensor dipasang dan dikalibrasi |
| OEM Learn | Membaca pulsa CDI OEM melalui isolator | Menu OEM Learn di Setup Lanjutan | Dipakai hanya saat sesi belajar |
| AUX | Output bantu/strobe | Kontrol AUX/strobe | Fungsi audio firmware belum aktif |
| TPS Diagnostic | Memantau referensi dan sinyal TPS | Data TPS raw/reference dan diagnosis | Jalur TPS diagnostic dipasang |

Contoh susunan yang sah:

| Susunan fisik | Profil pengapian | Fitur tambahan |
|---|---|---|
| Core | Core 1 Coil | Tidak ada |
| Core + THERMAL + AUX | Core 1 Coil | Suhu/fan dan strobe |
| Core + SIDE + THERMAL | Dual Coil | Coil kedua dan suhu/fan |
| Core + SIDE + OEM Learn + TPS Diagnostic | Dual Coil | OEM Learn dan diagnosis TPS |
| Core + seluruh modul | Core atau Dual sesuai SIDE | Semua menu modul tersedia |

Memasang modul fisik dan mengaktifkannya di aplikasi adalah dua pekerjaan
berbeda. Konektor pasif tidak selalu dapat dideteksi otomatis, sehingga setiap
modul harus dicentang sesuai hardware yang benar-benar dipasang. Modul boleh
aktif bersamaan, tetapi masing-masing tetap memiliki pemeriksaan dan syarat
sendiri.

## 3. Pin harness utama J1

| J1 | Fungsi |
|---:|---|
| 1 | Tidak digunakan |
| 2 | TPS_A |
| 3 | Sensor suhu, bila dipakai |
| 4 | TPS_B |
| 5 | 12 V setelah kontak (`IGN_12V`) |
| 6 | Coil SIDE, hanya dengan modul SIDE |
| 7 | Kendali relay kipas |
| 8 | Tidak digunakan |
| 9 | Tidak digunakan |
| 10 | Pickup/pulser |
| 11 | Ground utama (`GND_STAR`) |
| 12 | Coil Core/Center |

J1.8 dan J1.9 tetap kosong. Jangan memindahkan sinyal ke pin kosong hanya karena
pin tersebut tersedia.

## 4. Pemasangan langsung mengganti CDI OEM

1. Matikan kontak dan lepaskan terminal negatif aki.
2. Lepaskan soket CDI OEM.
3. Sambungkan harness IgniTra sesuai nomor J1.
4. Core memakai output coil `J1.12`; biarkan `J1.6` tidak tersambung.
5. Dual Coil memerlukan modul SIDE dan coil kedua pada `J1.6`.
6. Periksa ulang `J1.5` 12 V, `J1.10` pickup, dan `J1.11` ground.
7. Sambungkan kembali aki dan hidupkan kontak, tetapi jangan langsung starter.
8. Hubungkan aplikasi melalui BLE dan pilih **Setup Mudah**.

Tidak diperlukan adaptor luar atau CDI OEM untuk Setup Mudah.

## 5. Setup Mudah

### A. Pemasangan

Pilih **Core 1 Coil** atau **Dual Coil**. Dual hanya dipilih bila modul SIDE dan
coil kedua sudah dipasang. Centang konfirmasi bahwa CDI OEM sudah dilepas.
IgniTra masuk mode pengganti CDI, tetapi output masih ditahan sampai pemeriksaan
selesai.

### B. Pemeriksaan

#### Pickup

Tekan starter beberapa detik sampai RPM/pulsa terbaca. Lepas starter, tunggu RPM
kembali nol, lalu tekan **Simpan Pickup**. Jika kualitas kurang, periksa J1.10
dan polaritas pickup.

#### TDC

Gunakan strobe pada tanda TDC. Profil `UNIVERSAL` bukan nilai khusus kendaraan
dan tidak boleh dianggap sudah akurat. Profil kendaraan terverifikasi boleh
dipakai sebagai nilai awal, lalu tetap diperiksa.

#### TPS

Dengan mesin mati, simpan posisi gas tertutup lalu gas terbuka penuh. Jangan
membalik `TPS_A` dan `TPS_B` lewat aplikasi; jumper JTPS menentukan arah jalur.

### C. First Start

1. Pastikan kendaraan aman dan roda penggerak bebas.
2. Tekan **Mulai First Start** saat mesin mati.
3. Starter dan biarkan idle minimal tiga detik.
4. Mode ini membatasi RPM 3.000 dan advance 10°.
5. Matikan mesin setelah dinyatakan berhasil dan tunggu HV di bawah 30 V.
6. Core masuk `Ready Core`. Dual memerlukan offset SIDE terukur sebelum
   `Ready Dual`.

Jangan mengaktifkan Dual Coil menggunakan offset tebakan.

## 6. Arti tampilan

| Tampilan | Arti |
|---|---|
| `IgniTra Core • 1 Coil` | Kanal J1.12 dikonfigurasi |
| `Dual Coil (belum aktif)` | SIDE dipilih tetapi belum Ready |
| `IgniTra Core + SIDE • Dual Coil` | J1.12 dan J1.6 aktif |
| `HV Core/Center — J1.12` | Tegangan kapasitor utama |
| `HV Side — J1.6` | Tegangan kapasitor SIDE |
| `Belum diuji` | Dipilih tetapi belum ada bukti sinyal |
| `Aktif` | Firmware sedang memakai modul |
| `Gangguan` | Firmware mendeteksi kesalahan tertentu |

Nilai SIDE nol normal pada paket Core.

### Identitas perangkat

Menu Tentang/Perangkat menampilkan versi firmware dan Serial Number berbentuk
`IGT-ESP32-XXXXXXXXXXXX`. Aplikasi dapat meminta binding lokal sebelum membuka
menu Setup. Serial Number hanya identitas unit, bukan password atau kode rahasia.
Jika berganti ponsel atau memasang ulang aplikasi, binding lokal perlu dilakukan
ulang sesuai petunjuk aplikasi.

## 7. Fan otomatis

FAN AUTO hanya dipakai bila modul THERMAL/FAN dan sensor suhu terpasang serta
terkalibrasi. Jika sensor invalid dalam AUTO, firmware menyalakan fan sebagai
fail-safe. Arus motor kipas melewati relay, bukan GPIO IgniTra.

## 8. OEM Learn

OEM Learn bukan Setup Mudah. CDI OEM harus tetap menjalankan mesin dan modul
PC817 membaca pulsa OEM secara pasif. Fitur ini hanya untuk pengembangan profil
atau pengukuran offset.

## 9. Diagnosis singkat

### RPM tetap nol

- Periksa J1.10 dan J1.11.
- Periksa polaritas/edge pickup di menu lanjutan.
- Jangan lanjut ke TDC atau First Start.

### RPM ada tetapi HV Core nol

- Periksa J1.5, JP1 daya ESP32, charger, dan fault hardware.
- Matikan kontak sebelum menyentuh rangkaian charger.

### Core hidup tetapi SIDE belum aktif

- Pastikan SIDE dipilih sebagai terpasang.
- Pastikan First Start Core berhasil.
- Pastikan offset SIDE terukur dan Ready Dual disimpan.
- Periksa J1.6 dan feedback HV SIDE.

### TPS nol atau terbalik

- Kalibrasi CLOSED/OPEN ulang.
- Periksa JTPS dan J1.2/J1.4.
- Ubah jumper hanya saat kontak mati.

### Suhu invalid

- Pastikan THERMAL dipilih dan terpasang.
- Periksa J1.3 dan lakukan kalibrasi tiga titik sebelum FAN AUTO.

## 10. Keselamatan servis

- Charger, kapasitor, `BRIDGE_PLUS`, `COIL_CENTER`, dan `COIL_SIDE` berbahaya
  walaupun mesin telah mati.
- Tunggu HV di bawah 30 V sebelum mengubah setup.
- Jangan memasukkan 12 V atau pulsa coil langsung ke ESP32.
- Jangan mencabut modul saat kontak hidup.
- Jangan memakai ground sensor untuk arus coil atau kipas.
- Jangan berkendara sebelum pickup, TDC, TPS, dan First Start selesai.
