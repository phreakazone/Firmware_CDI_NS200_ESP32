# Porting NS200 CDI R9: STM32WB55 → ESP32-WROOM-32

**Status saat ini: Build terverifikasi sukses (ESP-IDF v6.1), uji bangku sedang berjalan.**

> Catatan jujur soal status: versi README sebelumnya menyatakan proyek ini
> "Selesai & Terverifikasi" termasuk BLE "Lulus kompilasi... Selesai 100%".
> Setelah diaudit ulang, klaim itu **terlalu jauh** dari bukti yang benar-benar
> ada saat itu — kode `cdi_ble_nimble.c` bahkan punya komentar penulisnya
> sendiri yang mengaku belum pernah dikompilasi. README ini ditulis ulang
> supaya setiap klaim status hanya berdasarkan sesuatu yang **benar-benar
> sudah dibuktikan** (build log, hasil test, atau pengukuran), dan yang masih
> berupa rencana/asumsi ditandai jelas sebagai "belum diverifikasi". Lihat
> §7 (Log Verifikasi) untuk riwayat lengkap apa yang sudah dicek dan kapan.

## 0. Cakupan Pekerjaan & Status Migrasi

STM32WB55 dan ESP32 tidak berbagi satu pun peripheral yang sama (timer, ADC,
BLE stack, tata letak flash), sehingga seluruh lapisan perangkat keras
ditulis ulang. Status per modul:

| Modul | Status | Bukti |
|---|---|---|
| `cdi_r5.c` (interpolasi, limiter, timing) | **Verbatim, hanya `IRAM_ATTR` ditambahkan** | Diff langsung ke sumber STM32: cuma 2 baris atribut berubah. `host_test.sh` asli lulus penuh dengan file ini. |
| `cdi_r5_charger.c`, `cdi_r5_protocol.c`, `cdi_r8_oem_learn.c`, `cdi_r5_ble.c`, `cdi_r8_ota.c` | **Verbatim 100%, byte-identik** | Diff kosong terhadap sumber STM32. |
| **Capture & Timebase** (`cdi_timebase.c`) | **Rewrite, compile sukses** | Satu `gptimer` bersama untuk seluruh sistem (bukan banyak timer terpisah) — desain ini otomatis menghindari masalah "hanya 4 hardware timer di ESP32 klasik" dan masalah offset antar-timer yang ditemukan di percobaan Arduino sebelumnya. **Presisi jitter aktualnya BELUM diukur di hardware** — lihat §3 dan §7. |
| **PWM Charger** (`cdi_board_esp32.c`) | **Rewrite, compile sukses** | `mcpwm` dengan dead-time hardware. Belum diverifikasi dengan osiloskop di keluaran fisik. |
| **Stack BLE (NimBLE)** (`cdi_ble_nimble.c`) | **Compile & link sukses** (build log terverifikasi, `ns200_cdi_esp32.bin` ter-generate) | **Konektivitas radio sungguhan (scan/pairing/GATT dari HP) BELUM diuji.** Jangan anggap ini "selesai" sampai lolos tes nRF Connect di §5. |
| **BLE OTA** (`esp_ota_ops`) | Kode terpasang, compile sukses | Belum pernah dicoba kirim data OTA sungguhan. Uji dengan data dummy dulu, bukan firmware asli (lihat §5). |
| **Brownout detector** | **Bug ditemukan & diperbaiki** | Lihat §4 — versi sebelumnya salah level (Level 7, bukan Level 0), sekarang sudah benar dan **terverifikasi lewat isi `sdkconfig` hasil generate**, bukan cuma `sdkconfig.defaults`. |
| **Self-test jitter tanpa osiloskop** (`cdi_selftest.c`) | **Aktif & terintegrasi penuh** — hook di `cdi_engine_esp32.c` + `cdi_selftest_init()` terpasang di `main.c` | Lihat §3.1. Sudah siap dipakai di meja; angka jitter aktual masih menunggu sesi bangku dengan pickup simulasi. |

> **Catatan soal branch `codex/r9-universal-firmware`:** ada pengembangan lanjutan
> (batas RPM 30.000, peta 32×16, profil mesin universal, kalibrasi suhu, live
> dyno trim) di branch itu, sudah diverifikasi identik antara repo STM32 dan
> ESP32 (diff `cdi_r5.c` kosong). **Belum di-merge ke `main`** karena mengubah
> token handshake BLE (`PONG_R7_2` -> `PONG_R9`) dan format penyimpanan
> (`STORE_VERSION` naik) — breaking change untuk unit yang sudah terpasang di
> lapangan dengan firmware `main` saat ini. Aplikasi Android pendamping
> (`CDI_STM32_Android`) sudah lebih dulu mengirim sebagian perintah gaya R9,
> tapi punya bug terpisah (pengecekan token PONG belum menerima `PONG_R9`) yang
> sudah dipatch di repo itu. Merge R9 ke `main` di sini adalah keputusan
> terpisah yang belum diambil.

## 1. Peta Pin (ESP32-WROOM-32 DevKit)

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

    [Uji Bangku SAJA - Tidak dipakai di operasi normal]
    Loopback self-test (GPIO5) -> Jumper dari GPIO25, lihat §3.1

**PERINGATAN KERAS:** Pin pickup (PA0) dan kedua pin tap OEM (PB3 & PB4)
**bukan** input 12V atau 285–345V. Semuanya harus lewat rangkaian
optocoupler/isolator berimpedansi tinggi sebelum menyentuh GPIO ESP32. ESP32
adalah IC 3.3V murni; satu pulsa bocor dari primer koil (ratusan volt) akan
menghancurkan chip seketika. Ini juga berlaku untuk sinyal simulasi dari alat
uji bangku — banyak generator sinyal murah (function generator/multi-tester)
beroperasi di ~5V, BUKAN 3.3V. **Cek amplitudo dulu sebelum menyambung ke GPIO
manapun** (lihat §3.1 untuk contoh pembagi tegangan).

## 2. Instruksi Build & Flashing

Diverifikasi jalan di **ESP-IDF v6.1**, target ESP32 klasik (WROOM-32).

    idf.py set-target esp32
    idf.py menuconfig   # (opsional) verifikasi parameter
    idf.py build
    idf.py -p COMx flash monitor

**Kalau kamu mengubah `sdkconfig.defaults` setelah `sdkconfig` sudah pernah
ter-generate sebelumnya, perubahan itu TIDAK otomatis berlaku** — opsi yang
sudah pernah di-set akan tetap dipakai dari `sdkconfig` lama, bukan dari
`.defaults` yang baru. Hapus dulu `sdkconfig` (bukan `fullclean`, cukup file
itu saja) lalu build ulang supaya semua nilai di `.defaults` benar-benar
diterapkan dari nol. Verifikasi hasilnya:

    findstr "BROWNOUT_DET_LVL_SEL_0 BROWNOUT_DET_LVL= CPU_FREQ_MHZ ISR_CACHE_SAFE" sdkconfig

Menambah file `.c` baru ke `main/` **tidak cukup** hanya dengan menaruhnya di
folder — `main/CMakeLists.txt` di proyek ini memakai daftar `SRCS` eksplisit
(bukan glob otomatis), jadi nama file barunya wajib ditambahkan manual ke
daftar itu. Setelah `CMakeLists.txt` diedit, `idf.py build` otomatis
men-trigger reconfigure CMake sendiri — tidak perlu `fullclean`.

**Catatan versi ESP-IDF:** beberapa nama opsi Kconfig berbeda antar versi.
Yang dipakai di `sdkconfig.defaults` sudah nama v6.1
(`CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240`, `CONFIG_GPTIMER_ISR_CACHE_SAFE`,
`CONFIG_MCPWM_ISR_CACHE_SAFE`). Kalau kamu pakai ESP-IDF v5.x, nama lamanya
adalah `CONFIG_ESP32_DEFAULT_CPU_FREQ_240` dan `*_ISR_IRAM_SAFE` — ESP-IDF
biasanya memetakan otomatis lewat `sdkconfig.rename` dan cuma memberi NOTE,
bukan error, tapi tetap disarankan pakai nama yang sesuai versimu.

## 3. Arsitektur Anti-Jitter

1. **Penjadwalan hardware, bukan software delay.** Satu `gptimer` 1 MHz
   berjalan bebas untuk seluruh sistem (`cdi_timebase.c`). Busi dijadwalkan
   sebagai alarm hardware pada *tick* absolut (`due = now + delay`), bukan
   "tunggu sekian mikrodetik dari sekarang" — sehingga keterlambatan
   penjadwalan ISR tidak ikut menggeser waktu tembak selama alarm sempat
   dipasang sebelum waktu targetnya tiba.
2. **ISR dikunci cache-safe.** `CONFIG_GPTIMER_ISR_CACHE_SAFE` dan
   `CONFIG_MCPWM_ISR_CACHE_SAFE` memastikan ISR jalur pengapian tetap
   presisi walau CPU sedang menjalankan operasi flash (commit NVS, tulis
   OTA) di core lain.
3. **Isolasi prioritas interrupt.** GPIO ISR pickup didaftarkan dengan
   `ESP_INTR_FLAG_IRAM | ESP_INTR_FLAG_LEVEL3`.
4. **Dynamic Frequency Scaling dimatikan total** (`CONFIG_PM_ENABLE=n`),
   CPU dikunci 240 MHz permanen, supaya resolusi tick tidak pernah melenceng
   akibat transisi sleep/frequency scaling.

**PENTING — batasan yang jujur:** poin 1–4 di atas adalah desain yang secara
teori seharusnya menghasilkan jitter rendah, TAPI **belum ada satu pun angka
jitter aktual yang terukur di hardware sungguhan** sampai dokumen ini
ditulis. Jangan anggap "arsitekturnya benar" sama dengan "sudah terbukti
presisi" — dua hal berbeda. Gunakan §3.1 atau osiloskop untuk benar-benar
mengukurnya sebelum percaya penuh.

### 3.1 Mengukur jitter tanpa osiloskop (`cdi_selftest.c`)

**Status: aktif secara default** — `cdi_selftest_init()` sudah dipanggil dari
`main.c`. Modul ini menjadikan ESP32 sendiri sebagai alat ukur, untuk yang
tidak punya osiloskop. Prinsipnya: `cdi_timebase.c` sudah tahu persis tick
target (`due`) kapan gate CENTER seharusnya menyala. Dengan kabel jumper
loopback dari `GPIO25` (gate center) ke `GPIO5` (input bebas), ESP32 bisa
menangkap kapan edge itu **sungguh-sungguh** terjadi (diukur dari jam yang
sama, `cdi_timebase_now()`) dan menghitung sendiri selisihnya — tanpa
instrumen eksternal apa pun.

Cara pakai:
1. Jumper `GPIO25 -> GPIO5`.
2. Suntikkan pulsa pickup simulasi ke `GPIO4` (lihat peringatan tegangan
   di §1 — cek amplitudo dulu, pakai pembagi tegangan kalau perlu, mis.
   10kΩ/15kΩ untuk sumber 5V menjadi ~3.0V).
3. `idf.py monitor`, baca baris:
   ```
   cdi_selftest: n=50  rata2=142us  min=138us  max=147us  JITTER(spread)=9us
   ```
   `rata2` adalah bias tetap (latensi ISR alarm + `gpio_set_level`, wajar
   ada). `JITTER(spread)` (selisih max-min) adalah angka yang sebenarnya
   dicari — idealnya sub-mikrodetik sampai beberapa mikrodetik saja di
   berbagai titik RPM simulasi.

File `cdi_selftest.h`/`cdi_selftest.c` ada di `main/`, dipanggil otomatis dari
`main.c`. Aman ditinggal aktif di build produksi (pin `GPIO5` idle tanpa
jumper tidak mengganggu operasi normal), tapi untuk build final yang dipasang
permanen di motor, comment baris `cdi_selftest_init();` di `main.c` supaya
task pelapor & ISR loopback tidak berjalan sia-sia, dan pastikan kabel
jumpernya sudah dilepas.

## 4. Mitigasi Brownout

Pada CDI otomotif, tegangan aki bisa anjlok ke 6–8V saat starter menyala,
sementara radio BLE ESP32 ikut menarik lonjakan arus sesaat.

**Koreksi penting (bug yang pernah ada di versi sebelumnya):** README lama
menyatakan `BROWNOUT_DET_LVL_SEL_7` = "~2.43V, ambang terendah/paling
toleran". Itu **terbalik**. Berdasarkan teks Kconfig resmi ESP-IDF:

```
LVL_SEL_0 = 2.43V  <- PALING TOLERAN (yang sebenarnya kita mau)
LVL_SEL_1 = 2.48V
LVL_SEL_2 = 2.58V
LVL_SEL_3 = 2.62V
LVL_SEL_4 = 2.67V
LVL_SEL_5 = 2.70V
LVL_SEL_6 = 2.77V
LVL_SEL_7 = 2.80V  <- PALING SENSITIF (kebalikan dari yang diinginkan)
```

Level 7 justru yang PALING GAMPANG trip. `sdkconfig.defaults` sudah
diperbaiki ke `CONFIG_ESP_BROWNOUT_DET_LVL_SEL_0=y`, dan **sudah
diverifikasi langsung lewat isi `sdkconfig` hasil generate** (bukan cuma
`.defaults`) menunjukkan `CONFIG_ESP_BROWNOUT_DET_LVL=0` benar-benar
terpasang.

Level 0 ini **sudah ambang paling toleran yang tersedia di silikon ESP32**.
Kalau brownout masih terjadi di level ini, rel 3.3V memang ambruk secara
fisik — perbaikan sesungguhnya ada di hardware, bukan di angka software:

**Solusi software yang sudah diterapkan:**
- BOD dikunci level 0 (lihat di atas).
- *Safety cut-off* memblokir koil kalau VBAT di luar rentang 9.5–16V.
- Pencatatan alasan reset (`esp_reset_reason()`) untuk memastikan restart
  yang terjadi memang brownout, bukan panic/watchdog (dua hal itu butuh
  perbaikan yang sama sekali berbeda).

**Persyaratan hardware yang wajib dipenuhi pengguna (software tidak bisa
menggantikan ini):**
- **Regulator Buck DC-DC** (mis. MP1584), BUKAN LDO linear (AMS1117) yang
  akan ambruk duluan saat aki drop.
- **Kapasitor Bulk 470–1000 µF** di jalur input 12V, **Kapasitor Output
  100–470 µF** sedekat mungkin ke pin 3V3 ESP32.
- **Ground Star-Point**: pisahkan ground koil/charger HV (kotor) dari
  ground logic ESP32.

## 5. Prosedur Uji Bangku (Wajib Sebelum Instalasi di Motor)

Build sukses **bukan berarti siap pasang ke motor**. Urutan validasi:

1. **Flash & cek reset reason.** `idf.py flash monitor`, pastikan baris
   pertama log menunjukkan `power-on normal`, bukan brownout/panic/watchdog,
   dan tidak ada reboot-loop dalam 10 detik pertama.
2. **Scan BLE dengan aplikasi generik (nRF Connect/LightBlue) dulu**,
   sebelum aplikasi Android NS200 asli. Pastikan nama advertising
   `NS200-CDI` muncul dan lima characteristic (TELEM/COMMAND/RESPONSE/
   OTA_DATA/OTA_STATUS) terlihat dengan UUID `7a8f1000...1005`. Ini baru
   membuktikan GATT table benar-benar jalan di silikon, bukan cuma compile
   bersih.
3. **Uji tanpa mesin dulu.** Pickup diam, pastikan tidak ada gate
   CENTER/SIDE yang menyala sendiri, telemetry menunjukkan RPM=0.
4. **Simulasi sensor & ukur jitter** — pakai osiloskop kalau ada (picu di
   tepi naik pulser, amati stabilitas tepi naik gate output di beberapa
   RPM simulasi), atau pakai modul `cdi_selftest.c` di §3.1 kalau tidak
   punya osiloskop.
5. **Uji charger HV tanpa beban dulu** — ukur duty cycle & dead-time
   dengan osiloskop sebelum disambung ke kapasitor/koil HV sungguhan.
6. **Uji OTA dengan data dummy kecil**, BUKAN firmware asli — memverifikasi
   jalur `esp_ota_write()` tidak crash tanpa risiko mem-brick board kalau
   ada bug.
7. **Stress test radio & brownout.** Hubungkan BLE, turunkan voltase PSU
   bertahap mendekati 7V, pastikan tidak reset saat radio memancarkan
   telemetri.
8. **Uji transisi ke kendaraan sungguhan** — baru setelah 1–7 semua lolos.
   Rekam kurva `OEM_LEARN` (pastikan PB3/PB4 lewat optocoupler), lalu
   validasi limiter `FIRST_START` (maks 3000 RPM).

## 6. Sisa Pekerjaan

- **Kalibrasi ADC fisik**: rasio resistor pembagi untuk HV (285–345V) dan
  VBAT harus diverifikasi manual pakai multimeter akurat. Skema kalibrasi
  software pakai `adc_cali_create_scheme_line_fitting` (ESP-IDF, bukan
  "curve fitting" — itu skema untuk chip lain seperti S2/S3, bukan ESP32
  klasik) sudah terpasang di `cdi_board_esp32.c`, tapi itu cuma
  mengoreksi non-linearitas ADC internal, bukan mengoreksi rasio pembagi
  tegangan fisik di board kamu.
- **Verifikasi BLE/OTA fungsional** (§5 poin 2 & 6) — belum dilakukan
  sampai dokumen ini ditulis.
- **Pengukuran jitter aktual**: alat (`cdi_selftest.c`, §3.1) sudah aktif dan
  siap pakai, tapi sesi pengukuran sungguhan dengan pickup simulasi belum
  dilakukan sampai dokumen ini ditulis — lihat §5 langkah 4.
- **Merge branch `codex/r9-universal-firmware` ke `main`**: keputusan
  terbuka, lihat catatan di §0. Butuh koordinasi dengan update app Android
  (`CDI_STM32_Android`) karena breaking change protokol BLE.

## 7. Log Verifikasi

Riwayat singkat apa yang sudah benar-benar dicek (bukan diasumsikan), untuk
transparansi:

- Diff `cdi_r5.c`/`.h` vs sumber STM32: hanya penambahan `IRAM_ATTR`,
  logika tidak berubah. 10 file lain byte-identik.
- `host_test.sh` (test suite asli STM32) dijalankan memakai `cdi_r5.c`
  versi repo ini: seluruh test lulus.
- Build penuh (`idf.py build`) sukses di ESP-IDF v6.1, termasuk
  `cdi_ble_nimble.c` — bootloader + `ns200_cdi_esp32.bin` ter-generate,
  tidak ada error compile/link.
- `cdi_r5_make_decision()` dikonfirmasi benar-benar mengecek
  `engine->output_permission` secara internal (baris ~243 `cdi_r5.c`) —
  interlock keselamatan bukan sekadar dekoratif di lapisan ESP32.
- Bug level brownout terbalik ditemukan (README lama vs teks Kconfig
  resmi), diperbaiki di `sdkconfig.defaults`, dan **dikonfirmasi ulang**
  lewat isi `sdkconfig` hasil generate menunjukkan `LVL_SEL_0`/`LVL=0`
  benar-benar terpasang (setelah menghapus `sdkconfig` lama yang masih
  menyimpan nilai salah).
- Modul `cdi_selftest.c`/`.h` ditambahkan, ter-link sukses ke build.
- Build penuh dijalankan ulang di toolchain ESP-IDF v6.1 sungguhan (bukan cuma
  di sandbox verifikasi ini) — sukses, `ns200_cdi_esp32.bin` ter-generate.
- Nilai `sdkconfig` hasil generate dicek langsung (`findstr`) dan dikonfirmasi
  `CONFIG_ESP_BROWNOUT_DET_LVL_SEL_0=y` benar-benar aktif (bukan cuma tertulis
  di `.defaults`) setelah `sdkconfig` lama yang masih menyimpan nilai salah
  dihapus dan di-generate ulang.
- Beberapa nama opsi Kconfig ternyata berbeda antara ESP-IDF v5.3 (dipakai
  saat menulis draf awal `sdkconfig.defaults`) dan v6.1 (yang sungguhan
  dipakai) — `*_ISR_IRAM_SAFE` menjadi `*_ISR_CACHE_SAFE`,
  `CONFIG_ESP32_DEFAULT_CPU_FREQ_*` menjadi `CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ*`.
  `sdkconfig.defaults` sudah diperbarui pakai nama v6.1.
- **Seluruh rangkaian patch (`sdkconfig.defaults`, `main/cdi_selftest.h`,
  `main/cdi_selftest.c`, `main/cdi_engine_esp32.c`, `main/CMakeLists.txt`,
  `main/main.c`) sudah di-push ke branch `main` repo ini dan diverifikasi
  ulang lewat `raw.githubusercontent.com` — isinya cocok byte-per-byte
  dengan yang disiapkan di sesi ini, termasuk potongan terakhir
  (`cdi_selftest_init()` di `main.c`) yang sempat tertinggal di push pertama.**
- Branch `codex/r9-universal-firmware` ditemukan di repo ini maupun repo
  STM32 (`Firmware_CDI_NS200`), diverifikasi `cdi_r5.c` identik di kedua
  repo, dan diverifikasi belum di-merge ke `main` di keduanya.
- **Belum dilakukan**: flash ke board fisik, scan BLE sungguhan, pengukuran
  jitter aktual lewat `cdi_selftest` dengan pickup simulasi, uji OTA data
  dummy, uji charger HV dengan osiloskop, kalibrasi ADC manual.