# Daftar Belanja IgniTra CDI Rev C

Jumlah sudah dibulatkan ke atas agar tersedia cadangan solder dan rework. Cocokkan ukuran body nyata sebelum memilih footprint.

| Nilai/jenis | Beli | Catatan/alternatif |
|---|---:|---|
| Resistor 10 kΩ ¼ W metal-film | 30 | pull-up, pull-down, comparator, relay |
| Resistor 1 kΩ ¼ W metal-film | 25 | DET, ADC, gate, audio |
| Resistor 4.7 kΩ ¼ W | 15 | I2C, pickup, optocoupler, QPRE AUX |
| Resistor 100 kΩ ¼ W | 10 | ADC dan pulldown |
| Resistor 270 kΩ ½ W | 10 | divider HV center/side |
| Resistor 470 kΩ ½ W | 10 | bleeder kapasitor CDI |
| Resistor 33 kΩ ½ W | 10 | OEM Learn |
| Resistor 330 Ω ½ W | 4 | gate SCR |
| Resistor 680 Ω ¼ W | 4 | driver relay AUX |
| Resistor 68 Ω 1 W | 2 | axial pitch 15.24 mm |
| Shunt 0.05 Ω 5 W | 2 | satu cadangan |
| 100 Ω, 2.2 kΩ, 8.2 kΩ, 15 kΩ, 22 kΩ, 27 kΩ, 39 kΩ, 120 kΩ | masing-masing 5 | nilai khusus dan cadangan |
| BAT85 THT | 20 | alternatif BAT43; leakage diperiksa |
| 1N4148 THT | 10 | clamp/fault |
| UF4007 atau HER108 | 12 | rectifier/flyback |
| 1N4007 | 5 | flyback fan relay |
| BC337-40 C-B-E | 5 | QREL1/QREL2 |
| 2N3906 E-B-C | 6 | QPC/QPS/QPRE1/QPRE2 dan cadangan |
| 2N3904 E-B-C | 5 | SIDE driver, QKEY_SENSE, dan cadangan |
| TIP122 B-C-E | 2 | fan relay coil |
| IRFB4110 G-D-S | 3 | charger |
| IRLZ44N G-D-S | 2 | strobe |
| BT151-800R | 3 | SCR center/side |
| PC817 atau EL817 DIP-4 | 4 | OEM Learn |
| LM339N atau LM2901N DIP-14 | 2 | comparator |
| TC4427A atau MIC4427 DIP-8 | 2 | pinout wajib dicocokkan |
| PCF8574P DIP-16 | 3 | U6=0x20, U7=0x21, satu cadangan |
| Relay Omron G8NB-1U 12 V | 3 | relay automotive PCB setara boleh jika footprint diubah |
| MKP/CBB22 1 µF 630 V | 3 | ukur jarak kaki aktual |
| Elco 470 µF 50 V 105 °C low-ESR | 2 | input filter |
| Elco 10 µF 25–50 V | 5 | decoupling |
| 100 nF 50–100 V | 15 | decoupling |
| 10 nF 50–100 V | 15 | filter ADC/feedback/audio |
| Header male 2×6 2.54 mm | 4 | Core |
| Header female 2×6 2.54 mm | 4 | module; long-tail bila stack-through |
| Header male 2×3 2.54 mm | 3 | Core |
| Header female 2×3 2.54 mm | 3 | module |
| Terminal block 2P 5.08 mm | 5 | AUX outputs |
| Standoff M3 dan baut | 16 set | minimal dua per module |

## Komponen wajib tambahan untuk AUX aman

Tambahkan QPRE1/QPRE2 BC557, RBPRE1/RBPRE2 4.7 kΩ dan RPREPU1/RPREPU2 10 kΩ. PCF8574 HIGH/power-up harus berarti relay OFF; PCF LOW mengaktifkan PNP pre-driver lalu BC337. Jangan menghubungkan EXP_IO1/2 langsung ke basis BC337.

## Input request Rev C

Tambahkan BAT54S SOT-23 ×5, SB560/Schottky ≥5 A 60 V ×4, resistor 47 kΩ ½ W ×5, serta 3.3 kΩ/22 kΩ/10 kΩ dan kapasitor 10 nF untuk J1.1/J1.8/J1.9. Nilai simbol boleh ditulis generik, tetapi footprint PCB wajib mengikuti body dan pitch komponen nyata.
