# Panduan Uji Firmware IgniTra CDI ESP32 R9

Panduan ini mengikuti PCB modular final. Jangan menyuntik sinyal kendaraan langsung ke GPIO ESP32; gunakan konektor dan rangkaian kondisioning pada PCB.

## Mode produksi dan bench

Pada `cdi_engine_esp32.c`:

```c
#define BENCH_TEST_MODE 0
```

- `0`: pemasangan normal, seluruh pemeriksaan aki dan HV aktif.
- `1`: hanya untuk uji USB/meja; pemeriksaan aki dan HV dibypass di software.

Firmware yang dipasang ke kendaraan wajib memakai `0`.

Self-test loopback pada `main.c` secara default tidak aktif:

```c
// cdi_selftest_init();
```

Aktifkan hanya selama pengukuran jitter, kemudian nonaktifkan dan lepaskan kabel test pad sebelum pemasangan ke motor.

## Uji pickup melalui PCB

| Dari | Ke | Keterangan |
|---|---|---|
| Ground generator | J1.11 / `GND_STAR` | Referensi sinyal |
| Sinyal generator | J1.10 / `PICKUP_RAW` | Masuk melalui filter dan LM339 |

Jangan menyuntikkan sinyal generator langsung ke GPIO4 pada unit PCB final. GPIO4 menerima `PICKUP_DIG` yang sudah dikondisikan LM339 dan ditarik ke 3V3 oleh RPICK4.

Untuk PPR=1:

| Frekuensi | RPM |
|---:|---:|
| 13,3 Hz | sekitar 800 |
| 25 Hz | 1.500 |
| 50 Hz | 3.000 |
| 100 Hz | 6.000 |

Mulai dari amplitudo rendah dan pastikan tegangan pada `PICKUP_DIG` tidak melampaui 3,3 V.

## Uji jitter

1. Pastikan HV/charger tidak terhubung.
2. Hubungkan sementara test pad `TP_GATE_C` ke `TP_BENCH_LOOP`.
3. Aktifkan `cdi_selftest_init()`.
4. Suntikkan pickup melalui J1.10.
5. Pantau laporan jitter melalui serial.
6. Setelah selesai, nonaktifkan self-test dan lepaskan kabel kedua test pad.

Test pad bukan jumper konfigurasi dan tidak boleh tersambung ketika CDI dipakai di motor.

## Uji OEM Learn

OEM Learn memerlukan modul `04_MODULE_OEM_LEARN`.

- Hubungkan probe CENTER modul ke primer koil OEM CENTER.
- Hubungkan probe SIDE hanya jika motor memiliki kanal koil OEM SIDE.
- Jangan pernah menghubungkan probe ke kabel busi atau sisi tegangan tinggi.
- Input ESP32 `OEM_CENTER` dan `OEM_SIDE` berasal dari PC817 aktif-rendah. Firmware membaca falling edge.
- Jalankan OEM Learn ketika CDI OEM masih menjadi sumber pengapian dan output IgniTra dinonaktifkan.
- Setelah profil selesai disimpan, matikan mesin sebelum berpindah ke mode DIY.

## Uji fan

1. Pasang modul thermal/fan.
2. Gunakan FAN ON untuk memastikan relay bekerja.
3. Pastikan transistor hanya menggerakkan kumparan relay, bukan motor kipas langsung.
4. Lakukan kalibrasi suhu tiga titik sebelum memilih FAN AUTO.
5. Jika kalibrasi/sensor tidak valid dalam AUTO, firmware menyalakan fan sebagai fail-safe.

## Uji CENTER-only dan SIDE

- Tanpa modul SIDE, gunakan READY CENTER. HV SIDE nol adalah kondisi normal dan tidak memicu imbalance.
- READY THREE hanya digunakan setelah modul SIDE terpasang dan offset SIDE sudah diukur atau direkam lewat OEM Learn.
- Jangan mengaktifkan SIDE hanya karena profil kendaraan bernama NS200.

## Sebelum dipasang ke kendaraan

1. `BENCH_TEST_MODE` harus `0`.
2. `cdi_selftest_init()` harus tidak dipanggil.
3. Kabel test pad loopback harus dilepas.
4. Gate CENTER, SIDE, strobe, fan, dan audio harus LOW saat boot.
5. Pastikan polaritas PC817, LM339, DREV, TVS, SCR dan MOSFET benar.
6. Pastikan `GND_POWER`, `GND_LOGIC`, dan `GND_STAR` mengikuti NT1/NT2 pada skematik.
7. Ukur HV dengan alat yang sesuai sebelum menghubungkan koil.
