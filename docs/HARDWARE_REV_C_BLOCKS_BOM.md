# IgniTra CDI Rev C — Pengelompokan blok, prefix, dan BOM

Dokumen ini adalah acuan penempatan manual. Kotak pada skematik hanya panduan visual; net dan reference designator di tabel ini yang menjadi sumber penempatan.

## J1 yang benar

J1 memakai penomoran kolom: kiri 1–6, kanan 7–12. Pada harness NS200 asli J1.1/J1.8/J1.9 tetap kosong; fungsi berikut hanya dipakai setelah terminal kabel tambahan dipasang dan Setup AUX diaktifkan.

| Pin | Net | Kelompok |
|---:|---|---|
| 1 | KEYLESS_REQ | pulsa +12 V terproteksi; wake logic dan toggle contact keyless |
| 2 | TPS_A | sensor TPS |
| 3 | TEMP_SENSOR | sensor suhu |
| 4 | TPS_B | sensor TPS |
| 5 | IGN_12V | keluaran kunci kontak normal / masukan daya kendaraan |
| 6 | COIL_SIDE | keluaran coil side |
| 7 | FAN_RELAY | coil relay kipas OEM |
| 8 | START_REQ | dry-contact/open-collector ke GND_LOGIC |
| 9 | MODE_REQ / NEUTRAL_IN | NS200/matic: mode; universal manual: netral aktif-rendah |
| 10 | PICKUP_RAW | pickup mesin |
| 11 | GND_STAR | satu-satunya return daya kendaraan |
| 12 | COIL_CENTER | keluaran coil center |

## Aturan prefix

| Prefix | Jenis |
|---|---|
| R | resistor | C | capacitor |
| D, ZD, TVS | diode/zener/TVS | Q | transistor/MOSFET |
| SCR | thyristor | U | IC/modul aktif |
| J, JP, JMOD, PMOD | konektor/jumper | K | relay |
| L | induktor | T | transformer | NT | net-tie/star link |
| TP | test point |

## CORE

### C1 — Harness, proteksi 12 V, dan ground star

J1; DREV SB560 DO-201; TVS_IN 1.5KE18A DO-201; DTVS1–DTVS2 1.5KE33A; L_IN 47 µH ≥5 A; C_IN 470 µF 50 V 105 °C low-ESR; NT1 jumper star; NT2 0 Ω; UBUCK MP1584; C5A 100 nF; C5B 47–100 µF/10 V; JP1 header 1×2.

Tempatkan J1→DREV/TVS→L_IN→C_IN→UBUCK dalam satu aliran. NT1/NT2 berada di satu titik, bukan di dua lokasi berbeda.

### C2 — ESP32, 3V3/5V, I2C, dan deteksi modul

U1 ESP32 DevKit 38 pin; U6 PCF8574P DIP-16 alamat 0x20; U7 PCF8574P DIP-16 alamat 0x21; CU6/CU7 100 nF; RI2C_SDA/RI2C_SCL 4.7 kΩ; RDET_SIDE/RDET_TH/RDET_LEARN/RDET_AUX/RDET_TPS 10 kΩ; JMOD_SIDE, JMOD_THERMAL, JMOD_AUX, JMOD_EXP header 2×6 2.54 mm; JMOD_LEARN/JMOD_TPS header 2×3 2.54 mm.

U6: P0 SIDE, P1 THERMAL, P2 LEARN, P3 AUX, P4 TPS, P5 EXP_IO1, P6 EXP_IO2, P7 EXP_IO3. U7: P0 KEYLESS_REQ_N, P1 START_REQ_F, P2 MODE_REQ_F/NEUTRAL_IN. SDA=GPIO21, SCL=GPIO22.

### C3 — Pickup, TPS, suhu, VBAT, comparator

U2 LM339N DIP-14; RPICK1 39 kΩ, RPICK2 10 kΩ, RPICK3 10 MΩ, RPICK4 4.7 kΩ, RPICK5 1 kΩ; DPICK_H/DPICK_L BAT85; RTPS0 100 Ω, RTPS1 15 kΩ, RTPS2 27 kΩ, RTPS3 1 kΩ; DTPS_H/DTPS_L BAT85; CTPSS; RVB1 100 kΩ, RVB2 22 kΩ, RVB3 1 kΩ, CBAT 10 nF; DBAT_HIGH/LOW BAT85; RHS_PD/RREF_PD/RTEMP_PD 100 kΩ; JTPS 2×3.

Semua komponen ini berada di sisi logic. Jangan lintaskan jalur transformer, relay, gate SCR, atau coil di bawah blok ini.

### C4 — Charger transformer dan driver 15 V

U5 TC4427AEPA DIP-8; QHV1/QHV2 IRFB4110 TO-220 G-D-S; RGA/RGB 10 Ω; RGPD1/RGPD2 10 kΩ; RPWMA/RPWMB 1 kΩ; RPWMPDA/RPWMPDB 10 kΩ; T1 EE35; RSENSE 0.05 Ω 5 W; RIS 100 Ω; RI1 120 kΩ; RI2 10 kΩ; RVM1/RVM2 10 kΩ; RVOV1 120 kΩ; RVOV2 100 kΩ; CVOV 10 nF; DREC1–DREC4 UF4007; DCLA/DCLB 1N4148; RDRV_SUP 68 Ω 1 W axial pitch 15.24 mm; ZD15 15 V 1.5 W; CDRV1 10 µF 25 V; CDRV2 100 nF; DFAULT_A/B 1N4148.

QHV, transformer, rectifier, shunt, dan C_IN harus jauh dari U1/U6 dan jalur pickup.

### C5 — CDI center/HV

C_CENTER 1 µF 630 V MKP; SCR1 BT151-800R TO-220; DCH_C UF4007; QNC BC547B; QPC 2N3906 E-B-C; RGC1 4.7 kΩ, RGC2 **2.2 kΩ**, RGC3 10 kΩ, RGC4 330 Ω ½ W, RGC5 1 kΩ, RGC6 10 kΩ; RBC1–RBC4 470 kΩ ½ W; RHVC1–RHVC4 270 kΩ, RHVC5 8.2 kΩ, RHVC6 1 kΩ; DHVC_H/L BAT85; CFBC 10 nF/100 V.

Pertahankan zona tanpa copper dan slot pada node HV. C_CENTER, SCR1, DCH_C, dan keluaran coil ditempatkan berdekatan.

## MODULE SIDE

- S1 HV charge/discharge: C_SIDE 1 µF 630 V, SCR2 BT151-800R, DCH_S UF4007.
- S2 gate driver: QNS 2N3904 E-B-C, QPS 2N3906 E-B-C, RGS1 4.7 kΩ, RGS2 **2.2 kΩ**, RGS3 10 kΩ, RGS4 330 Ω ½ W, RGS5 1 kΩ, RGS6 10 kΩ.
- S3 feedback: RHVS1–RHVS4 270 kΩ, RHVS5 8.2 kΩ, CFBS 10 nF/100 V.
- S4 interface: PMOD_SIDE 2×6, RMDET_SIDE 1 kΩ.

Pisahkan pin BRIDGE_PLUS/COIL_SIDE dari SDA/SCL/DET dengan slot, keepout, dan conformal coating.

## MODULE THERMAL

- T1 sensor: RT1 4.7 kΩ, RT2 15 kΩ, RT3 27 kΩ, RT4 1 kΩ, CTEMP 10 nF, DTEMP_HIGH/LOW BAT85.
- T2 driver coil relay OEM: QFAN TIP122 TO-220 pin 1=B,2=C,3=E; RFAN1 1 kΩ; RFAN2 10 kΩ; DFAN 1N4007.
- T3 interface: PMOD_THERMAL 2×6, RMDET_THERMAL 1 kΩ.

QFAN hanya untuk coil relay OEM. Motor fan tetap melalui sekring dan kontak relay OEM.

## MODULE OEM LEARN

- L1 center: ROEMC1–ROEMC4 masing-masing 33 kΩ ½ W; DOEMC_REV 1N4148; U3 PC817.
- L2 side: ROEMS1–ROEMS4 masing-masing 33 kΩ ½ W; DOEMS_REV 1N4148; U4 PC817.
- L3 interface: JLEARN 1×3, PMOD_LEARN 2×3, RMDET_LEARN 1 kΩ.

Beri jarak antara resistor input OEM dan sisi output optocoupler.

## MODULE AUX

- A1 strobe: QSTROBE IRLZ44N, RSTR1 **100 Ω ½ W**, RSTR2 10 kΩ, JSTROBE 1×2.
- A2 keyless: K1 G8NB-1U 12 V, QREL1 BC337, DREL1 UF4007, RREL1 680 Ω, RRELPD1 10 kΩ, JKEYLESS terminal 5.08 mm.
- A3 starter: U8 NE555P edge-triggered one-shot; CSTART_TRIG 10 nF, RSTART_TRIG 10 kΩ, DSTART_TRIG BAT85; RSTART_TIME 2.2 MΩ 1%, CSTART_TIME 1 µF X7R ±20%, CSTART_CTRL 10 nF; K2 G8NB-1U 12 V, QREL2 BC337, DREL2 UF4007, RREL2 680 Ω, RRELPD2 10 kΩ, JSTART terminal 5.08 mm.
- A4 audio: RAUD1/RAUD2 1 kΩ; CAUD1/CAUD2 10 nF; JAUDIO/JAUDIO_OUT.
- A5 interface: PMOD_AUX 2×6, RMDET_AUX 1 kΩ.

QPRE1/RBPRE1/RPREPU1 memberi K1 safe-default HIGH=OFF. K2 memakai pulsa jatuh EXP_IO2 untuk memicu U8. Dengan t=1.1RC, nominal 2.42 s dan worst-case R +1%/C +20% sekitar 2.93 s; output PCF yang macet LOW tidak dapat menahan K2 terus-menerus. K2 hanya memparalel tombol/coil relay starter OEM, bukan arus dinamo starter.

## MODULE TPS dan EXP

TPS: RTPS4 15 kΩ, RTPS5 27 kΩ, RTPS6 1 kΩ, CTPSR, DTREF_H/L BAT85, PMOD_TPS 2×3, RMDET_TPS 1 kΩ. EXP: PMOD_EXP 2×6; area lain dibiarkan kosong untuk ekspansi.

## Daftar belanja gabungan

| Kelompok | Komponen utama | Alternatif |
|---|---|---|
| MCU | ESP32 DevKitC 38 pin | ESP32-WROOM DevKit 38 pin dengan pinout sama |
| Comparator | LM339N DIP-14 | LM2901N automotive-temperature |
| Gate driver | TC4427AEPA DIP-8 | MIC4427 DIP-8 dengan pinout diverifikasi |
| I/O expander | PCF8574P DIP-16 | PCF8574AP hanya jika alamat firmware diubah |
| MOSFET charger | IRFB4110 ×2 | MOSFET N 100 V, RDS(on) ≤8 mΩ, G-D-S |
| SCR | BT151-800R ×2 | BT152-800R dengan pinout/arus diperiksa |
| Relay AUX | Omron G8NB-1U 12 V ×2 | relay automotive PCB 12 V SPST-NO setara; footprint wajib disesuaikan |
| Fan driver | TIP122 ×1 | TIP120/TIP121 hanya bila rating tegangan mencukupi |
| Optocoupler | PC817 ×2 | EL817 DIP-4 |
| TVS input | 1.5KE18A ×1 | 1.5KE18CA; polaritas/tegangan clamp diperiksa |
| TVS HV | 1.5KE33A ×2 | 1.5KE33CA |
| Rectifier | UF4007 minimal 8 pcs | HER108/FR107 dengan rating sesuai |
| Clamp signal | BAT85 sekitar 12 pcs | BAT43/1N5819 untuk THT, leakage diverifikasi |
| Kapasitor CDI | 1 µF 630 V MKP ×2 | CBB22 105J 630 V dengan ukuran kaki nyata |
| Header | male/female THT 2.54 mm 2×6 dan 2×3 | long-tail female bila benar-benar ditumpuk |
| Resistor | metal-film 1%, ¼ W umum | carbon-film 5% hanya untuk non-sense/non-HV |
| HV divider/OEM | 270 kΩ/33 kΩ, gunakan ½ W | seri resistor tambahan jika body lebih pendek |

Sebelum pembelian, footprint dipilih dari ukuran body nyata. Tulisan “68 Ω 1 W” tidak menggantikan footprint; gunakan axial body sekitar 9–11 mm dengan pitch 15.24 mm.
