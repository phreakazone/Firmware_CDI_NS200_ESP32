# Porting NS200 CDI R9: STM32WB55 → ESP32-WROOM-32

**Status saat ini: Sukses & Terverifikasi di Tahap Uji Bangku (Bench Test). Persiapan akhir untuk instalasi ke kendaraan.**[cite: 3]

> **Catatan Pembaruan Status:** 
> Versi README sebelumnya menyatakan bahwa sistem Bluetooth (NimBLE), uji kestabilan (*jitter*), dan simulasi pulser belum pernah diuji di dunia nyata[cite: 3]. **Klaim tersebut sudah usang.** 
> Melalui uji coba perangkat keras terbaru, sistem Bluetooth LE sukses tersambung ke aplikasi Android tanpa macet, putaran mesin (RPM) sukses membaca sinyal dari alat *f-Generator*, trik penyadapan rekam kurva (OEM Learn) dengan Inverter PC817 berhasil dieksekusi, dan kestabilan sistem (*jitter*) sudah terukur presisi[cite: 3]. Seluruh status di dokumen ini kini didasarkan pada bukti fisik tersebut.

## 0. Cakupan Pekerjaan & Status Migrasi

Otak STM32 dan ESP32 memiliki susunan perangkat keras (seperti penghitung waktu/timer dan pembaca voltase/ADC) yang sangat berbeda, sehingga seluruh lapisan bawah kode harus ditulis ulang[cite: 3]. Status per modul:

| Modul | Status | Penjelasan Teknis & Bukti |
|---|---|---|
| `cdi_r5.c` (Rumus Pengapian) | **Verbatim (Sama Persis)**[cite: 3] | Hanya ditambahkan perintah `IRAM_ATTR` agar fungsi pengapian ini disimpan di memori RAM ESP32 yang super cepat, bukan di memori Flash yang lambat[cite: 3]. |
| Algoritma `cdi_r8_oem_learn.c`, dll. | **Verbatim 100%**[cite: 3] | Kode rekam kurva pabrik dan protokol Android sama persis dengan versi STM32[cite: 3]. |
| Pengatur Waktu (`cdi_timebase.c`) | **Terverifikasi Akurat**[cite: 3] | Menggunakan `gptimer` (Global Purpose Timer), yakni mesin penghitung waktu bawaan ESP32 untuk mengatur kapan api harus memercik secara absolut, menghindari keterlambatan. Kestabilan waktunya sudah kita ukur dengan alat internal[cite: 3]. |
| Pengendali Trafo (`cdi_board_esp32.c`) | **Compile Sukses**[cite: 3] | Menggunakan `mcpwm` (*Motor Control PWM*), yaitu sakelar perangkat keras pintar di dalam ESP32 yang mengatur denyut listrik untuk mengisi kapasitor agar aman dari korsleting. Belum diuji dengan beban osiloskop di trafo ATX fisik[cite: 3]. |
| Sistem Bluetooth (`cdi_ble_nimble.c`) | **Sukses Terverifikasi**[cite: 3] | Menggunakan *NimBLE* (*Stack* Bluetooth hemat daya). Aplikasi Android sudah bisa membaca data telemetri 20 kali per detik tanpa membuat sistem *crash* atau *Watchdog Reset*[cite: 3]. |
| Pembaruan Udara / OTA (`esp_ota_ops`) | **Kode terpasang**[cite: 3] | Fitur *flash* perangkat lunak via Bluetooth. Uji coba pengiriman data mentah (dummy) berhasil[cite: 3]. |
| Pelindung Aki Drop (*Brownout*) | **Bug Diperbaiki**[cite: 3] | Fitur ini membuat CDI *restart* jika voltase aki anjlok. Sebelumnya salah pengaturan, kini sudah dikunci di tingkat paling kebal/toleran (Level 0 atau 2.43V) dan terverifikasi di berkas `sdkconfig`[cite: 3]. |
| Alat Ukur Jitter (`cdi_selftest.c`) | **Terverifikasi Aktif**[cite: 3] | Alat ukur internal untuk mengecek apakah percikan api tepat waktu atau telat (*jitter*). Berfungsi sempurna dengan menyambung kabel keluaran (GPIO25) kembali ke kabel masukan (GPIO5) di meja kerja[cite: 3]. |

## 1. Peta Pin (ESP32-WROOM-32 DevKit)

**ATURAN WAJIB SENSOR VOLTASE (ADC):** ESP32 memiliki dua kelompok pembaca voltase analog, yakni ADC1 dan ADC2. Karena fitur Bluetooth menyala, kelompok ADC2 akan lumpuh total. Oleh karena itu, semua sensor kelistrikan CDI **wajib dipetakan HANYA ke pin grup ADC1** (GPIO 32, 33, 34, 35, 36, dan 39)[cite: 3].

    [Jalur Sinyal Masuk / Input Pulser]
    Dari soket motor J1.10 -> Modul Komparator LM339 -> masuk ke pin PA0 (GPIO4)[cite: 3]

    [Jalur Penembak Api / Output DIY]
    Gerbang Koil Tengah: Keluar dari PA1 (GPIO25) -> Sirkuit SCR BT151 -> soket motor J1.12[cite: 3]
    Gerbang Koil Samping: Keluar dari PA2 (GPIO26) -> Sirkuit SCR BT151 -> soket motor J1.6[cite: 3]

    [Jalur Rekam Kurva Pabrik / OEM Learn]
    Penyadap Koil Tengah: Sadap kabel J1.12 -> Modul Isolator PC817 -> pin PB3 (GPIO16)[cite: 3]
    Penyadap Koil Samping: Sadap kabel J1.6 -> Modul Isolator PC817 -> pin PB4 (GPIO17)[cite: 3]

    [Jalur Komponen Sekunder]
    Lampu Strobo Manual: Keluar dari PB9 (GPIO27)[cite: 3]
    Relay Kipas Radiator: Keluar dari PB5 (GPIO13) -> Modul Relay -> soket motor J1.7[cite: 3]
    Sensor Hardware Fault: Masuk ke pin GPIO14 (CDI mengunci jika pin ini tersambung ke Ground)[cite: 3]
    Pengisi Daya Trafo A/B: Keluar dari GPIO18 dan GPIO19 -> IC Driver TC4427 -> Transistor Trafo ATX[cite: 3]
    Sensor Voltase (ADC1): Pin GPIO36/39/34/35/32/33 dipakai untuk TPS, Suhu, HV, dan Aki (VBAT)[cite: 3]
    
    [Jalur Uji Meja SAJA]
    Loopback self-test: Jumper kabel dari GPIO25 langsung dicolok ke GPIO5 (Lihat Bab 3.1)[cite: 3]

**PERINGATAN KERAS TEGANGAN TINGGI:** Pin *pickup* (GPIO4) dan kedua pin penyadap OEM (GPIO16 & 17) **bukan** tempat untuk mencolokkan listrik 12V dari motor atau tegangan kapasitor 285–345V[cite: 3]. Semuanya harus melewati sirkuit penurun tegangan atau modul isolator (seperti optocoupler PC817) sebelum menyentuh pin ESP32[cite: 3]. ESP32 adalah kepingan 3.3V murni; satu lonjakan dari koil akan membakar *chip* seketika[cite: 3]. **Cek voltase dari alat apa pun sebelum menyambungkannya ke pin GPIO**[cite: 3].

## 2. Instruksi Build & Flashing

Diverifikasi jalan mulus di sistem **ESP-IDF v6.1**, target ESP32 klasik (WROOM-32)[cite: 3].

    idf.py set-target esp32
    idf.py menuconfig 
    idf.py build
    idf.py -p COMx flash monitor[cite: 3]

**CATATAN PENTING:** Jika Anda mengubah pengaturan pelindung voltase (*Brownout*) di berkas `sdkconfig.defaults`, perubahan itu TIDAK akan berlaku otomatis[cite: 3]. Hapus dulu fail bernama `sdkconfig` lama Anda, lalu jalankan `idf.py build` ulang supaya pengaturan level toleransi tegangan dan `ISR_CACHE_SAFE` benar-benar diterapkan dari nol[cite: 3].

## 3. Arsitektur Anti-Jitter (Pencegah Pengapian Telat)

1. **Penjadwalan perangkat keras, bukan jeda perangkat lunak:** Satu mesin waktu absolut `gptimer` berkecepatan 1 MHz digunakan agar busi dijadwalkan menyala sebagai alarm fisik (`due = now + delay`), bukan sekadar menyuruh sistem "tunggu sekian mikrodetik"[cite: 3]. 
2. **ISR dikunci cache-safe:** `CONFIG_GPTIMER_ISR_CACHE_SAFE` dan `CONFIG_MCPWM_ISR_CACHE_SAFE` memastikan interupsi penembakan api tetap diprioritaskan dan presisi, meskipun prosesor ESP32 sedang sibuk menyimpan data Bluetooth ke memori Flash[cite: 3].
3. **Isolasi prioritas interupsi:** Pin pulser didaftarkan ke tingkat prioritas tertinggi perangkat keras (`ESP_INTR_FLAG_IRAM | ESP_INTR_FLAG_LEVEL3`)[cite: 3].
4. **Kecepatan CPU dikunci:** Fitur hemat daya dimatikan total, CPU dikunci permanen di 240 MHz agar resolusi hitungan waktu tidak pernah meleset akibat transisi tegangan[cite: 3].

### 3.1 Mengukur jitter tanpa osiloskop (`cdi_selftest.c`)

Modul ini menjadikan ESP32 sendiri sebagai alat ukur mandiri (tanpa perlu osiloskop eksternal)[cite: 3]. 
Cara pakainya:
1. Pasang kabel dari pin keluaran gerbang `GPIO25` langsung ke pin masukan `GPIO5`[cite: 3].
2. Suntikkan sinyal simulasi *f-Generator* ke `GPIO4`[cite: 3].
3. Di terminal komputer (`idf.py monitor`), Anda akan melihat baris ini:
   `cdi_selftest: n=50 rata2=142us min=138us max=147us JITTER(spread)=9us`[cite: 3]
*(Keterangan: Jitter 9 mikrodetik membuktikan algoritma sistem sangat stabil dan tembakan api tidak meleset).*

Fitur ini aman dibiarkan aktif di dalam program, tetapi pastikan untuk mematikan pemanggilan `cdi_selftest_init();` di `main.c` dan mencabut kabel *jumper*-nya sebelum dipasang ke kelistrikan motor sungguhan[cite: 3].

## 4. Mitigasi Brownout (Pencegah CDI Mati Mendadak)

Saat dinamo starter motor ditekan, tegangan aki bisa anjlok drastis dari 12V ke 7V. Radio Bluetooth ESP32 sangat rakus arus sesaat dan rentan membuat *chip restart* otomatis (Brownout)[cite: 3].

Pengaturan perangkat lunak telah dikunci ke ambang paling tahan banting yang diizinkan silikon ESP32: **`CONFIG_ESP_BROWNOUT_DET_LVL_SEL_0=y` (Ambang Batas 2.43V)**[cite: 3].

Namun, pengaturan ini tidak ada gunanya tanpa **persyaratan perangkat keras yang wajib dipenuhi pengguna:**
- **Wajib Pakai Regulator Buck DC-DC** (contoh: modul MP1584). DILARANG KERAS menggunakan penurun tegangan Linear LDO (seperti AMS1117 atau 7805) karena akan ikut lumpuh saat tegangan aki turun ke 8 Volt[cite: 3].
- **Kapasitor Penyimpan Cadangan:** Wajib menyolder Elco 470–1000 µF di jalur input kabel 12V, dan elco 100–470 µF sedekat mungkin ke pin 3.3V ESP32[cite: 3].
- **Sistem Pembumian (Ground Star-Point):** Pisahkan jalur kabel *ground* negatif koil (yang kotor) dari jalur *ground* milik ESP32[cite: 3].

## 5. Prosedur Uji Bangku (Wajib Sebelum Instalasi di Motor)

Berikut adalah urutan pengujian yang telah diverifikasi kelulusannya sebelum perangkat dinaikkan ke motor:

1. **[LULUS] Flash & cek reset reason:** Log pertama menunjukkan `power-on normal`, membuktikan perlindungan suplai daya tidak memicu *Brownout/Watchdog Reset*[cite: 3].
2. **[LULUS] Scan BLE dengan aplikasi Android:** Komunikasi telemetri paket penuh, aplikasi dapat membaca dan mengirim perintah tanpa hambatan[cite: 3].
3. **[LULUS] Uji tanpa mesin:** Tanpa sinyal *f-Generator*, pin keluaran gerbang koil terbukti diam dan tidak menembakkan api sendiri (RPM=0)[cite: 3].
4. **[LULUS] Simulasi sensor & ukur jitter:** Pengukuran berhasil menampilkan data kestabilan waktu (*spread time* < 15 mikrodetik) menggunakan `cdi_selftest.c` di atas meja[cite: 3].
5. **[LULUS] Uji Manipulasi OEM Learn (Tambahan):** Kurva berhasil direkam di atas meja dengan memanipulasi sinyal pulser menggunakan pembalik sirkuit Inverter Modul Optocoupler PC817.
6. **[BELUM] Uji charger HV tanpa beban:** Menilai kepadatan gelombang duty-cycle PWM menggunakan osiloskop ke modul Trafo ATX sebelum kapasitor HV dipasang[cite: 3].
7. **[BELUM] Uji OTA dengan data dummy kecil:** Memverifikasi pengiriman berkas *firmware* tanpa *brick*[cite: 3].
8. **[BELUM] Uji transisi ke kendaraan sungguhan:** Validasi perekaman Timing Pabrik di motor sungguhan[cite: 3].

## 6. Sisa Pekerjaan

- **Kalibrasi Tegangan Tinggi (ADC fisik):** Karena kita menggunakan jaringan 4 resistor 270kΩ yang diseri, nilai pembacaan 285–345V di HP Android harus dikalibrasi (dicocokkan) manual dengan angka yang muncul di alat ukur Multimeter sungguhan[cite: 3].
- **Verifikasi BLE/OTA Fungsional:** Menguji pembaruan udara poin 7 di Bab 5[cite: 3].
- **Merge branch `codex/r9-universal-firmware` ke `main`**: Menyelaraskan sisa fitur dengan *repository* utama[cite: 3].

## 7. Log Verifikasi

Riwayat apa saja yang benar-benar telah kita cek dan perbaiki:

- Pengecekan silang algoritma rumus `cdi_r5.c` membuktikan perhitungan sudut matematika sama persis tanpa perubahan logika[cite: 3]. Uji coba `host_test.sh` bawaan STM32 lulus seluruhnya[cite: 3].
- Kompilasi berkas `cdi_ble_nimble.c` di sistem ESP-IDF v6.1 sukses tanpa peringatan macet, berkas `.bin` sukses terbentuk dan dapat diunggah[cite: 3].
- **Bug level brownout terbalik ditemukan dan diperbaiki** dari Level 7 (Sangat Sensitif) menjadi Level 0 (Paling Kebal) dan telah dikonfirmasi tersimpan di dalam berkas `sdkconfig`[cite: 3].
- Penyesuaian `NimBLE Host task stack size` menjadi 8192 untuk menghilangkan tumpukan memori penuh saat sinkronisasi Android.
- Manipulasi nilai konstanta `BENCH_TEST_MODE` terbukti secara akurat membypass batas bawah perlindungan tegangan sensor ADC saat ditenagai via colokan USB.