# Panduan Lengkap Bench Test CDI ESP32 (Menggunakan GM328A & Self-Test Loopback)

Panduan ini ditujukan untuk menguji logika otak pengapian (Derajat Pengapian, Peta Resolusi 32x16, koneksi ke HP Android, dan kestabilan Jitter) menggunakan ESP32 secara mandiri di atas meja, tanpa menyalakan mesin motor sungguhan[cite: 4].

---

## 1. Persiapan Firmware (Bypass & Alokasi Memori)

Jika kita hanya menyalakan ESP32 memakai kabel colokan USB (5 Volt), sensor akan melaporkan bahwa aki motor tekor (0 Volt). Ini memicu sistem perlindungan perangkat keras (*Hardware Fault*) untuk memblokir pembacaan RPM. Anda wajib menipu sistem tersebut:

**A. Mengaktifkan Mode Uji Meja (Hardware Bypass)**
Di dalam file `cdi_engine_esp32.c`, cari baris kode ini di bagian atas:
`#define BENCH_TEST_MODE 1`[cite: 4]

*   **Nilai `1`:** ESP32 akan memanipulasi pembacaan sensor analog (ADC) dari dalam, sehingga ESP32 menganggap aki 12V dan kapasitor 345V dalam keadaan aman, sehingga RPM bisa dibaca[cite: 4].
*   **Nilai `0`:** Sistem perlindungan aktif sepenuhnya. Hanya gunakan ini jika perangkat sudah disolder di PCB dan akan dihubungkan ke aki motor sungguhan.

**B. Memperbesar Memori Mesin Bluetooth**
Agar ESP32 tidak macet saat mengobrol membagi data dengan aplikasi Android:
1. Buka terminal PC, ketik perintah `idf.py menuconfig`[cite: 4].
2. Masuk ke **Component config** -> **Bluetooth** -> **NimBLE Options**[cite: 4].
3. Cari pengaturan `NimBLE Host task stack size` lalu perbesar ukurannya menjadi **8192**[cite: 4].
4. Simpan (Save) dan Keluar (Quit)[cite: 4].

**C. Menghidupkan Alat Ukur Kestabilan (Self-Test)**
Pastikan file `main.c` Anda memanggil fungsi pengukuran internal ini (tanpa garis miring):
`cdi_selftest_init(); // <-- AKTIFKAN UNTUK UJI MEJA`[cite: 4]

---

## 2. Pengkabelan Uji Meja (Wiring GM328A)

Kita menggunakan alat *LCR Component Tester / GM328A* dan kabel *jumper* sebagai pengganti mesin yang berputar[cite: 4]. 

**SANGAT PENTING:** Anda wajib menggunakan menu **f-Generator** (Penghasil Gelombang Frekuensi) di alat Anda. JANGAN menggunakan menu `10-bit PWM`. Menu PWM tersebut memancarkan kecepatan statis yang kelewat tinggi (sekitar 7.8kHz atau setara 460.000 RPM). Filter penyaring perangkat keras di ESP32 akan otomatis menolaknya dan menganggapnya sebagai "sampah elektronik" (noise).

**Rute Pemasangan Kabel:**
| Dari (Alat / Pin) | Menuju Pin ESP32 | Keterangan Alur |
|---|---|---|
| **Kabel Bawah / GND** (Alat GM328A) | **GND** (ESP32) | Jalur wajib agar alat tester dan ESP32 memiliki referensi 0V yang satu pemahaman[cite: 4]. |
| **Kabel Sinyal f-Generator** (Alat) | **GPIO4** (ESP32 / Pickup) | Jalur ini bertindak sebagai suara pulser dari putaran magnet mesin kruk as[cite: 4]. **(BACA PERINGATAN TEGANGAN DI BAWAH)** |
| **GPIO25** (Gerbang Koil Tengah) | **GPIO5** (Pin Kosong Loopback)| Siapkan seutas kabel pendek (jumper) untuk menyatukan kedua pin ini secara langsung agar latensi penembakan koil dapat diukur oleh sistem[cite: 4]. |

> **⚠️ PERINGATAN TEGANGAN MAKSIMAL:**
> Sebelum mencolokkan kabel *f-Generator* ke **GPIO4**, ukur dulu voltasenya menggunakan alat ukur lain (Voltage Test)[cite: 4]. Jika tegangan yang dipancarkan tester Anda mencapai 5V, Anda **wajib** memotong kabelnya dan menyisipkan resistor pembagi tegangan (misalnya resistor 10kΩ dan 15kΩ) agar listrik yang masuk ke ESP32 mentok di batas aman maksimal 3.3V, atau *chip* akan terbakar[cite: 4].

**Trik Khusus: Merekam Kurva (OEM Learn) Tanpa Mesin Motor:**
Mode perekaman kurva mewajibkan ESP32 menerima pukulan sinyal dari Pulser dan dari Koil secara persis bersamaan. Untuk menipu fitur ini di atas meja:
* Siapkan papan **Modul Optocoupler PC817** (Dirangkai terbalik sebagai Inverter *Active-Low*).
* Hubungkan pin listrik `3.3V` dari ESP32 langsung ke terminal masukan (seperti `IN1` dan `U1`) di modul PC817.
* Cabang kabel sinyal dari *f-Generator*, lalu colokkan ke terminal masukan *Ground* di PC817.
* Sambungkan terminal keluaran modul PC817 langsung ke pin **GPIO16** (OEM Tap Center) dan **GPIO17** (OEM Tap Side) di ESP32.

---

## 3. Simulasi & Pembacaan Angka Jitter

Setelah semua kode dimasukkan (`idf.py app-flash`), buka aplikasi Android dan koneksikan tombol Bluetooth[cite: 4]. Di alat GM328A, pilih menu **f-Generator**[cite: 4].

Terjemahan angka Hertz (Hz) menjadi Putaran Mesin (RPM) - Jika mesin Anda PPR = 1:
* **13.3 Hz** = Sekitar 800 RPM (Stasioner / Langsam)[cite: 4]
* **25 Hz** = 1500 RPM[cite: 4]
* **50 Hz** = 3000 RPM[cite: 4]
* **100 Hz** = 6000 RPM[cite: 4]

Biarkan menyala dan lihat layar hitam terminal komputer Anda (`idf.py monitor`)[cite: 4]. Setiap detik, sistem akan mencetak laporan:
`I (xxxx) cdi_selftest: n=50 rata2=142us min=138us max=147us JITTER(spread)=9us`[cite: 4]

* **rata2 = 142us (Beban Bias):** Ini adalah waktu yang dihabiskan mikrokontroler untuk bereaksi secara konstan[cite: 4]. Ini hal yang sangat wajar di dunia elektronik digital, dan perbedaan sudut ini akan secara otomatis digeser (di-Nol-kan) saat Anda mengetesnya memakai pistol *Timing Light* di menu Offset aplikasi Android[cite: 4].
* **JITTER = 9us (Skor Stabilitas):** Ini adalah selisih melesetnya api (Max dikurangi Min)[cite: 4]. Jika skor angka ini stabil bernilai kecil (di bawah 15 mikrodetik), artinya sistem pengapian Anda sangat tajam, akurat, dan dapat diandalkan untuk putaran mesin tinggi[cite: 4].

---

## 4. Persiapan Akhir Pasang ke Motor (Production Ready)

Jika semua skor di atas meja sudah lulus, **Anda WAJIB MENGUBAH 4 HAL INI KEMBALI KE ASAL** sebelum mikrokontroler dihubungkan ke alat tegangan tinggi Inverter Trafo ATX dan aki motor[cite: 4]:

1. **LEPAS** semua kabel dari alat GM328A dan cabut sepenuhnya kabel pelompat (`GPIO25` ke `GPIO5`)[cite: 4].
2. Buka berkas `cdi_engine_esp32.c`, ubah sakelar utamanya menjadi `#define BENCH_TEST_MODE 0` untuk menghidupkan kembali sensor perlindungan kelistrikan dari korsleting perangkat keras[cite: 4].
3. Buka berkas `main.c`, matikan fungsi penghitung latensi kabel yang sudah Anda cabut tadi dengan menambahkan dua buah garis miring: `// cdi_selftest_init();`[cite: 4].
4. Lakukan proses *Build* dan *Flash* ke mikrokontroler ESP32 Anda untuk terakhir kalinya agar sistem siap bertarung di kelistrikan motor[cite: 4].