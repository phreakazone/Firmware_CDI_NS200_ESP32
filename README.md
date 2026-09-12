# Porting NS200 CDI R8: STM32WB55 → ESP32-WROOM-32

## 0. Baca ini dulu — cakupan pekerjaan yang jujur

Ini **bukan** "kompilasi ulang untuk chip lain". STM32WB55 dan ESP32 tidak
berbagi satu pun peripheral yang sama (timer, ADC, BLE stack, tata letak
flash), jadi setiap baris yang menyentuh hardware ditulis ulang dari nol.
Yang bisa — dan memang — dipindahkan **apa adanya** hanyalah logika yang
sudah tidak bergantung ke STM32 sama sekali:

| Bagian | Status | Bukti |
|---|---|---|
| `cdi_r5.c` — interpolasi peta, limiter, keputusan pengapian | **Diporting verbatim** | `host_test.sh` asli lulus 100% dengan `gcc` biasa sebelum disalin |
| `cdi_r5_charger.c` — regulasi HV charger | **Diporting verbatim** | idem |
| `cdi_r5_protocol.c` — parser perintah ASCII | **Diporting verbatim** | idem |
| `cdi_r5_ble.c` — encode telemetry, dispatch command | **Diporting verbatim** | idem |
| `cdi_r8_oem_learn.c` / `cdi_r8_ota.c` | **Diporting verbatim** | idem |
| Capture pickup, penjadwalan busi, ADC, PWM charger | **Ditulis ulang total** untuk ESP32 | `cdi_timebase.c`, `cdi_board_esp32.c`, `cdi_engine_esp32.c` |
| BLE (NimBLE, UUID sama) | **Ditulis ulang**, pola standar ESP-IDF | `cdi_ble_nimble.c` — **belum pernah dikompilasi terhadap SDK sungguhan** di lingkungan pembuatan file ini (tidak ada toolchain Xtensa di sandbox). Kemungkinan ada nama makro/field yang perlu disesuaikan ke versi ESP-IDF Anda. |
| OTA lewat BLE | **Tidak diporting** — beda model flash total. Karakteristik didaftarkan tapi menolak halus. Gunakan `esp_ota_ops`/`esp_https_ota` bawaan ESP-IDF. |
| Bootloader custom (dual-bank, USB DFU) | **Tidak relevan** — ESP-IDF sudah punya bootloader + OTA sendiri. |

Saya sudah menjalankan `host_test.sh` dari repo asli di dalam sandbox ini
sebelum menyalin apa pun — semua 6 suite lulus (`cdi_r5`, `cdi_r5_charger`,
`cdi_r5_protocol`, `cdi_r5_ble`, `cdi_r8_oem_learn`, `cdi_r8_ota`). Itu
sebabnya bagian tabel di atas yang saya tandai "verbatim" saya percaya
penuh — bagian *hardware baru* (timebase, ISR, board bring-up, BLE) saya
tulis dengan hati-hati dan penjelasan di setiap desain, tapi **wajib** diuji
di bangku (lihat §5) sebelum dipasang ke motor sungguhan. ESP32 dengan port
yang salah bisa memicu busi di sudut yang salah — itu bisa merusak mesin
(kickback ke starter, detonasi) sebelum sempat dikoreksi.

## 1. Peta pin (ESP32-WROOM-32 DevKit) — lihat `main/cdi_board_esp32.h`

```
PICKUP utama (capture)  -> GPIO4    (pengganti PA0/TIM2-CH1)
OEM tap CENTER          -> GPIO16   (pengganti PB3/EXTI3, hanya OEM_LEARN)
OEM tap SIDE            -> GPIO17   (pengganti PB4/EXTI4, hanya OEM_LEARN)
Gate CENTER (ke driver) -> GPIO25   (pengganti PA1)
Gate SIDE (ke driver)   -> GPIO26   (pengganti PA2)
Strobo manual           -> GPIO27   (pengganti PB9)
Fault hardware (aktif-L)-> GPIO14   (pengganti PA10)
Relay kipas             -> GPIO13   (pengganti PB5)
Charger push-pull A/B   -> GPIO18/19 (pengganti PA9/PB8, lewat MCPWM+dead-time)
ADC TPS/TEMP/TPS_REF/HVC/HVS/VBAT -> GPIO36/39/34/35/32/33 (ADC1 saja — ADC2 dihindari, konflik dgn WiFi)
```

**PERINGATAN KERAS, SAMA SEPERTI CATATAN DI REPO ASLI:** pin pickup dan
kedua tap OEM **bukan** input 12 V atau 285–345 V. Semuanya harus lewat
rangkaian conditioner/isolator berimpedansi tinggi yang sama seperti yang
disebut README asli (opto-isolator atau pembagi tegangan + clamp + buffer
Schmitt-trigger) sebelum menyentuh GPIO ESP32. ESP32 adalah IC 3.3 V murni
— tanpa proteksi, satu pulsa dari primer koil (bisa ratusan volt) akan
menghancurkannya seketika, dan berpotensi membalik tegangan ke jalur lain.

## 2. Build

Proyek ini pakai ESP-IDF (bukan Arduino) karena butuh akses langsung ke
`gptimer`, `mcpwm`, dan alokasi interrupt level tinggi + IRAM — hal-hal yang
tersedia tapi lebih rumit dikontrol presisinya lewat framework Arduino.

```sh
idf.py set-target esp32
idf.py menuconfig   # verifikasi opsi di sdkconfig.defaults benar-benar terpasang
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

## 3. Jitter — kenapa ESP32 sering "meleset derajat pengapian", dan apa yang dibenahi di sini

Root cause jitter di kebanyakan port CDI/ECU ke ESP32 hampir selalu salah
satu dari lima hal ini, dan port ini membenahi kelimanya di level arsitektur
(bukan sekadar "menambah delay compensation"):

1. **Penjadwalan lewat software (`delay()`/`vTaskDelay`/polling loop)
   alih-alih hardware compare.** STM32 asli menangkap pulsa pickup di
   hardware (input capture TIM2-CH1) lalu menjadwalkan penyalaan busi juga
   di hardware (output-compare TIM2-CH2/3/4) pada counter yang **sama**.
   Port ini meniru pola itu lewat `cdi_timebase.c`: satu `gptimer` 1 MHz
   free-running, pickup dibaca via `gptimer_get_raw_count()` di dalam ISR,
   dan busi dijadwalkan sebagai alarm hardware pada tick absolut
   `now + delay` — bukan lewat `vTaskDelay` (resolusi ~1 ms, terlalu kasar)
   atau lewat polling di loop utama (jitter bisa puluhan ms).

2. **ISR bukan di IRAM → cache-miss flash stall.** Ini penyebab jitter
   paling umum dan paling sering luput: kalau kode ISR ada di flash (bukan
   IRAM) dan cache instruksi meleset saat itu terjadi bersamaan operasi
   flash lain (NVS commit, dsb.), ISR bisa tertunda puluhan mikrodetik
   sampai *milidetik*. Semua fungsi di jalur panas — `pickup_isr`,
   callback slot timebase, `cdi_r5_make_decision`, `cdi_r5_map_interpolate`
   — ditandai `IRAM_ATTR` (lihat `cdi_r5.h`, shim `CDI_R5_IRAM`). Aturan
   "jangan menulis NVS/flash selagi mesin hidup" dari firmware asli
   **dipertahankan persis** di `cdi_engine_esp32.c` sebagai lapisan
   pertahanan kedua.

3. **WiFi/BT coexistence.** Radio ESP32 klasik memakai ISR prioritas
   tinggi yang bisa menunda interrupt lain beberapa puluh mikrodetik saat
   ada aktivitas radio. Mitigasi di port ini:
   - WiFi **tidak diinisialisasi sama sekali** (tidak dipanggil di mana
     pun) — hanya NimBLE (BLE saja, bukan BT klasik) yang aktif, dan itu
     pun dengan jejak radio jauh lebih kecil dari WiFi.
   - ISR pickup dipasang dengan `ESP_INTR_FLAG_LEVEL3` (lebih tinggi dari
     level default banyak driver) supaya lebih sulit digeser.
   - Task 1 kHz (ADC/charger/telemetry, non-time-critical) di-pin ke
     **core 1**, terpisah dari core 0 tempat stack BLE biasanya berjalan.
   - **Realistis:** kalau butuh keandalan mikrodetik yang absolut (balap,
     RPM sangat tinggi dengan banyak silinder), opsi paling robus adalah
     MCU khusus real-time (STM32 kecil/RP2040/AVR) hanya untuk capture +
     fire, dan ESP32 dipakai murni untuk BLE/logging/konfigurasi,
     berkomunikasi lewat UART/SPI. Ini disebutkan sebagai upgrade path,
     bukan diimplementasikan di sini, karena di luar cakupan "migrasi ke
     satu chip ESP32" yang diminta.

4. **Dynamic frequency scaling / light sleep.** `sdkconfig.defaults`
   mematikan `CONFIG_PM_ENABLE` total — clock APB/CPU tetap 240 MHz kapan
   pun, tidak ada transisi frekuensi yang mengubah resolusi tick atau
   menambah latensi bangun-dari-sleep.

5. **Symbol IRAM Kconfig untuk driver GPIO/GPTimer belum diaktifkan.**
   `sdkconfig.defaults` sudah menyalakan opsi-opsi yang membuat
   `gpio_set_level`, `gpio_get_level`, dan pembacaan raw count `gptimer`
   ikut IRAM-resident. **Nama simbol ini berubah antar versi ESP-IDF** —
   kalau `idf.py build` komplain simbol tidak dikenal, buka
   `idf.py menuconfig`, cari kata "IRAM", dan cocokkan manual.

**Upgrade lanjutan (opsional, tidak diimplementasikan di sini):** ganti
capture berbasis GPIO-ISR+`gptimer_get_raw_count()` dengan peripheral
**MCPWM Capture** ESP32 (`driver/mcpwm_cap.h`), yang mem-*latch* timestamp
tepi pulsa di hardware — timestamp itu sendiri jadi nol-jitter walau ISR
yang membacanya telat beberapa mikrodetik. Ini level presisi di atas apa
yang dibutuhkan CDI motor 1–2 silinder, tapi relevan kalau suatu saat mau
dipakai untuk mesin RPM sangat tinggi/banyak silinder.

## 4. Brownout — kenapa ESP32 reset sendiri saat digas/starter

Brownout pada CDI motor **hampir selalu masalah catu daya**, bukan
murni bug software, karena dua hal terjadi bersamaan persis saat gejala
muncul:

- **Starter menyedot arus besar** dari aki 12 V → tegangan aki bisa
  ambles ke 6–8 V selama ratusan milidetik saat cranking.
- **ESP32 sendiri menarik arus burst** (hingga ratusan mA saat radio BLE
  TX) yang perlu disuplai *seketika* dari kapasitor lokal — kalau
  decoupling-nya kurang, tegangan 3V3 ambles sesaat meski suplai 12 V-nya
  baik-baik saja.

### Yang sudah dibenahi di software (`sdkconfig.defaults`)

- Brownout detector diturunkan ke ambang **terendah yang tersedia**
  (`BROWNOUT_DET_LVL_SEL_7`, ~2.43 V di rel 3V3 RTC) supaya dip transien
  singkat yang masih aman tidak memicu reset "hantu".
- `main.c` mencatat setiap reset akibat brownout ke NVS
  (`nvs: cdi_diag/bod_count`) dan mencetaknya ke log saat boot, supaya
  Anda punya data kuantitatif ("sudah berapa kali brownout sejak
  dipasang") alih-alih menebak.
- Aturan `battery_ok` (9.5–16 V) dari firmware asli dipertahankan — output
  pengapian otomatis dikunci mati kalau tegangan aki di luar rentang itu,
  jadi kalaupun terjadi sag yang belum sampai memicu brownout MCU, sistem
  tidak memaksa menyalakan busi dengan HV yang tidak stabil.

**Menurunkan ambang BOD BUKAN solusi utama** — itu cuma mengurangi
kejadian *nuisance reset*. Kalau board masih brown-out di ambang
terendah, akar masalahnya ada di hardware, dan itu **wajib** dibenahi:

### Yang wajib dibenahi di hardware (di luar cakupan kode)

1. **Regulator buck** (bukan LDO linear seperti AMS1117) dengan rentang
   input lebar (6–40 V) supaya tetap bisa meregulasi bahkan saat aki
   ambles ke ~6–7 V saat cranking. LDO linear akan drop-out lebih dulu.
2. **Kapasitor bulk besar di sisi input regulator** (elco low-ESR
   470–1000 µF otomotif, rating suhu -40…105 °C) untuk menahan (bridging)
   sag ratusan-ms saat starter berputar, ditambah keramik 10 µF + 100 nF
   dekat pin input untuk noise frekuensi tinggi.
3. **Kapasitor output cukup besar dekat pin 3V3 ESP32** (100–470 µF +
   keramik 1–10 µF sedekat mungkin ke modul) — ini yang paling sering
   diabaikan dan jadi penyebab brownout bahkan tanpa starter menyala,
   karena burst arus TX radio ESP32 butuh sumber arus cepat lokal.
4. **TVS diode + dioda anti-polaritas-balik** di jalur 12 V masuk, untuk
   meredam *load dump* alternator (bisa spike 40–80 V pada motor).
5. **Ground star-point**: pisahkan ground driver ignition coil / switching
   charger 100 kHz (arus tinggi, berisik) dari ground analog/logic ESP32,
   satukan hanya di satu titik. Ground bouncing dari sisi HV/koil yang
   ikut ke rel 3V3 adalah penyebab umum brownout maupun ADC yang liar.
6. Kalau starter motor menyebabkan sag ekstrem (<6 V) secara rutin, opsi
   kelas atas adalah pre-regulator **buck-boost/SEPIC**, yang tetap bisa
   meregulasi output walau input turun di bawah target output —
   pendekatan yang umum dipakai ECU otomotif profesional untuk "cold
   crank survival".

## 5. Rencana uji bangku (WAJIB sebelum dipasang ke motor)

1. **Tanpa HV, tanpa koil.** Simulasikan pickup dengan signal generator
   (gelombang kotak 3.3 V, frekuensi setara idle–redline) ke pin
   `CDI_PIN_PICKUP_CENTER` lewat conditioner yang sama seperti nanti di
   motor. Amati `GPIO25`/`GPIO26` dengan osiloskop atau logic analyzer.
2. **Ukur jitter aktual**: trigger scope pada tepi pickup, ukur variasi
   waktu ke tepi naik gate CENTER pada RPM tetap selama beberapa ratus
   siklus. Ini angka yang menjawab pertanyaan "apakah fix ini cukup" —
   jangan asumsikan dari teori saja.
3. **Uji brownout dengan sengaja**: turunkan suplai bertahap sambil
   memantau log reset reason, dan sambil membebani radio BLE (koneksi
   aktif + notifikasi telemetry jalan) untuk mensimulasikan beban starter
   + beban radio bersamaan.
4. **Uji watchdog**: pastikan `esp_task_wdt` benar-benar me-reset board
   kalau task 1 kHz macet (mis. sengaja `while(1);` sebentar di build
   debug) — jangan sampai baru ketahuan saat motor jalan.
5. Baru setelah 1–4 stabil, lanjut ke `OEM_LEARN` dengan CDI OEM asli
   tetap terpasang (persis alur di README asli), lalu `FIRST START`
   dengan pengaman map konservatif seperti firmware asli.

## 6. Yang belum saya sertuh (perlu kerja lanjutan)

- **Kalibrasi ADC** untuk pembagi tegangan HV (285–345 V) dan VBAT: ESP32
  dan STM32 sama-sama ADC 12-bit 0–3.3 V, tapi linearitas dan referensi
  keduanya tidak identik. `cdi_board_esp32.c` sudah memakai skema
  kalibrasi kurva ESP-IDF (`adc_cali_curve_fitting`), tapi pembagi
  tegangan fisik (rasio resistor) tetap harus diverifikasi ulang dengan
  multimeter/sumber HV terkendali sebelum dipercaya untuk regulasi charger.
- **OTA**: lihat catatan di `cdi_ble_nimble.c` — perlu dirancang terpisah
  di atas `esp_ota_ops`/`esp_https_ota`, idealnya tetap dengan gerbang
  keselamatan yang sama (`RPM == 0`, output OFF, HV < 30 V) seperti
  `cdi_r8_ota.c` asli.
- **NimBLE**: perlu dikompilasi & diverifikasi terhadap versi ESP-IDF
  target Anda; ada kemungkinan penyesuaian kecil pada nama field API.
