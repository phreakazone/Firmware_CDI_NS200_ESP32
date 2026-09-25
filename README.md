# IgniTra CDI ESP32 R9 Modular

Firmware ESP-IDF untuk IgniTra CDI R9 berbasis ESP32-WROOM-32 DevKit 38-pin. NS200 dipertahankan sebagai preset awal dan kendaraan uji; dukungan lintas kendaraan ditentukan oleh konfigurasi mesin, bukan nama produk. Core menjalankan satu kanal CDI CENTER; SIDE, THERMAL/FAN, OEM Learn, AUX, TPS Diagnostic, dan EXP adalah modul opsional.

README ini adalah dokumentasi tunggal repository firmware: sumber firmware, kontrak aplikasi, pin/net, header modul, BOM, setup, build, uji dan keselamatan.

## Status rilis

| Item | Nilai dari source |
|---|---|
| Release | R9 |
| Semantic version | 9.6.3 |
| Build ID | 20260925 |
| Platform | ESP32 klasik/WROOM-32 |
| Protocol command | 5 |
| Telemetry | v3, 20 byte |
| Advertising BLE | NS200-CDI (identitas legacy kompatibel; produk tetap IgniTra CDI R9) |
| Map | 4 slot, maksimum 32×16 |
| Batas format RPM | 30.000 RPM |
| Batas advance | -30,0° sampai +80,0° |
| PPR maksimum | 12 |
| Target HV yang dapat diprogram | 180–345 V |

Nilai source pada `main/cdi_r5_protocol.h` dan capability runtime adalah acuan, bukan nama folder atau dokumen lama.

## Kontrak final dan ruang lingkup lintas kendaraan

- Core Rev C dibekukan sebagai battery-powered CDI lintas kendaraan 9,5–16 V, satu pickup terproteksi, satu atau dua output CDI, PPR 1–12, empat map, dan load 0–100% yang saat ini berasal dari TPS.
- Nilai map, limiter, advance, live tune, profil idle, dan mode kendaraan dipilih pengguna. Firmware tidak memakai lock berdasarkan merek/tipe motor.
- Batas yang tidak dapat dimatikan hanya batas listrik/fisik: target charger maksimum 345 V, format advance −30°…+80°, advance tidak dapat mendahului sudut pickup, hardware FAULT_N, proteksi input, dan one-shot starter.
- J1 dan seluruh JMOD Rev C tidak boleh dipetakan ulang untuk fitur baru. Sensor MAP/knock/multi-input memakai modul pintar I²C; input berlatensi rendah memakai GPIO27/STROBE melalui modul terproteksi; fault eksternal memakai FAULT_N.
- U6/PCF8574 dipakai untuk deteksi dan kontrol lambat, bukan quickshifter atau trigger presisi karena polling 20 ms tidak deterministik untuk event pengapian.
- Scope ini bukan ECU injeksi dan bukan TCI multi-coil. Kebutuhan tersebut harus menjadi modul pintar atau produk lain, bukan revisi pin Core.

Kontrak pin, protokol additive, schema telemetry, dan envelope listrik di atas adalah design freeze. Perbaikan bug firmware berikutnya tidak boleh memaksa perubahan PCB atau harness.

## Source yang dikompilasi

Entry point: `main/main.c`.

| File | Fungsi |
|---|---|
| `cdi_board_esp32.*` | GPIO, ADC1, PWM charger dan akses board |
| `cdi_timebase.*` | penjadwalan pengapian GPTimer |
| `cdi_engine_esp32.*` | ISR pickup, CENTER/SIDE, charger, fan, telemetry |
| `cdi_r5.*` | map, limiter dan keputusan pengapian |
| `cdi_r5_charger.*` | kontrol charger HV dan target tegangan |
| `cdi_r5_protocol.*` | command protocol v5 dan commissioning |
| `cdi_r5_ble.*`, `cdi_ble_nimble.*` | GATT dan telemetry v3 |
| `cdi_module_io.*` | PCF8574 U6/U7, DET dan AUX I/O |
| `cdi_timing_modes.*` | STANDARD sampai CUSTOM |
| `cdi_r8_oem_learn.*` | OEM Learn |
| `cdi_r8_ota.*` | OTA |
| `cdi_selftest.*` | uji servis; tidak boleh aktif permanen di kendaraan |

Folder `build/` adalah hasil ESP-IDF dan tidak disimpan di Git. `sdkconfig.old`, log, ELF, MAP dan binary lokal juga bukan sumber kebenaran.

## GPIO ESP32

Gunakan nama net dan GPIO; nomor header fisik DevKit harus dicocokkan dengan board 38-pin yang benar sebelum membuat footprint.

| GPIO | Net | Arah/fungsi |
|---:|---|---|
| 36 / ADC1_CH0 | TPS_ADC | TPS utama |
| 39 / ADC1_CH3 | TEMP_ADC | suhu opsional |
| 34 / ADC1_CH6 | TPS_REF_ADC | referensi TPS diagnostic |
| 35 / ADC1_CH7 | HV_C_ADC | feedback HV CENTER |
| 32 / ADC1_CH4 | HV_S_ADC | feedback HV SIDE |
| 33 / ADC1_CH5 | VBAT_ADC | tegangan aki |
| 25 | GATE_C | SCR CENTER |
| 26 | GATE_S | SCR SIDE |
| 27 | STROBE | output strobe |
| 14 | FAULT_N | fault aktif-rendah |
| 13 | FAN_CTL | driver coil relay fan |
| 23 | AUDIO_PWM | reserved, ditahan LOW |
| 19 | PWM_B | TC4427 kanal B |
| 18 | PWM_A | TC4427 kanal A |
| 5 | BENCH_LOOP | test pad, bukan jumper permanen |
| 17 | OEM_SIDE | PC817 aktif-rendah |
| 16 | OEM_CENTER | PC817 aktif-rendah |
| 4 | PICKUP_DIG | keluaran comparator pickup |
| 21 | MOD_I2C_SDA | U6/U7 PCF8574P |
| 22 | MOD_I2C_SCL | U6/U7 PCF8574P |

Semua input analog memakai ADC1 karena ADC2 tidak stabil ketika BLE aktif. Catu DevKit adalah 5V_LOGIC melalui jumper power dan 3V3 dari regulator board. GND_LOGIC dan GND_POWER hanya bertemu pada titik star/net-tie yang ditentukan skematik.

## Harness utama J1

Header J1 2×6 memakai kolom kiri 1–6 dan kanan 7–12.

| J1 | Net | Fungsi |
|---:|---|---|
| 1 | KEYLESS_REQ | pulsa +12 V terproteksi untuk wake/toggle keyless |
| 2 | TPS_A | sensor TPS |
| 3 | TEMP_SENSOR | sensor suhu |
| 4 | TPS_B | sensor TPS |
| 5 | IGN_12V | kontak mekanis/masukan daya normal |
| 6 | COIL_SIDE | keluaran coil SIDE |
| 7 | FAN_RELAY | coil relay fan OEM |
| 8 | START_REQ | dry-contact/open-collector ke GND_LOGIC |
| 9 | MODE_REQ / NEUTRAL_IN | profil manual memakai netral aktif-rendah |
| 10 | PICKUP_RAW | pickup mesin |
| 11 | GND_STAR | return daya kendaraan |
| 12 | COIL_CENTER | keluaran coil CENTER |

Pada harness NS200 standar, J1.1/J1.8/J1.9 tetap NC sampai kabel tambahan dipasang. J1.5 tidak boleh diubah menjadi ground atau status keyless sintetis: firmware membaca kontak mekanis secara terpisah melalui U7/P3.

## Header modul Rev C

Core memakai male THT dan modul memakai female 2,54 mm; gunakan female long-tail hanya bila benar-benar ditumpuk. Header 2×6: kolom kiri 1–6 dan kanan 7–12. Header 2×3: baris 1–2, 3–4, 5–6.

### JMOD_SIDE 2×6

| Pin | Net | Pin | Net |
|---:|---|---:|---|
| 1 | BRIDGE_PLUS | 7 | VIN_FILT |
| 2 | SIDE_DET | 8 | GATE_S |
| 3 | COIL_SIDE | 9 | HV_S_FB |
| 4 | MOD_I2C_SDA | 10 | GND_POWER |
| 5 | GND_POWER | 11 | 3V3 |
| 6 | MOD_I2C_SCL | 12 | GND_LOGIC |

### JMOD_THERMAL 2×6

| Pin | Net | Pin | Net |
|---:|---|---:|---|
| 1 | VIN_PROT | 7 | TEMP_ADC |
| 2 | GND_POWER | 8 | FAN_CTL |
| 3 | 5V_LOGIC | 9 | FAN_RELAY |
| 4 | GND_LOGIC | 10 | THERMAL_DET |
| 5 | 3V3 | 11 | MOD_I2C_SDA |
| 6 | TEMP_SENSOR | 12 | MOD_I2C_SCL |

### JMOD_LEARN 2×3

| Pin | Net | Pin | Net |
|---:|---|---:|---|
| 1 | 3V3 | 2 | GND_LOGIC |
| 3 | OEM_CENTER | 4 | OEM_SIDE |
| 5 | GND_POWER | 6 | LEARN_DET |

### JMOD_AUX 2×6

| Pin | Net | Pin | Net |
|---:|---|---:|---|
| 1 | VIN_PROT | 7 | STROBE |
| 2 | GND_POWER | 8 | AUDIO_PWM |
| 3 | 5V_LOGIC | 9 | MOD_I2C_SDA |
| 4 | GND_LOGIC | 10 | MOD_I2C_SCL |
| 5 | 3V3 | 11 | EXP_IO1 |
| 6 | AUX_DET | 12 | EXP_IO2 |

### JMOD_TPS 2×3

| Pin | Net | Pin | Net |
|---:|---|---:|---|
| 1 | TPS_REF_RAW | 2 | 3V3 |
| 3 | GND_LOGIC | 4 | TPS_REF_ADC |
| 5 | TPS_DET | 6 | TPS_SIG_RAW |

### JMOD_EXP 2×6

| Pin | Net | Pin | Net |
|---:|---|---:|---|
| 1 | 3V3 | 7 | MOD_I2C_SDA |
| 2 | GND_LOGIC | 8 | MOD_I2C_SCL |
| 3 | 5V_LOGIC | 9 | EXP_IO2 |
| 4 | GND_POWER | 10 | EXP_IO3 |
| 5 | VIN_PROT | 11 | FAULT_N |
| 6 | EXP_IO1 | 12 | STROBE |

SIDE/THERMAL/LEARN/AUX/TPS wajib memasang resistor DET 1 kΩ dari DET ke GND_LOGIC. Core memakai pull-up 10 kΩ. Jangan menghubungkan DET ke GND_POWER.

## Deteksi modul dan AUX I/O

### U6 PCF8574P alamat 0x20

| Port | Fungsi |
|---:|---|
| P0 | SIDE_DET |
| P1 | THERMAL_DET |
| P2 | LEARN_DET |
| P3 | AUX_DET |
| P4 | TPS_DET |
| P5 | EXP_IO1 / keyless control |
| P6 | EXP_IO2 / starter trigger |
| P7 | EXP_IO3 |

DET aktif-rendah dan didebounce. Modul yang dilepas langsung hilang dari present/active; bila masih configured, firmware memberi fault/peringatan.

### U7 PCF8574P alamat 0x21

| Port | Fungsi |
|---:|---|
| P0 | KEYLESS_REQ_N |
| P1 | START_REQ_F |
| P2 | MODE_REQ_F / NEUTRAL_IN |
| P3 | IGN_SW_N, posisi kontak mekanis J1.5 |

Output relay harus safe-default OFF saat boot/I²C gagal. K2 starter wajib melewati NE555 one-shot; output PCF tidak boleh langsung menahan transistor relay.

Firmware ESP-IDF 6.1 memakai API `driver/i2c_master.h` dan komponen `esp_driver_i2c`; API legacy `driver/i2c.h` tidak digunakan. Bus berjalan 100 kHz, pull-up internal dimatikan karena Core menyediakan pull-up eksternal, filter glitch disetel 7 cycle, dan timeout transaksi 5 ms. U6/U7 tetap fail-safe: tiga kegagalan polling berturut-turut menghapus status present/request dan melepas izin output terkait.

## Blok penempatan PCB

| Blok | Isi utama | Aturan penempatan |
|---|---|---|
| C1 daya/harness | J1, reverse diode, TVS, L_IN, C_IN, MP1584, star | aliran J1→proteksi→filter→buck pendek |
| C2 logic/I²C | ESP32, U6, U7, pull-up, header modul | jauh dari trafo, SCR dan coil |
| C3 sensor | LM339, pickup, TPS, suhu, VBAT, clamp | tidak dilintasi PWM/HV/relay |
| C4 charger | TC4427, MOSFET, EE35, shunt, rectifier | loop arus pendek dan lebar |
| C5 CENTER HV | MKP, SCR, diode, divider feedback | satu zona HV ber-slot/keepout |
| SIDE HV | MKP, SCR, driver, feedback | creepage dari I²C/DET dan logic |
| THERMAL | conditioner suhu, TIP122, flyback | TIP122 hanya menggerakkan coil relay OEM |
| OEM Learn | resistor input dan PC817 | pisahkan sisi input dari logic |
| AUX | K1, K2, NE555, strobe, terminal | pisahkan relay/starter dari analog |

Net BRIDGE_PLUS, HV_CENTER, HV_SIDE, COIL_CENTER dan COIL_SIDE tidak boleh dirutekan di bawah ESP32, comparator, I²C atau input analog. Pitch header 2,54 mm tidak menggantikan creepage; gunakan slot, keepout dan coating sesuai lingkungan.

## Komponen utama dan alternatif

| Fungsi | Utama | Alternatif yang diperbolehkan |
|---|---|---|
| MCU | ESP32 DevKitC 38-pin | board 38-pin dengan pinout identik |
| Comparator | LM339N DIP-14 | LM2901N |
| Gate driver | TC4427AEPA DIP-8 | MIC4427, pinout diverifikasi |
| I/O expander | PCF8574P DIP-16 | PCF8574AP hanya jika alamat firmware diubah |
| Charger MOSFET | IRFB4110 | N-MOSFET 100 V, RDS(on) ≤8 mΩ, G-D-S |
| SCR | BT151-800R | BT152-800R setelah pinout/rating diperiksa |
| Relay AUX | Omron G8NB-1U 12 V | relay automotive PCB setara, footprint disesuaikan |
| Fan driver | TIP122 | TIP120/121 bila rating cukup |
| Optocoupler | PC817 | EL817 DIP-4 |
| Rectifier cepat | UF4007 | HER108/FR107 sesuai rating |
| Clamp THT | BAT85 | BAT43/1N5819 setelah leakage diperiksa |
| Kapasitor CDI | MKP 1 µF 630 V | CBB22 105J 630 V dengan pitch nyata |
| Header | 2×6/2×3 2,54 mm | female long-tail untuk stack-through |

Nilai generik seperti `68 Ω 1 W` boleh dipakai pada nama simbol, tetapi footprint harus mengikuti body dan pitch komponen yang dibeli. Untuk resistor 68 Ω 1 W gunakan axial body sekitar 9–11 mm/pitch sekitar 15,24 mm bila sesuai barang nyata.

## BOM pembelian ringkas

Jumlah berikut menyediakan cadangan solder/rework.

| Item | Jumlah beli |
|---|---:|
| 10 kΩ ¼ W metal-film | 30 |
| 1 kΩ ¼ W metal-film | 25 |
| 4,7 kΩ ¼ W | 15 |
| 100 kΩ ¼ W | 10 |
| 270 kΩ dan 470 kΩ ½ W | masing-masing 10 |
| 33 kΩ ½ W | 10 |
| 68 Ω 1 W | 2 |
| BAT85 | 20 |
| UF4007/HER108 | 12 |
| 1N4148 | 10 |
| BC337, 2N3904, 2N3906 | masing-masing 5 |
| TIP122 | 2 |
| IRFB4110 | 3 |
| IRLZ44N | 2 |
| BT151-800R | 3 |
| PC817/EL817 | 4 |
| LM339N/LM2901N | 2 |
| TC4427A/MIC4427 | 2 |
| PCF8574P | 3 |
| NE555P | 2 |
| G8NB-1U 12 V | 3 |
| MKP/CBB22 1 µF 630 V | 3 |
| Elco 470 µF 50 V 105 °C low-ESR | 2 |
| 100 nF dan 10 nF | masing-masing 15 |
| male/female 2×6 2,54 mm | masing-masing 4 |
| male/female 2×3 2,54 mm | masing-masing 3 |

AUX juga memerlukan QPRE safe-default, R timing 2,2 MΩ 1%, C timing 1 µF X7R ±20%, diode clamp, terminal 5,08 mm dan proteksi input request. Nominal one-shot K2 adalah `t = 1,1RC = 2,42 s`; worst-case R +1% dan C +20% sekitar 2,93 s.

## Kontrak BLE

### UUID

| Fungsi | UUID |
|---|---|
| Service | `7a8f1000-6c9d-4e40-a45f-0b4b4e533230` |
| Telemetry notify | `7a8f1001-6c9d-4e40-a45f-0b4b4e533230` |
| Command write | `7a8f1002-6c9d-4e40-a45f-0b4b4e533230` |
| Response notify | `7a8f1003-6c9d-4e40-a45f-0b4b4e533230` |
| OTA data | `7a8f1004-6c9d-4e40-a45f-0b4b4e533230` |
| OTA status | `7a8f1005-6c9d-4e40-a45f-0b4b4e533230` |

Frame command:

```text
@sequence,COMMAND,arg1,arg2*CRC16\n
```

CRC16-CCITT polynomial 0x1021, initial 0xFFFF, dihitung dari sequence sampai argumen terakhir. Queue aplikasi mengirim satu command sampai ACK/ERR/query response diterima.

### Respons penting

```text
INFO,ESP32,R9,5,3,IGNITRA_R9_MODULAR
VERSION,1,R9,9.5.0,20260925,ESP32,5,3
IDENTITY,1,<serial>,SERIAL_V1,LOCAL_APP,0
CAPS,7,30000,-300,800,32,16,4,12,...,UNIVERSAL_CDI,SIGNED_LIVE,HV_TARGET_MAX_345V
MODULES,2,presentMask,activeMask,observedMask,faultMask,coreProfile,configuredMask,ioOk
TIMING,2,mode,intensity,minRpm,maxRpm
AUX,3,keylessOn,starterOn,auxPresent,inputEnabled,profile,requestMask,requestIoOk,mechanicalOn,contactSource,engineRunning,ignitionAllowed
```

Bit MODULES: SIDE=1, THERMAL=2, OEM_LEARN=4, AUX=8, TPS_DIAG=16.

### Telemetry v3

Paket 20 byte little-endian, magic 0xCD15, sequence pada offset 4 dan CRC16 byte 0–17 pada offset 18.

- CORE: RPM, TPS permille, advance centidegree, aki centivolt, HV CENTER dan HV SIDE.
- DIAGNOSTIC: suhu, slot, limiter mode, flags, output flags, fault, trigger, pickup quality dan First Start timer.

UUID, paket v3 dan arti bit lama tidak boleh diubah diam-diam. Fitur baru harus additive, memakai schema, dan diiklankan CAPS.

`LIVE,loadIndex,rpmIndex,advanceCdeg` menerima nilai bertanda penuh −3000…+8000 centidegree. Tidak ada lagi pembatas perubahan ±2° per perintah; aplikasi menampilkan user agreement dan peringatan risiko tanpa mengunci tuning.

## Timing modes

```text
SET,TIMING,mode,intensity,minRpm,maxRpm
GET,TIMING
```

| Mode | ID |
|---|---:|
| STANDARD | 0 |
| SOFT | 1 |
| RESPONSIVE | 2 |
| KUDA | 3 |
| DRUMBAND | 4 |
| FOMO | 5 |
| CUSTOM | 6 |

Default firmware v3: STANDARD 0/10 1150–1700; SOFT 2/10 1250–1550; RESPONSIVE 3/10 1200–1700; KUDA 4/10 1200–1600; DRUMBAND 5/10 1200–1650; FOMO 6/10 1150–1700; CUSTOM 4/10 1200–1650 RPM.

SOFT/RESPONSIVE dibatasi ±2°. Profil ritmis mengayunkan timing bipolar per event dan dijepit ±8° dari map aktif; tidak ada fuel-cut atau spark-cut. Efek hanya aktif pada TPS ≤5%, retard dibatalkan 100 RPM dekat batas bawah, advance dibatalkan 100 RPM dekat batas atas, lalu kembali ke map utama di luar jendela. Untuk profil NS200, idle referensi 1350–1450 RPM. Kendaraan lain wajib memakai rentang idle yang sesuai mesinnya. KUDA/DRUMBAND/FOMO adalah nama profil IgniTra, bukan angka baku universal.

HV SIDE hanya dibaca ketika bit SIDE_DET dari U6 valid. Tanpa modul SIDE, firmware mengirim 0 V dan mengecualikan kanal tersebut dari keputusan `hv_enabled`; aplikasi menampilkannya sebagai N/A. Core tetap memakai pull-down 1 MΩ agar ADC tidak mengambang. Dengan resistor bawah modul 8,2 kΩ, beban paralel menjadi sekitar 8,13 kΩ (galat skala sekitar 0,8%), bukan 7,58 kΩ/sekitar 7,5% seperti pull-down 100 kΩ.

Referensi penetapan baseline:

- [MaxxECU — Lumpy idle](https://www.maxxecu.com/webhelp/solutions_and_faq-lumpy_idle.html): efek dibuat dengan perubahan ignition timing cepat per event, bukan ignition/fuel cut.
- [Pulsar 200NS Service Manual](https://roadsafetymoris.org.in/ns200/bajaj_pulsar_200_nsServiceManual.pdf): idle standar 1350–1450 RPM.

Referensi tersebut menetapkan metode dan envelope kerja, bukan angka universal untuk nama KUDA/DRUMBAND/FOMO. Nilai profil IgniTra di atas adalah baseline konservatif; hasil suara dan batas termalnya harus dikunci dari log RPM, timing, suhu mesin, dan pemeriksaan busi pada prototipe.

## Koreksi panic VHCI R9.6.3

Log lapangan mengonfirmasi reboot nyata pada ESP32: `LoadProhibited` di `vhci_flow_on` dengan backtrace rusak. Penyebab yang ditutup pada R9.6.3 adalah pemanggilan `ble_gatts_notify_custom()` dari task engine/worker di luar task host NimBLE. Mutex R9.6.1 hanya mencegah dua task aplikasi masuk bersamaan, tetapi tidak memindahkan operasi ke konteks host sehingga belum cukup.

R9.6.3 memakai dua antrean TX: antrean prioritas delapan paket untuk ACK/response dan status OTA, serta satu slot latest-value untuk telemetri agar paket lama tidak menumpuk. Event pada default event queue NimBLE menguras ACK/OTA lebih dahulu dan hanya callback event host tersebut yang membuat mbuf serta memanggil `ble_gatts_notify_custom()`. Command dan data OTA masuk satu FIFO worker 16 paket sehingga commit NVS maupun `esp_ota_write()` tidak lagi berjalan pada callback GATT dan urutan BEGIN/DATA/END tetap terjaga. Disconnect mengosongkan antrean TX sehingga paket dari connection handle lama tidak ikut terkirim setelah reconnect. UUID, payload command, telemetry v3, J1, JMOD, dan skematik tidak berubah.

Capability R9.6.3 juga mengumumkan `MANUAL` dan `DIY` sesuai command `MODE` yang memang diimplementasikan firmware. Ini mencegah aplikasi menolak pilihan commissioning tersebut karena daftar `CAPS` yang sebelumnya tidak lengkap.

Respons `CAPS` lengkap panjangnya 223 karakter. Buffer body GET ditetapkan 240 byte; setelah sequence dan CRC, frame tetap di bawah batas antrean TX BLE 256 byte. Ukuran ini harus diperiksa kembali bila capability baru ditambahkan.

Peringatan `PCF8574 U6 tidak merespons; modul opsional fail-safe OFF` sesudah reboot adalah kondisi yang diharapkan saat menguji ESP32 tanpa PCB Core; itu bukan penyebab panic. Kriteria uji regresi: `SETUP,INSTALL,CORE,OEM_REMOVED`, perpindahan Dashboard/Setup, background/resume aplikasi, dan telemetri aktif tidak boleh menghasilkan reboot, `SW_CPU_RESET`, atau disconnect lokal.

## NVS setup journal R9.6.2

Perubahan setup kecil—termasuk `SETUP,INSTALL,CORE,OEM_REMOVED`—sekarang menyimpan hanya struktur setup ke key NVS terpisah. Firmware tidak lagi menulis ulang seluruh blob empat map dan profil OEM untuk satu konfirmasi instalasi. Saat boot, journal setup divalidasi lalu di-overlay ke store utama dan CRC disegel ulang. Perubahan map/OEM tetap menulis store penuh. Ini mengurangi waktu flash stall pada jalur setup tanpa mengubah format protokol maupun pin hardware.

## Koreksi reconnect saat konfirmasi Core

R9.6.1 menambahkan mutex TX, jeda telemetri selama command, stack worker 8 KiB, 32 blok mbuf kelas-1, dan satu koneksi aplikasi aktif. Log `vhci_flow_on` kemudian membuktikan mutex lintas task belum cukup karena pemanggilan notify masih terjadi di luar task host NimBLE. R9.6.3 menggantikan jalur tersebut dengan event queue host; pengaturan stack, pool mbuf, satu koneksi, log `disconnect reason`, dan stack watermark tetap dipertahankan.

Hasil yang diwajibkan: menekan **Konfirmasi Pasang Core 1-Coil** menghasilkan `ACK,INSTALL_CORE` atau `ERR,STOP_ENGINE_WAIT_HV_LT30`; keduanya tidak boleh memulai reconnect. ESP32 tanpa Core boleh menghasilkan nilai ADC tidak valid, tetapi link BLE harus tetap hidup.

## Uji BLE dengan ESP32 tanpa Core

Uji protokol menggunakan ESP32 saja tidak mewakili pembacaan listrik Core: input ADC aki/HV/sensor dapat mengambang atau tidak valid. Firmware tidak boleh reboot atau memutus BLE karena keadaan tersebut. Command GATT kini hanya dimasukkan ke queue; parsing, perubahan konfigurasi, dan commit NVS dijalankan oleh task worker sehingga callback host NimBLE tidak tertahan oleh operasi flash. `SETUP,INSTALL` tetap atomik dan baru mengirim ACK setelah penyimpanan berhasil. Uji coil/HV/keyless/starter tetap wajib dilakukan dengan Core dan modul fisik yang benar.

## AUX, kontak dan starter

Konfigurasi:

```text
AUX,CONFIG,NS200,ON
AUX,CONFIG,MANUAL,ON
AUX,CONFIG,MATIC,ON
```

Kontrol:

```text
AUX,KEYLESS,ON
AUX,KEYLESS,OFF
AUX,START,PULSE,100..3000
AUX,ALL,OFF
GET,AUX
```

Aturan:

1. CONTACT ON memberi izin pengapian sebelum K1 aktif.
2. Starter ditolak jika AUX tidak present/enabled, kontak tidak aktif, tegangan/fault tidak aman, RPM terlalu tinggi, atau I²C gagal.
3. Profil MANUAL juga mewajibkan NEUTRAL_IN.
4. K2 dilepas pada timeout, RPM ≥500, fault, tegangan tidak aman, I²C gagal atau modul dilepas.
5. CONTACT OFF menjalankan K2 OFF → spark/charger HV OFF → K1 OFF.
6. Aplikasi wajib membaca GET,AUX dan GET,STATUS setelah ACK.

Kontak mekanis tetap dapat menyalakan/mematikan mesin tanpa aplikasi. Firmware melaporkan sumber OFF, MECHANICAL atau KEYLESS sehingga aplikasi menampilkan tombol KONTAK ON, START ENGINE atau STOP ENGINE sesuai keadaan nyata.

## Setup pengguna

1. Lepas CDI OEM dan pasang IgniTra.
2. Pilih Core atau Dual menggunakan `SETUP,INSTALL,...,OEM_REMOVED`.
3. Starter untuk memverifikasi pickup.
4. Simpan edge/PPR/gate yang benar.
5. Kalibrasi TDC nyata.
6. Kalibrasi TPS CLOSED dan OPEN.
7. Jalankan FIRST START; batas sementara 3.000 RPM/10°.
8. Matikan mesin, tunggu RPM 0 dan HV <30 V.
9. Simpan READY CENTER atau READY DUAL.
10. Kalibrasi suhu sebelum FAN AUTO.

OEM Learn hanya opsi lanjutan. TPS utama tetap bekerja tanpa modul TPS Diagnostic. Thermal tanpa sensor/kalibrasi valid menyalakan fan secara fail-safe.

## Build dan flashing

Prasyarat: ESP-IDF sesuai project, Python/toolchain aktif, target ESP32 klasik.

```bash
idf.py set-target esp32
idf.py build
idf.py -p COMx flash monitor
```

Jika `sdkconfig.defaults` berubah dan konfigurasi lokal lama masih tersimpan, lakukan reconfigure/clean sesuai ESP-IDF. Jangan commit folder `build/`.

## Uji sebelum kendaraan

1. Pastikan `BENCH_TEST_MODE=0` untuk firmware kendaraan.
2. Pastikan pemanggilan self-test loopback tidak aktif di `main.c`.
3. Lepas jumper BENCH_LOOP.
4. Verifikasi output gate tetap OFF ketika pickup tidak valid.
5. Verifikasi CENTER-only tidak membuat fault SIDE palsu.
6. Verifikasi pelepasan setiap modul menghapus present/active dan mematikan output terkait.
7. Verifikasi fan AUTO fail-safe ketika sensor invalid.
8. Verifikasi kontak mekanis ON/OFF tanpa aplikasi.
9. Verifikasi KEYLESS ON, START, STOP dan batas one-shot K2.
10. Untuk motor manual, verifikasi starter ditolak ketika NEUTRAL_IN tidak aktif.
11. Pastikan RPM 0 dan HVC/HVS <30 V sebelum mengubah konfigurasi atau melepas modul.

## Keselamatan produksi

- Tegangan CDI/HV dapat berbahaya walau mesin telah dimatikan; ukur dan discharge dengan prosedur benar.
- Jangan memasukkan 12 V, pickup mentah atau tegangan coil langsung ke GPIO.
- FAN_RELAY hanya menggerakkan coil relay OEM; arus motor fan memakai kontak relay, kabel dan sekring OEM.
- K2 hanya memparalel tombol/coil relay starter, bukan arus dinamo starter.
- Pertahankan interlock netral/kopling/standar samping OEM.
- Jangan menyatukan GND_LOGIC dan GND_POWER di luar star point.
- Gunakan komponen 105 °C, margin tegangan/arus memadai, sambungan tahan getaran, coating dan creepage HV.
- Produksi PCB tetap memerlukan ERC/DRC, pemeriksaan footprint terhadap komponen nyata, review Gerber, prototype dan uji kendaraan bertahap.

## Aturan pemeliharaan

1. README ini adalah dokumentasi tunggal firmware dan hardware.
2. Perubahan pin harus memperbarui `cdi_board_esp32.h`, skematik, aplikasi dan README.
3. Perubahan protokol harus additive serta memiliki parser/test aplikasi.
4. EFI belum menjadi capability R9.
5. Folder build, binary, log dan patch sementara tidak boleh disimpan di repository.
