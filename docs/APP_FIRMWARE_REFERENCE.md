# Kontrak Integrasi Aplikasi–Firmware IgniTra

Dokumen ini adalah spesifikasi implementasi untuk memigrasikan aplikasi Android
lama ke firmware **IgniTra CDI ESP32 R9 Modular**. Dokumen ini menjelaskan
bagian aplikasi yang harus dipertahankan, diubah, ditambah, atau dihapus,
urutan komunikasi BLE, sumber kebenaran state, setup produk, modul, map, OTA,
binding, kompatibilitas firmware lama, serta kriteria pengujian.

Referensi firmware aktif:

- main/cdi_engine_esp32.c
- main/cdi_r5_protocol.c
- main/cdi_r5_ble.c
- main/cdi_ble_nimble.c

main/cdi_firmware.* bukan implementasi aktif dan tidak boleh dijadikan referensi.

---

## 1. Sasaran arsitektur

1. Firmware adalah sumber kebenaran untuk status setup, modul, output, map,
   fault, profil, dan versi.
2. Aplikasi hanya menyimpan preferensi UI, binding lokal, nama kendaraan, dan
   cache tampilan terakhir. Cache tidak boleh membuka izin tulis.
3. Setup normal adalah **langsung mengganti CDI OEM**, bukan OEM Learn.
4. CORE selalu berarti satu kanal coil melalui J1.12/COIL_CENTER.
5. Kanal kedua hanya ada jika modul SIDE dipilih dan dikonfigurasi, melalui
   J1.6/COIL_SIDE.
6. Fitur UI ditentukan oleh capability dan status modul, bukan model motor.
7. Parser harus toleran terhadap field/capability baru dan tidak boleh putus
   hanya karena query opsional belum dikenal firmware lama.
8. Aplikasi lama tetap dapat membaca paket telemetry v3 dan memakai perintah
   lama selama masa migrasi.

### Sumber kebenaran setiap data

| Data | Sumber utama | Cache aplikasi |
|---|---|---|
| Versi dan platform | GET,VERSION | Boleh, hanya tampilan |
| Serial perangkat | GET,IDENTITY | Boleh untuk binding |
| Capability | GET,CAPS | Boleh per serial dan build |
| Modul terpasang/aktif | GET,MODULES | Boleh, tidak boleh mengubah sendiri |
| Tahap commissioning | GET,COMMISSION | Boleh, tidak boleh menjadi authority |
| Konfigurasi setup | GET,SETUP | Boleh |
| Map aktif dan metadata | GET,META | Boleh |
| RPM/TPS/HV/fault | Telemetry dan GET,STATUS | Hanya last-known |
| Izin menulis | Koneksi + binding + safety firmware | Tidak boleh dipalsukan |
| Mode demo | State lokal terpisah | Tidak pernah dikirim ke perangkat |
| Receiver BT audio dipasang | Pengaturan aplikasi per serial CDI | Ya, accessory config |
| Pairing/connection/rute audio | Android Bluetooth/AudioManager | Hanya status Android |

---

## 2. Identitas firmware R9.2

| Item | Nilai |
|---|---|
| Platform | ESP32 klasik/WROOM-32 |
| Release | R9 |
| Semantic version | 9.2.0 |
| Build ID | 20260923 |
| Protokol perintah | 5 |
| Paket telemetry | v3, 20 byte |
| Nama advertising | NS200-CDI |
| Map | 4 slot |
| Grid maksimum | 32 RPM × 16 TPS |
| Batas format RPM | 30.000 RPM |
| Batas format advance | -30,0° sampai +80,0° |
| PPR maksimum | 12 |

Jangan menilai kompatibilitas hanya dari teks R9 atau semantic version.
Gunakan platform, protocolVersion, telemetryVersion, capability, dan schema
masing-masing respons.

---

## 3. Audit aplikasi lama: keputusan per bagian

Tabel ini mengacu pada struktur aplikasi
phreakazone/CDI_STM32_Android saat dokumen dibuat.

| File/bagian lama | Keputusan | Perubahan wajib |
|---|---|---|
| CdiProtocol.kt | Pertahankan dan perluas | Pertahankan CRC/frame dan decoder telemetry v3/v4; tambah model respons bertipe, capability, modul, commission, version, identity |
| BleCdiClient.kt | Pertahankan | Pertahankan single in-flight queue, sequence, retry, buffering; scan utama berdasarkan service UUID; tambah status sinkronisasi |
| CdiViewModel.kt | Refactor besar | Pisahkan DeviceSessionState, DemoSessionState, BindingState, CommissionState; firmware menjadi authority |
| DashboardScreen.kt | Ubah | Kartu/gauge dinamis berdasarkan MODULES dan TEMP; jangan selalu menganggap dual coil |
| MapsScreen.kt | Ubah | Hilangkan limit 11.500/36°; bentuk grid dan batas berasal dari CAPS, META, PROFILE |
| SetupScreen.kt | Bangun ulang | Ganti enam tab menjadi tiga layar berbasis COMMISSION |
| QuickSetupGuideScreen.kt | Hapus dari alur produk | Jangan ada setup kedua; boleh dihapus atau dipindah ke kode arsip developer |
| StrobeScreen.kt | Gabungkan | Strobe menjadi bagian Pemeriksaan/TDC, bukan flow setup terpisah |
| WiringWorkshopHubScreen.kt | Hapus dari navigasi pengguna | Ganti menu Wiring menjadi Buku Petunjuk; editor/perakitan PCB bukan fitur pengguna produk |
| BleHexScreen.kt | Ubah | Jadikan halaman Perangkat/Diagnostik; raw console hanya mode developer |
| SoundScreen.kt | Ubah | Pertahankan generator suara Android; tambah status rute Bluetooth A2DP eksternal, tanpa command audio ke firmware |
| MainActivity.kt / ScreenTab | Ubah | Navigasi: Dashboard, Map, Setup, Buku, Perangkat; fitur lanjutan muncul sesuai capability |

### Daftar file target yang disarankan

Developer tidak wajib memakai nama persis ini, tetapi tanggung jawabnya harus
dipisah:

| Aksi | File | Isi |
|---|---|---|
| Ubah | CdiProtocol.kt | Frame, CRC, telemetry, sealed response parser |
| Ubah | BleCdiClient.kt | Scan UUID, queue satu command, subscription, reconnect |
| Pecah | CdiViewModel.kt | Jangan lagi menjadi parser + storage + demo + UI sekaligus |
| Tambah | DeviceModels.kt | Identity, Caps, Modules, Commission, Status, Setup |
| Tambah | DeviceRepository.kt | Snapshot authoritative dari firmware |
| Tambah | SyncCoordinator.kt | Initial query dan refresh sesudah command |
| Tambah | BindingStore.kt | Record lokal per serial |
| Tambah | BindingCoordinator.kt | Evaluasi BOUND/UNBOUND/LEGACY |
| Tambah | WritePolicy.kt | Satu gate untuk seluruh write |
| Tambah | AudioRouteRepository.kt | Pairing/connection/rute A2DP Android |
| Bangun ulang | SetupScreen.kt | Pemasangan, Pemeriksaan, First Start/Ready |
| Ubah | SoundScreen.kt | Generator suara lama + status/pemilih rute media |
| Ubah | DashboardScreen.kt | Kartu berdasarkan MODULES/TEMP |
| Ubah | MapsScreen.kt | CAPS/META/PROFILE dinamis |
| Ubah | BleHexScreen.kt | Perangkat/diagnostik, raw write tetap digate |
| Hapus dari route | QuickSetupGuideScreen.kt | Tidak boleh menjadi setup kedua |
| Hapus dari route | WiringWorkshopHubScreen.kt | Diganti Buku Petunjuk |

Urutan aman untuk refactor: buat model/parser dan repository terlebih dahulu,
baru pindahkan Screen satu per satu. Jangan menghapus UI lama sebelum jalur
baru sudah membaca firmware dengan benar.

### Bug kompatibilitas yang harus diperbaiki

1. Aplikasi lama hanya menganggap ACK,PONG_R7_2 sebagai hasil PING yang sah.
   R9 menjawab ACK,PONG_R9. Terima setiap ACK dengan operation berawalan
   PONG_, termasuk PONG_R7_2 dan PONG_R9.
2. Scan lama mengutamakan nama NS200-CDI-R7. R9 mengiklankan NS200-CDI.
   Filter utama harus service UUID; nama hanya fallback.
3. Aplikasi membatasi rev limit ke 11.500 RPM dan advance ke 0–36°.
   Hapus hard-code dan gunakan CAPS/PROFILE.
4. Parser SETUP membatasi PPR 1–4. R9 mendukung 1–12.
5. Readback map hanya menerima rpmCount 8/16 dan tpsCount 4/8. R9 mendukung
   rpmCount 2–32 dan tpsCount 2–16.
6. Nilai setup_stage di SharedPreferences saat ini dapat membuat state READY
   dan output flag sintetis. Ini harus dihentikan saat perangkat nyata
   terhubung.
7. READY dual lama mengirim SETUP,READY,THREE. Implementasi baru mengirim
   SETUP,READY,DUAL,offset. Parser ACK tetap menerima READY_THREE untuk
   kompatibilitas.
8. OTA_IMAGE_VERSION 80200 tidak boleh menjadi versi universal. Versi image
   berasal dari metadata image/manifest dan platform harus cocok.
9. Toggle MCU STM32/ESP32 manual tidak boleh muncul di setup normal. Platform
   dideteksi melalui INFO/VERSION; override hanya di diagnostik developer.
10. Setup saat ini digandakan di SetupScreen dan QuickSetupGuideScreen.
    Hanya satu state machine dan satu jalur UI yang boleh menulis ke firmware.

---

## 4. BLE dan framing

### UUID

| Fungsi | UUID | Properti |
|---|---|---|
| Service | 7a8f1000-6c9d-4e40-a45f-0b4b4e533230 | Primary |
| Telemetry | 7a8f1001-6c9d-4e40-a45f-0b4b4e533230 | Notify |
| Command | 7a8f1002-6c9d-4e40-a45f-0b4b4e533230 | Write |
| Response | 7a8f1003-6c9d-4e40-a45f-0b4b4e533230 | Notify |
| OTA data | 7a8f1004-6c9d-4e40-a45f-0b4b4e533230 | Write without response |
| OTA status | 7a8f1005-6c9d-4e40-a45f-0b4b4e533230 | Notify |

### Frame teks

    @sequence,COMMAND,arg1,arg2*CRC16\n

CRC16-CCITT:

- polynomial 0x1021;
- initial value 0xFFFF;
- dihitung dari sequence sampai argumen terakhir;
- karakter @, tanda bintang, CRC, CR, dan LF tidak ikut dihitung.

Respons memiliki sequence yang sama. Queue aplikasi tetap hanya mengirim satu
perintah sampai ACK/ERR/response query diterima atau timeout. Notification yang
terpotong harus ditampung sampai newline.

### Scan dan koneksi

Urutan filter:

1. Service UUID cocok: terima.
2. Jika UUID tidak ada di advertising, fallback nama yang mengandung NS200-CDI.
3. Jangan exact-match hanya NS200-CDI-R7.
4. Setelah connect, wajib verifikasi service dan characteristic.
5. Subscribe Response dan Telemetry sebelum mengirim handshake.

---

## 5. State machine sesi aplikasi

| State | Arti | UI/write |
|---|---|---|
| DISCONNECTED | Tidak ada perangkat | Demo/read-only lokal |
| SCANNING | Mencari perangkat | Tidak boleh write |
| CONNECTING | GATT sedang dibuat | Tidak boleh write |
| SUBSCRIBING | Characteristic ditemukan | Tidak boleh write |
| SYNCING | Handshake/query awal berjalan | Read-only, tampilkan loading |
| NEEDS_BINDING | Serial valid tetapi belum cocok | Read-only |
| READY_READ_ONLY | Firmware lama/tidak dibinding | Telemetry dan diagnosis |
| READY_FULL | Sinkron, binding cocok | Write tetap mengikuti safety |
| DEGRADED | Query wajib gagal/format salah | Read-only dan jelaskan sebab |

~~~mermaid
flowchart TD
    A[Scan service UUID] --> B[Connect dan subscribe]
    B --> C[Handshake berurutan]
    C --> D{Identity tersedia?}
    D -- Tidak --> E[Legacy read-only atau kebijakan legacy]
    D -- Ya --> F{Serial cocok binding?}
    F -- Tidak --> G[Konfirmasi binding]
    F -- Ya --> H[Sinkronkan state firmware]
    G --> H
    H --> I[Dashboard dan fitur dinamis]
~~~

DemoSessionState harus berbeda objek dari DeviceSessionState. Saat perangkat
nyata tersambung:

1. hentikan generator telemetry demo;
2. hapus overlay READY/output sintetis;
3. masuk SYNCING;
4. isi UI hanya dari respons firmware;
5. bila sinkronisasi gagal, jangan kembali memakai state demo dengan label
   seolah berasal dari perangkat.

---

## 6. Handshake dan sinkronisasi awal

Setelah subscription aktif, kirim secara berurutan:

1. PING
2. GET,INFO
3. GET,VERSION
4. GET,IDENTITY
5. GET,CAPS
6. GET,HARDWARE
7. GET,MODULES
8. GET,COMMISSION
9. GET,SETUP
10. GET,STATUS
11. GET,META
12. GET,PROFILE
13. GET,TEMP
14. GET,ADC
15. GET,MODE
16. GET,LEARN
17. GET,OTA

Klasifikasi:

- PING, INFO/CAPS, STATUS, SETUP, dan META dianggap minimum untuk jalur legacy.
- VERSION, IDENTITY, HARDWARE, MODULES, COMMISSION, PROFILE, TEMP, ADC, MODE,
  LEARN, dan OTA adalah query tambahan.
- ERR,GET pada query tambahan berarti unsupported, bukan koneksi gagal.
- Timeout PING atau service tidak lengkap membuat sesi DEGRADED/disconnect.
- UI baru boleh dinyatakan sinkron setelah semua query yang didukung selesai.

~~~kotlin
data class SyncResult(
    val requiredOk: Boolean,
    val unsupportedQueries: Set<String>,
    val parseErrors: List<String>
)

fun canWrite(session: DeviceSession): Boolean =
    session.phase == SessionPhase.READY_FULL &&
    session.binding.matches &&
    session.identity.serial != "UNAVAILABLE"
~~~

canWrite hanya gate aplikasi. Firmware tetap menentukan kondisi RPM/HV.

---

## 7. Respons query dan model data

Parser memeriksa token pertama dan schema, lalu menerima field tambahan di
belakang untuk kompatibilitas ke depan.

### INFO dan VERSION

    INFO,ESP32,R9,5,3,IGNITRA_R9_MODULAR
    VERSION,1,R9,9.2.0,20260923,ESP32,5,3

VERSION berisi schema, release, semver, buildId, platform, protocolVersion,
telemetryVersion. Bandingkan semver sebagai angka per komponen. Keputusan fitur
tetap memakai capability.

### IDENTITY

    IDENTITY,1,IGT-ESP32-XXXXXXXXXXXX,SERIAL_V1,LOCAL_APP,0

| Field | Arti |
|---|---|
| schema | 1 |
| serial | ID stabil dari base MAC/eFuse |
| serialScheme | SERIAL_V1 |
| bindingPolicy | LOCAL_APP |
| firmwareEnforced | 0 pada R9.2 |

UNAVAILABLE tidak boleh disimpan sebagai binding permanen.

### CAPS

    CAPS,5,30000,-300,800,32,16,4,12,FAN,TEMP3,DYNO,PROFILE,OTA,OEM_LEARN,MANUAL,DIY,FIRST_START,MODULE_STATUS,QUICK_INSTALL,FW_VERSION,DEVICE_SERIAL,APP_LOCAL_BINDING

| Posisi | Nama | Unit/nilai |
|---:|---|---|
| 1 | protocolVersion | 5 |
| 2 | maxRpm | RPM |
| 3 | minAdvanceX10 | 0,1 derajat |
| 4 | maxAdvanceX10 | 0,1 derajat |
| 5 | maxRpmPoints | jumlah |
| 6 | maxLoadPoints | jumlah |
| 7 | mapSlots | jumlah |
| 8 | maxPpr | jumlah |
| 9+ | capabilities | token string |

Capability tidak dikenal harus diabaikan dengan aman, bukan dianggap error.
Gunakan Set<String>, bukan enum parser yang gagal pada token baru.

### HARDWARE

    HARDWARE,1,CENTER_BASE,SIDE_OPTIONAL,THERMAL_OPTIONAL,OEM_LEARN_OPTIONAL,AUX_OPTIONAL,TPS_DIAG_OPTIONAL

HARDWARE menyatakan kemampuan PCB, bukan bukti modul terpasang.

### MODULES

    MODULES,1,installedMask,activeMask,observedMask,faultMask,coreProfile

Model modul adalah **komposisi bit independen**, bukan satu pilihan enum.
Core tidak mempunyai bit karena selalu tersedia. SIDE, THERMAL, OEM_LEARN,
AUX, dan TPS_DIAG dapat dipasang serta dikonfigurasi bersamaan. Aplikasi wajib
memakai operasi bit untuk setiap modul dan tidak boleh membuat pilihan radio
yang hanya mengizinkan satu modul.

| Bit | Nilai | Modul |
|---:|---:|---|
| 0 | 1 | SIDE/coil kedua |
| 1 | 2 | THERMAL/fan |
| 2 | 4 | OEM_LEARN |
| 3 | 8 | AUX/strobe |
| 4 | 16 | TPS_DIAG |

- installed: pilihan konfigurasi yang disimpan;
- active: sedang diperintah aktif oleh firmware;
- observed: ada bukti listrik/sinyal saat kondisi memungkinkan;
- fault: firmware menyatakan fault.

observed=0 bukan otomatis rusak. SIDE saat HV mati ditampilkan “Belum diuji”.

Contoh decoding installedMask:

| installedMask | Modul terpasang di atas Core |
|---:|---|
| 0 | Tidak ada modul opsional |
| 1 | SIDE |
| 2 | THERMAL |
| 3 | SIDE + THERMAL |
| 13 | SIDE + OEM_LEARN + AUX |
| 31 | SIDE + THERMAL + OEM_LEARN + AUX + TPS_DIAG |

Nilai kombinasi diperoleh dengan OR/penjumlahan bit. Jangan membuat daftar
kombinasi tetap karena seluruh kombinasi 0–31 sah secara format. Validitas
operasional tetap ditentukan oleh dependensi masing-masing modul.

| coreProfile | Label |
|---:|---|
| 0 | IgniTra Core • 1 Coil |
| 1 | IgniTra Core + SIDE • Dual Coil, belum aktif |
| 2 | IgniTra Core + SIDE • Dual Coil |

coreProfile hanya menjelaskan profil output pengapian Core/SIDE. Field ini
tidak merangkum THERMAL, OEM_LEARN, AUX, atau TPS_DIAG. Contoh:
coreProfile=2 dan installedMask=31 berarti Dual Coil dengan seluruh modul
opsional terpasang.

### COMMISSION

    COMMISSION,1,stage,nextAction,ready,advisoryMask

| nextAction | Tindakan UI |
|---:|---|
| 1 | Pilih pemasangan Core/Dual |
| 2 | Verifikasi pickup |
| 3 | Kalibrasi TDC |
| 4 | Kalibrasi TPS |
| 5 | First Start |
| 6 | Matikan mesin dan konfirmasi Ready |
| 7 | Selesai |
| 8 | OEM Learn lanjutan |

| Bit advisory | Pesan |
|---:|---|
| 0 | Pickup belum selesai |
| 1 | TDC belum selesai |
| 2 | TPS belum selesai |
| 3 | First Start belum terbukti |
| 4 | SIDE dikonfigurasi tetapi belum aktif |
| 5 | THERMAL dikonfigurasi tetapi suhu belum valid |

nextAction menentukan tombol utama, bukan SharedPreferences.

### STATUS, META, SETUP

    STATUS,rpm,tpsPermille,hvCenter,hvSide,activeSlot,mapMode,outputPermission,proEnabled
    META,name,mapMode,limiterType,rpmLimit,softBandRpm,hvTargetVolts,generation,rpmCount,tpsCount
    SETUP,stage,pickupEdge,triggerCdeg,sideOffsetCdeg,ppr,gateUs,tpsClosedAdc,tpsOpenAdc,firstStartHv,centerEnabled,sideEnabled,fanMode,pickupQuality

rpmCount valid 2–32, tpsCount 2–16, dan PPR 1 sampai maxPpr dari CAPS.

### PROFILE, TEMP, ADC

    PROFILE,name,rpmMin,rpmMax,advanceMinX10,advanceMaxX10,ppr,triggerX10
    TEMP,fanMode,onX10,offX10,currentTempX10,valid,fanOutput
    ADC,tpsRaw,tempRaw,tpsRefRaw,hvCenter,hvSide,vbatRaw,hardwareFault,fanOutput

PROFILE dapat mempersempit CAPS; UI memakai irisan keduanya. currentTempX10
adalah -32768 bila tidak tersedia; tampilkan “Sensor belum valid”.

### MODE, LEARN, OTA, CELL

    MODE,operatingMode,diyUnplugConfirmed,proEnabled,firstStartProven
    LEARN,state,coverage,accepted,rejected,sideSamples,sideOffsetCdeg
    OTA,state,received,expected,errorCode
    CELL,tpsIndex,rpmIndex,advanceCdeg

CELL readback memakai centidegree, sedangkan MAP,CELL write memakai 0,1 derajat.

---

## 8. Telemetry biner

Semua integer multibyte little-endian.

| Offset | Ukuran | Header |
|---:|---:|---|
| 0 | 2 | Magic 0xCD15 |
| 2 | 1 | Versi 3 |
| 3 | 1 | 0 CORE, 1 DIAGNOSTIC |
| 4 | 2 | Sequence |
| 18 | 2 | CRC16-CCITT byte 0–17 |

CORE:

| Offset | Tipe | Isi |
|---:|---|---|
| 6 | uint16 | RPM |
| 8 | uint16 | TPS 0–1000 permille |
| 10 | int16 | Advance centidegree |
| 12 | uint16 | Aki centivolt |
| 14 | uint16 | HV Core/Center volt |
| 16 | uint16 | HV Side volt |

DIAGNOSTIC:

| Offset | Tipe | Isi |
|---:|---|---|
| 6 | int16 | Suhu centidegree; INT16_MIN tidak valid |
| 8 | uint8 | Slot map 0–3 |
| 9 | uint8 | 0 fire, 1 soft cut, 2 hard cut |
| 10 | uint8 | Flags utama |
| 11 | uint8 | Flags output |
| 12 | uint16 | Fault bits |
| 14 | uint16 | Trigger angle centidegree |
| 16 | uint8 | Pickup quality 0–100 |
| 17 | uint8 | First Start detik |

Flags output: bit 0 CORE/CENTER, bit 1 SIDE, bit 2 strobe, bit 3 fan.
Status modul berasal dari MODULES. Decoder v3/v4 lama dipertahankan.

---

## 9. Binding lokal aplikasi

R9.2 mengirim firmwareEnforced=0. Firmware belum mengautentikasi ponsel;
binding adalah perlindungan UI agar pengguna tidak salah mengubah CDI. Ini
bukan DRM atau keamanan kriptografis.

Tanpa binding: scan/connect, telemetry read-only, versi, serial, status, fault,
dan Buku Petunjuk. Binding cocok wajib untuk Setup, modul, map, tuning, limiter,
profile, fan, kalibrasi suhu, OEM Learn, OTA, dan reset.

Aturan:

1. Baca IDENTITY setiap koneksi.
2. Jangan bind UNAVAILABLE.
3. Serial adalah identifier publik, bukan password.
4. Perangkat baru meminta konfirmasi sebelum mengganti binding aktif.
5. Raw BLE console juga tunduk pada write gate.

### Binding bukan pairing Bluetooth

Developer harus membedakan tiga hal:

| Istilah | Pelaku | Fungsi |
|---|---|---|
| BLE connection | Android ↔ ESP32 CDI | Telemetry dan command |
| App binding | Database lokal aplikasi ↔ serial CDI | Mencegah salah menulis CDI |
| BT audio pairing | Android ↔ receiver A2DP eksternal | Mengirim suara mesin |

Ketiganya independen. Menghapus pairing receiver audio tidak menghapus binding
CDI. Unbind CDI juga tidak menghapus pairing Bluetooth Android.

### Penyimpanan binding

Simpan satu record per serial, bukan hanya satu variabel global:

~~~kotlin
data class BindingRecord(
    val serial: String,
    val appInstanceId: String,
    val vehicleName: String?,
    val boundAtEpochMs: Long,
    val lastFirmwareSemver: String,
    val lastBuildId: String
)

interface BindingStore {
    suspend fun find(serial: String): BindingRecord?
    suspend fun save(record: BindingRecord)
    suspend fun remove(serial: String)
}
~~~

appInstanceId dibuat satu kali per instalasi dan disimpan terenkripsi. Record
binding boleh berada di DataStore terenkripsi; jangan simpan serial sebagai
secret karena serial bersifat publik.

### Flow koneksi pertama

1. Hubungkan BLE dan selesaikan initial sync sampai IDENTITY diterima.
2. Jika serial UNAVAILABLE, tampilkan mode read-only dan pesan bahwa firmware
   tidak menyediakan identitas valid.
3. Cari BindingStore berdasarkan serial.
4. Jika record ditemukan, state menjadi BOUND dan lanjut full sync.
5. Jika belum ada, tampilkan dialog **Perangkat baru** berisi serial, versi,
   build, dan nama BLE.
6. Tombol **Lihat saja** masuk READY_READ_ONLY.
7. Tombol **Bind dan lanjutkan** menyimpan record lokal, lalu masuk READY_FULL.
8. Binding tidak mengirim command apa pun ke firmware R9.2.

### Flow koneksi berikutnya

| Kondisi | BindingState | Hasil |
|---|---|---|
| Serial ada dalam BindingStore | BOUND | Fitur tulis tersedia setelah sync |
| Serial baru | UNBOUND | Read-only sampai pengguna bind |
| Serial UNAVAILABLE | UNAVAILABLE | Read-only |
| IDENTITY unsupported | LEGACY | Ikuti kebijakan adapter legacy |
| Record rusak/tidak dapat didekripsi | ERROR | Read-only dan tawarkan reset binding lokal |

### Menu Perangkat → Binding

Halaman harus menampilkan:

- Serial Number CDI;
- nama kendaraan lokal;
- status Terikat/Belum terikat/Legacy;
- firmware release, semver, build, platform;
- tanggal binding;
- tombol Bind, Ubah nama, dan Lepaskan binding;
- penjelasan bahwa binding R9.2 hanya berlaku di aplikasi ini.

Lepaskan binding hanya menghapus record lokal setelah konfirmasi. Jangan kirim
factory reset atau SETUP,RESET. Setelah unbind, sesi langsung turun menjadi
READY_READ_ONLY dan semua tombol tulis dinonaktifkan.

### Gate tulis pusat

Jangan mengulang logika binding pada setiap Screen. Semua tindakan mutasi harus
melewati satu fungsi:

~~~kotlin
fun WritePolicy.check(
    session: DeviceSessionState,
    requiredCapability: String? = null,
    requiresStoppedEngine: Boolean = false
): WriteDecision {
    if (session.phase != READY_FULL) return Denied("Perangkat belum siap")
    if (session.binding !is Bound) return Denied("Binding diperlukan")
    if (requiredCapability != null &&
        requiredCapability !in session.caps.tokens) return Denied("Tidak didukung")
    if (requiresStoppedEngine && !session.status.safeStopped)
        return Denied("Matikan mesin dan tunggu HV <30 V")
    return Allowed
}
~~~

Setup, MODULE SET, map, limiter, profile, fan, OEM Learn, OTA, reset, dan raw
console write wajib melewati policy tersebut. SoundScreen Android dan pemilihan
rute A2DP tidak memerlukan binding karena tidak menulis firmware.

---

## 10. Setup baru: satu flow, tiga layar

Enam tab lama Baru/Pulser/TDC/TPS/First Start/Ready tidak lagi menjadi state
utama UI.

### Layar 1 — Pemasangan

1. Ganti CDI OEM — Core 1 Coil, default.
2. Ganti CDI OEM — Dual Coil, jika SIDE dipasang.
3. Setup Lanjutan untuk OEM Learn/Manual/DIY.

Setelah konfirmasi CDI OEM dilepas:

    SETUP,INSTALL,CORE,OEM_REMOVED
    SETUP,INSTALL,DUAL,OEM_REMOVED

Respons: ACK,INSTALL_CORE atau ACK,INSTALL_DUAL. Gate tetap mengikuti
commissioning.

### Layar 2 — Pemeriksaan

| Pemeriksaan | Instruksi | Perintah |
|---|---|---|
| Pickup | Starter beberapa detik, lepaskan, tunggu RPM 0 dan HV <30 V | SETUP,PICKUP,CONFIRM |
| Edge | Pilih hanya bila pickup mengharuskan | SETUP,EDGE,FALLING atau RISING |
| PPR | Nilai 1..maxPpr | SETUP,PPR,n |
| Gate | 40..150 µs | SETUP,GATE_US,n |
| Strobe | Aktifkan saat kalibrasi | SETUP,STROBE,ON/OFF |
| TDC otomatis | Simpan sampel strobe | SETUP,SAVE_TDC |
| TDC manual | Hanya nilai terverifikasi | SETUP,MANUAL_TDC,centidegree,CONFIRM |
| TPS tertutup | Throttle benar-benar tertutup | SETUP,TPS,CLOSED |
| TPS terbuka | Throttle penuh | SETUP,TPS,OPEN |

Pickup dan TDC wajib. TPS belum valid menjadi peringatan karena load map tidak
akurat.

### Layar 3 — First Start dan Ready

1. Mesin berhenti dan HV <30 V.
2. Kirim SETUP,FIRST_START.
3. Nyalakan mesin tanpa membuka gas berlebihan.
4. Firmware membatasi 3.000 RPM dan advance maksimum 10°.
5. Setelah stabil minimal tiga detik, matikan mesin.
6. Tunggu RPM 0 dan HV <30 V.
7. Core: SETUP,READY,CENTER.
8. Dual: SETUP,READY,DUAL,sideOffsetCentidegree.
9. Query ulang COMMISSION, SETUP, MODULES, STATUS.
10. Tampilkan selesai hanya bila COMMISSION.ready=1.

~~~mermaid
flowchart TD
    A["Pemasangan Core/Dual"] --> B["Pickup dan TDC"]
    B --> C["Kalibrasi TPS"]
    C --> D["First Start terbatas"]
    D --> E{Mesin stabil?}
    E -- Tidak --> D
    E -- Ya --> F["Matikan mesin"]
    F --> G["Konfirmasi Ready"]
~~~

OEM Learn hanya menu lanjutan:

    MODE,OEM_LEARN
    LEARN,START
    LEARN,STOP
    LEARN,ABORT
    MODE,MANUAL
    MODE,DIY,OEM_UNPLUGGED

---

## 11. Modul dan dashboard dinamis

    MODULE,SET,SIDE,ON|OFF
    MODULE,SET,THERMAL,ON|OFF
    MODULE,SET,OEM_LEARN,ON|OFF
    MODULE,SET,AUX,ON|OFF
    MODULE,SET,TPS_DIAG,ON|OFF

Syarat: RPM 0, HV tidak aktif, HVC/HVS <30 V. Setelah ACK, query ulang
MODULES, SETUP, TEMP, dan STATUS.

Konfigurasi firmware di aplikasi harus berupa lima switch/checkbox independen.
Mengaktifkan THERMAL tidak boleh mematikan SIDE; mengaktifkan OEM_LEARN tidak
boleh menghapus AUX atau TPS_DIAG. Pilihan Core/Dual berada di bagian profil
pengapian, terpisah dari daftar modul. Receiver BT audio ditampilkan di bagian
aksesori aplikasi, bukan dicampur ke installedMask firmware.

| Kondisi | UI |
|---|---|
| Core saja | HV Core/Center — J1.12 |
| SIDE installed, belum active | HV Side — J1.6, status Belum aktif |
| SIDE active | Dua kartu HV dan label Dual Coil |
| THERMAL tidak installed | Sembunyikan suhu/fan |
| THERMAL installed, valid=0 | Sensor belum valid |
| THERMAL installed, valid=1 | Suhu, mode fan, ambang, relay |
| OEM_LEARN tidak installed | Sembunyikan OEM Learn |
| AUX tidak installed | Sembunyikan kontrol strobe |
| AUX installed | Tampilkan kontrol strobe |
| TPS_DIAG installed | Tampilkan TPS raw, reference, range, dan status diagnosis |
| BT audio dikonfigurasi lokal | Tampilkan status receiver dan rute A2DP Android |
| Semua modul firmware installed | Tampilkan SIDE, THERMAL, OEM Learn, AUX, dan TPS Diagnostic sekaligus |

Nama CENTER Cap dan SIDE Cap lama diganti menjadi HV Core/Center (J1.12) dan
HV Side (J1.6). Jangan menyimpulkan modul dari tipe motor.

### Dependensi modul

| Modul | Dapat berdampingan | Syarat sebelum dinyatakan siap |
|---|---|---|
| SIDE | Semua modul | Dual dipilih, coil J1.6 dipasang, offset valid |
| THERMAL | Semua modul | Sensor valid; kalibrasi diperlukan untuk nilai akurat/AUTO |
| OEM_LEARN | Semua modul | Hardware isolator terpasang; sesi learn hanya dari menu lanjutan |
| AUX | Semua modul | Hardware strobe terpasang |
| TPS_DIAG | Semua modul | Jalur reference/signal terpasang dan rentang dapat dibaca |

installed, active, observed, dan fault ditampilkan per modul. Satu modul fault
tidak boleh membuat aplikasi menyembunyikan atau menonaktifkan status modul
lain yang sehat.

Receiver BT audio bukan bagian GET,MODULES karena tidak mempunyai jalur status
ke ESP32. Simpan pilihan “receiver dipasang” sebagai AccessoryConfig lokal
berdasarkan Serial CDI. Status koneksi dan rute audio berasal dari Android.

---

## 12. Map, limiter, profile, fan, temperature, dyno

### Map R9

    MAP,BEGIN,rpmCount,tpsCount
    MAP,RPM,index,rpm
    MAP,LOAD,index,loadPercent
    MAP,CELL,rpmIndex,tpsIndex,advanceX10Degree
    MAP,SAVE,slot
    MAP,SELECT,slot

- rpmCount 2..min(32, CAPS.maxRpmPoints);
- tpsCount 2..min(16, CAPS.maxLoadPoints);
- RPM <= CAPS.maxRpm dan axis naik ketat;
- load 0..100 dan axis naik ketat;
- advance memakai irisan CAPS dan PROFILE;
- slot 0 sampai CAPS.mapSlots-1;
- status tersimpan hanya setelah ACK,MAP_SAVED;
- setelah save/select, query META dan CELL.

Readback:

    GET,CELL,tpsIndex,rpmIndex

Konversi:

    displayDegrees = advanceCdeg / 100.0
    writeAdvanceX10 = round(displayDegrees * 10)

Perintah legacy LOAD/SAVE/LIMIT/LIVE dipakai hanya oleh adapter firmware lama.

### Perintah tuning lain

    SET,LIMIT,rpm
    SET,FAN,mode,onX10,offX10
    SET,PROFILE,name,rpmMin,rpmMax,advanceMinX10,advanceMaxX10,ppr,triggerX10
    TEMP,CAL,adc0,temp0X10,adc1,temp1X10,adc2,temp2X10
    DYNO,BEGIN
    DYNO,TRIM,advanceX10
    DYNO,COMMIT
    DYNO,ABORT

Limiter memakai PROFILE yang dijepit CAPS, bukan 3.000..11.500. Menu fan hanya
muncul jika FAN ada dan THERMAL installed. Dyno TRIM -200..200 berarti
-20,0°..+20,0° dan belum permanen sebelum COMMIT.

### Modul Bluetooth audio eksternal

Suara tetap dibuat oleh aplikasi Android. Jalur datanya:

    Telemetry RPM: ESP32 CDI --BLE--> Android
    Audio mesin: Android --Classic Bluetooth A2DP--> receiver BT audio
    Suara fisik: receiver/amp --> speaker

Tidak ada GET,AUDIO, AUDIO,SET, atau AUDIO,CONFIG pada firmware. GPIO23
AUDIO_PWM tetap reserved dan LOW. Pilihan silinder, karakter suara, dan volume
tetap merupakan state SoundScreen Android.

BLE CDI dan A2DP dapat digunakan bersamaan karena merupakan koneksi dan profil
berbeda. Aplikasi tidak boleh menganggap “CDI BLE tersambung” berarti receiver
audio juga tersambung.

Status UI BT audio adalah gabungan dua sumber:

| Data | Sumber | Label UI |
|---|---|---|
| Modul dipasang | AccessoryConfig lokal per serial CDI | Terpasang/dikonfigurasi |
| Receiver paired | Android Bluetooth settings/API | Sudah dipasangkan |
| Receiver connected | BluetoothProfile.A2DP | Tersambung |
| Audio diarahkan ke receiver | AudioManager/current media route | Audio aktif ke modul |
| Modul benar-benar menyala | Tidak dapat diketahui tanpa pin sense | Tidak dapat diverifikasi |

Urutan status yang disarankan:

1. Tidak dipasang.
2. Dipasang, belum dipasangkan.
3. Dipasangkan, belum tersambung.
4. Tersambung, tetapi bukan rute media aktif.
5. Siap — rute media A2DP aktif.

Modul harus berupa receiver **Classic Bluetooth A2DP**. Modul BLE-only tidak
dapat menerima audio media Android. Jangan menjalankan A2DP pada ESP32 CDI yang
sama karena firmware ini hanya menyediakan BLE kontrol/telemetry dan tugas
pengapian harus tetap diprioritaskan.

Potensi gangguan bukan konflik protokol, tetapi interferensi RF/catu daya.
Gunakan receiver eksternal, pisahkan antena dari ESP32/trafo/coil, beri
decoupling lokal, dan jangan mengambil arus amplifier besar dari 3V3 ESP32.

---

## 13. Safety dan penanganan error

Write aktif hanya jika sesi READY_FULL, binding cocok, capability ada, dan
status cukup baru. Firmware tetap pemberi izin final. Jika telemetry basi,
disable tombol dan query STATUS.

| ERR firmware | Pesan pengguna | Aksi aplikasi |
|---|---|---|
| CRC | Data komunikasi rusak | Retry terbatas |
| GET | Query belum didukung | Tandai unsupported bila opsional |
| STOP_ENGINE_WAIT_HV_LT30 | Matikan mesin dan tunggu HV <30 V | Poll STATUS, jangan auto-retry write |
| INSTALL_CORE_OR_DUAL_OEM_REMOVED | Pilih Core/Dual dan pastikan OEM dilepas | Kembali ke Pemasangan |
| PICKUP_NOT_STABLE | Pickup belum stabil | Tampilkan quality, ulang starter |
| START_STROBE_FIRST | Aktifkan strobe | Tawarkan tombol strobe |
| STROBE_SAMPLES | Sampel belum cukup | Lanjut sampling |
| TPS_RANGE | Rentang TPS tidak valid | Ulang closed/open |
| FIRST_START_NOT_PROVEN | First Start belum stabil | Ulang First Start |
| SIDE_MODULE_NOT_CONFIGURED | SIDE belum dikonfigurasi | Buka modul |
| SIDE_OFFSET | Offset SIDE belum valid | Kalibrasi offset |
| OEM_LEARN_UNAVAILABLE | OEM Learn belum diaktifkan | Tampilkan syarat modul |
| LEARN_NEEDS_20_VALID_PULSES | Sampel OEM belum cukup | Lanjut sampling |
| MAP_BEGIN_FIRST | Transaksi map belum dimulai | Ulang BEGIN |
| MAP_INVALID | Grid/map tidak valid | Sorot axis/cell |
| LIMIT_RANGE | Limiter di luar profil | Muat PROFILE/CAPS |
| FAN_RANGE | Ambang fan tidak valid | Perbaiki hysteresis |
| PROFILE_RANGE | Profil di luar capability | Muat CAPS |
| TEMP_CAL_RANGE | Kalibrasi tidak valid | Periksa tiga titik |
| DYNO_BEGIN_FIRST | Sesi dyno belum dimulai | Kembali ke BEGIN |
| OTA_NOT_SAFE | Kondisi OTA tidak aman | Matikan mesin/tunggu HV |
| OTA_VERIFY | Verifikasi image gagal | Minta file yang benar |
| FLASH | Penyimpanan gagal | Jangan tandai tersimpan |
| UNKNOWN | Perintah tidak dikenal | Tampilkan mismatch protokol |

Jangan auto-retry perintah tulis kecuali dipastikan idempotent. Query boleh
di-retry.

---

## 14. OTA

1. Pastikan VERSION.platform=ESP32.
2. Pastikan capability OTA dan binding cocok.
3. Validasi image/manifest untuk ESP32 R9.
4. Hitung panjang dan CRC32.
5. Kirim OTA,BEGIN,imageVersion,length,crc32.
6. Setelah ACK, kirim data ke OTA Data.
7. Pantau OTA Status dan GET,OTA.
8. Commit sesuai protokol aktif.
9. Tunggu reboot, reconnect, full handshake.
10. Sukses hanya bila VERSION/build baru terbaca.

ESP32 R9 mempertahankan notification OTA Status ringkas dua byte state/error;
GET,OTA adalah status rinci authoritative. Hapus asumsi
OTA_IMAGE_VERSION=80200. Jangan kirim binary STM32 ke ESP32 atau sebaliknya.

---

## 15. Struktur model yang disarankan

~~~kotlin
data class FirmwareCaps(
    val maxRpm: Int,
    val advanceRangeX10: IntRange,
    val maxRpmPoints: Int,
    val maxLoadPoints: Int,
    val mapSlots: Int,
    val maxPpr: Int,
    val tokens: Set<String>
)

data class ModuleStatus(
    val installedMask: Int,
    val activeMask: Int,
    val observedMask: Int,
    val faultMask: Int,
    val coreProfile: Int
)

data class CommissionStatus(
    val stage: Int,
    val nextAction: Int,
    val ready: Boolean,
    val advisoryMask: Int
)

data class DeviceSessionState(
    val phase: SessionPhase,
    val identity: FirmwareIdentity?,
    val caps: FirmwareCaps?,
    val modules: ModuleStatus?,
    val commission: CommissionStatus?,
    val binding: BindingState,
    val lastStatusAtMs: Long,
    val unsupportedQueries: Set<String>
)
~~~

Gunakan sealed response: InfoResponse, VersionResponse, IdentityResponse,
CapsResponse, ModulesResponse, CommissionResponse, AckResponse, ErrorResponse.
Hindari satu blok onResponse besar yang mengubah banyak StateFlow.

ACK operation minimal:

- PONG_R9 dan PONG_ legacy;
- INSTALL_CORE, INSTALL_DUAL;
- MODULE_ON, MODULE_OFF;
- MAP_BEGIN, MAP_RPM, MAP_LOAD, MAP_CELL, MAP_SAVED, MAP_SELECTED;
- PROFILE, TEMP_CAL;
- DYNO_BEGIN, DYNO_TRIM, DYNO_COMMIT, DYNO_ABORT;
- operation setup lama dan READY_THREE.

ACK operation baru yang belum dikenal tetap sukses generik bila frame dan
sequence sah.

---

## 16. Navigasi hasil migrasi

| Menu | Isi |
|---|---|
| Dashboard | Telemetry, output, modul, fault |
| Maps | Slot, grid, limiter, profile |
| Setup | Tiga layar commissioning dan modul |
| Suara | Simulasi mesin Android dan status rute receiver A2DP eksternal |
| Buku | Hardware, J1, Core/Dual, status, troubleshooting |
| Perangkat | BLE, serial, firmware, platform, binding, OTA, diagnostics |

Setup Lanjutan hanya menampilkan fitur CAPS. Buku memakai
BUKU_PETUNJUK_PENGGUNA.md. Editor PCB/workshop skematik tidak masuk UI produk
massal. Sound diberi label “Simulasi suara Android”; receiver A2DP ditampilkan
sebagai rute keluaran, bukan sebagai penghasil suara firmware.

---

## 17. Firmware lama dan STM32

Gunakan adapter terpisah:

~~~kotlin
interface CdiDeviceAdapter {
    suspend fun initialSync(): DeviceSnapshot
    fun supports(feature: String): Boolean
    suspend fun execute(action: DeviceAction): DeviceResult
}

class Esp32R9Adapter : CdiDeviceAdapter
class LegacyEsp32Adapter : CdiDeviceAdapter
class Stm32Adapter : CdiDeviceAdapter
~~~

| Kondisi | Perilaku |
|---|---|
| VERSION unsupported | Gunakan INFO, versi detail tidak tersedia |
| IDENTITY unsupported | Jangan buat serial palsu; legacy read-only/konfirmasi sesi |
| MODULES unsupported | UI legacy, jangan klaim deteksi modul |
| COMMISSION unsupported | Mapping setup lama hanya fallback |
| CAPS lama | Batas legacy khusus adapter, bukan global |
| Platform STM32 | Stm32Adapter dan panduan STM32 |

Jangan memutus koneksi karena query tambahan mengembalikan ERR,GET.

---

## 18. Cookbook integrasi Android untuk developer pemula

Bagian ini adalah urutan implementasi minimum. Jangan mulai dari perubahan UI
acak; selesaikan lapisan sesuai urutan.

### 18.1 Lapisan aplikasi

| Lapisan | Tanggung jawab | Tidak boleh dilakukan |
|---|---|---|
| BleTransport | Scan, connect, subscribe, frame, queue, timeout | Mengubah state Setup |
| ProtocolParser | String/binary menjadi model bertipe | Menampilkan Toast/UI |
| DeviceRepository | Menggabungkan response menjadi DeviceSnapshot | Menyimpan state demo |
| BindingStore | Record binding lokal per serial | Mengirim command firmware |
| WritePolicy | Memutuskan fitur tulis boleh/tidak | Menggantikan safety firmware |
| ViewModel | Mengubah snapshot menjadi UiState dan action | Parsing frame mentah |
| Screen | Render UiState dan mengirim intent | Memanggil BluetoothGatt langsung |
| AudioRouteRepository | Status A2DP/rute media Android | Mengklaim status dari GET,MODULES |

### 18.2 Startup aplikasi

1. Muat appInstanceId dan BindingStore.
2. Buat DemoSessionState terpisah; jangan menyalinnya ke DeviceSessionState.
3. Minta permission sesuai versi Android.
4. Tampilkan Dashboard dalam kondisi disconnected.
5. Tombol Hubungkan membuka scan BLE CDI.

Permission minimum:

| Android | BLE CDI | Status receiver audio |
|---|---|---|
| Android 12+ | BLUETOOTH_SCAN dan BLUETOOTH_CONNECT runtime | BLUETOOTH_CONNECT untuk membaca perangkat terhubung |
| Android 11 ke bawah | ACCESS_FINE_LOCATION untuk scan BLE | Gunakan API Bluetooth yang tersedia pada versi tersebut |

Aplikasi tidak perlu meminta pairing ESP32 untuk menjalankan BLE protocol yang
sekarang. Receiver audio dipasangkan melalui pengaturan Bluetooth Android
sebagai perangkat media.

### 18.3 Connect dan initial sync

~~~kotlin
suspend fun connectAndSync(device: BluetoothDevice) {
    session.updatePhase(CONNECTING)
    transport.connect(device)
    session.updatePhase(SUBSCRIBING)
    transport.subscribe(RESPONSE_UUID)
    transport.subscribe(TELEMETRY_UUID)
    session.updatePhase(SYNCING)
    val result = syncCoordinator.runInitialQueries()
    if (!result.requiredOk) {
        session.enterDegraded(result)
        return
    }
    bindingCoordinator.evaluate(result.identity)
    session.publishSnapshot(result.snapshot)
}
~~~

Setiap query masuk queue yang sama. Jangan mengirim 17 frame sekaligus.
Tunggu response/ERR/timeout command sebelumnya. Query opsional yang unsupported
dicatat lalu dilanjutkan.

### 18.4 Evaluasi binding

1. Parser menerima IDENTITY.
2. BindingCoordinator mencari record berdasarkan serial.
3. Jika cocok: READY_FULL.
4. Jika tidak cocok: NEEDS_BINDING.
5. Dialog binding hanya muncul setelah VERSION dan IDENTITY tersedia agar
   pengguna melihat perangkat yang benar.
6. Setelah Bind ditekan, simpan record dan ubah state; tidak ada command BLE.
7. Jalankan refresh MODULES, COMMISSION, STATUS sebelum membuka Setup.

### 18.5 Konfigurasi seluruh modul dan aksesori

Pada Setup → Hardware Terpasang tampilkan lima switch firmware independen:

| Switch | Command | Status sesudah ACK |
|---|---|---|
| Coil SIDE | MODULE,SET,SIDE,ON/OFF | Refresh MODULES dan SETUP |
| Thermal/Fan | MODULE,SET,THERMAL,ON/OFF | Refresh MODULES, TEMP |
| OEM Learn | MODULE,SET,OEM_LEARN,ON/OFF | Refresh MODULES, MODE, LEARN |
| AUX/Strobe | MODULE,SET,AUX,ON/OFF | Refresh MODULES |
| TPS Diagnostic | MODULE,SET,TPS_DIAG,ON/OFF | Refresh MODULES, ADC |

Semua command MODULE SET memerlukan mesin berhenti dan HV <30 V. UI boleh
mengirim beberapa perubahan, tetapi harus satu per satu dan menunggu ACK.
Jika perubahan ketiga gagal, jangan mengubah dua modul sebelumnya kembali
secara lokal; refresh MODULES dan tampilkan hasil firmware sebenarnya.

Di bawahnya tampilkan bagian **Aksesori aplikasi**:

| Aksesori | Penyimpanan | Status |
|---|---|---|
| Receiver BT audio eksternal | AccessoryConfig lokal dengan key Serial CDI | Android paired/connected/current media route |

Mengubah aksesori BT audio tidak mengirim command ke firmware dan tidak
memerlukan mesin berhenti. Bila serial belum tersedia, jangan simpan konfigurasi
aksesori sebagai milik CDI tertentu.

### 18.6 Flow pengguna baru dengan semua modul

1. Pasang hardware lalu hidupkan kontak tanpa starter.
2. Hubungkan BLE CDI.
3. Selesaikan binding lokal.
4. Buka Hardware Terpasang dan aktifkan semua modul firmware yang benar-benar ada.
5. Jika receiver audio dipasang, aktifkan aksesori BT Audio pada aplikasi dan
   pasangkan receiver melalui Android.
6. Pilih Core atau Dual pada profil pengapian.
7. Jalankan Pemeriksaan pickup, TDC, dan TPS.
8. Jalankan First Start lalu konfirmasi Ready.
9. Dashboard mulai dari MODULES/STATUS, bukan pilihan lokal.
10. Buka Suara, pilih preset simulasi, periksa rute media, lalu Play.

### 18.7 Refresh setelah command

| Command berhasil | Query ulang wajib |
|---|---|
| MODULE,SET | MODULES dan query modul terkait |
| SETUP apa pun | COMMISSION, SETUP, STATUS |
| SET/FAN | TEMP, MODULES |
| SET/PROFILE | PROFILE, CAPS, META, SETUP |
| MAP/SAVE atau SELECT | META lalu CELL |
| MODE/LEARN | MODE, LEARN, COMMISSION |
| OTA selesai | Reconnect dan full initial sync |

ACK hanya berarti command diterima. Tampilan final selalu berasal dari query
ulang, bukan dari optimistik state.

### 18.8 Menu dan gate

| Menu | Tanpa CDI | CDI read-only | CDI bound/full |
|---|---|---|---|
| Dashboard | Demo/offline | Telemetry | Telemetry |
| Maps | Lihat cache/demo | Lihat saja | Edit/save |
| Setup | Buku ringkas | Lihat status | Jalankan setup/modul |
| Suara | Simulasi HP/BT audio | Sama | Sama |
| Buku | Aktif | Aktif | Aktif |
| Perangkat | Scan | Versi/serial/bind | Binding/OTA/diagnostik |

### 18.9 Disconnect dan reconnect

Saat BLE putus:

1. batalkan command in-flight;
2. tandai telemetry stale;
3. nonaktifkan seluruh tombol write;
4. jangan mengubah commissioning atau installedMask;
5. suara Android boleh dihentikan atau tetap berjalan sesuai pilihan UX, tetapi
   RPM harus turun ke nol bila telemetry sudah stale;
6. setelah reconnect, lakukan full initial sync dan evaluasi binding lagi.

### 18.10 Mapping response ke UI

| Response | State aplikasi | Konsumen |
|---|---|---|
| VERSION/INFO | identity/platform/protocol | Perangkat, adapter |
| IDENTITY | serial/binding policy | BindingCoordinator |
| CAPS/HARDWARE | feature availability | Navigasi dan control ranges |
| MODULES | module state | Dashboard, Setup Hardware |
| COMMISSION/SETUP | setup state | Setup tiga layar |
| STATUS/telemetry | live state | Dashboard, safety gate |
| META/CELL/PROFILE | tuning state | Maps |
| TEMP/ADC | sensor state | Dashboard, diagnosis |
| MODE/LEARN | advanced setup | OEM Learn |
| OTA | update state | Perangkat/OTA |
| Android A2DP route | audio route state | SoundScreen |

---

## 19. Rencana implementasi

### Tahap 1 — Parser dan session

1. Tambah model respons.
2. Perbaiki PONG_R9 dan scan UUID.
3. Tambah initial sync.
4. Pisahkan demo/device state.
5. Jadikan firmware authority.

### Tahap 2 — Binding dan navigasi

1. Tambah appInstanceId/BindingRecord terenkripsi.
2. Tambah halaman Perangkat.
3. Terapkan write gate pusat.
4. Wiring menjadi Buku.
5. Hapus QuickSetupGuide ganda.

### Tahap 3 — Setup

1. Bangun tiga layar.
2. Gunakan COMMISSION.nextAction.
3. Tambah INSTALL CORE/DUAL.
4. Pindahkan mode lanjutan.
5. Query ulang setelah ACK.

### Tahap 4 — Dashboard/modul

1. Parse MODULES/TEMP.
2. Kartu dinamis.
3. Konfigurasi modul dengan safety.

### Tahap 5 — Map/tuning

1. Hapus hard-code 11.500/36°.
2. Editor dari CAPS/META/PROFILE.
3. Transaksi MAP.
4. Fan/temp/dyno.

### Tahap 6 — OTA/legacy

1. Validasi platform/image.
2. Versi image nyata.
3. Reconnect dan verifikasi VERSION.
4. Uji adapter ESP32 R9, ESP32 lama, STM32.

---

## 20. Matriks pengujian

### Koneksi/protokol

- [ ] Scan menemukan NS200-CDI melalui service UUID.
- [ ] ACK,PONG_R9 menyelesaikan preflight.
- [ ] Fragmentasi notification aman.
- [ ] CRC/sequence salah tidak mengubah state.
- [ ] ERR,GET opsional tidak memutus koneksi.
- [ ] Reconnect melakukan full sync.
- [ ] Capability baru tidak membuat parser gagal.

### Binding

- [ ] Serial stabil setelah restart.
- [ ] UNAVAILABLE tidak dapat dibinding.
- [ ] Perangkat tidak cocok read-only.
- [ ] Raw console tidak melewati gate.

### Setup

- [ ] Core selesai tanpa SIDE.
- [ ] Dual tidak READY tanpa SIDE/offset.
- [ ] nextAction mengendalikan CTA.
- [ ] Cache lokal tidak memalsukan READY.
- [ ] Disconnect kembali ke state firmware.

### Modul/dashboard

- [ ] Core tidak menampilkan HV Side fault.
- [ ] SIDE belum observed tampil Belum diuji.
- [ ] THERMAL off menyembunyikan fan.
- [ ] Suhu invalid tidak menjadi angka.
- [ ] Label J1.12/J1.6 benar.
- [ ] installedMask 31 menampilkan seluruh modul firmware tanpa saling menonaktifkan.
- [ ] BT audio configured lokal tidak diklaim connected tanpa status A2DP Android.
- [ ] Receiver A2DP putus tidak memutus BLE CDI.
- [ ] BLE CDI reconnect tidak mengubah pairing receiver audio.

### Map/tuning

- [ ] Grid 2×2 sampai 32×16.
- [ ] Advance negatif dapat dibaca/ditulis.
- [ ] RPM >11.500 dapat dipakai jika profil mengizinkan.
- [ ] Save gagal tidak dianggap tersimpan.
- [ ] CELL centidegree dikonversi benar.
- [ ] Dyno disconnect tidak dianggap commit.

### OTA/demo/legacy

- [ ] Binary MCU salah ditolak.
- [ ] OTA kondisi tidak aman ditolak.
- [ ] Sukses setelah VERSION baru.
- [ ] Demo tidak pernah mengirim command.
- [ ] Koneksi nyata menghapus state demo.
- [ ] Firmware lama tetap read-only.
- [ ] STM32 memakai adapter sendiri.

---

## 21. Definition of Done

Migrasi selesai bila:

1. PONG_R7_2 bukan satu-satunya respons sah;
2. setup_stage lokal tidak dapat membuka READY/output;
3. tidak ada batas global 11.500 RPM, 36°, PPR 4, grid 16×8;
4. hanya ada satu flow setup;
5. setup normal langsung mengganti CDI OEM;
6. dashboard mengikuti MODULES/TEMP;
7. semua write melewati binding/safety gate;
8. map memakai transaksi R9 dengan unit benar;
9. OTA memverifikasi platform dan versi setelah reboot;
10. fallback firmware lama tidak merusak R9;
11. status BT audio membedakan installed, paired, connected, dan active route;
12. Binding menu dapat dipahami tanpa menganggap binding sebagai pairing;
13. matriks uji lulus.

---

## 22. Aturan evolusi protokol

1. Jangan ubah UUID, paket v3, atau arti bit lama diam-diam.
2. Fitur baru additive dan diiklankan CAPS.
3. Respons baru memakai schema.
4. Parser mengabaikan field tambahan.
5. installedMask bukan deteksi fisik.
6. BLE putus tidak mengubah izin pengapian.
7. Perubahan pin memperbarui board header, README, skematik, Buku, dokumen ini.
8. EFI belum menjadi capability R9.
9. Serial adalah identifier publik.
10. Selama firmwareEnforced=0, binding hanya membatasi UI aplikasi.


## 23. Additive protocol R9.3 — physical DET, AUX relay, timing presets

Queries:

    GET,MODULES
    GET,TIMING
    GET,AUX

MODULES v2:

    MODULES,2,presentMask,activeMask,observedMask,faultMask,coreProfile,configuredMask,ioOk

presentMask comes from PCF8574P U6 and is authoritative. configuredMask is the saved application choice. A removed module immediately disappears from present/active and appears in fault when still configured. The app must render the switch unchecked/inactive from presentMask, while retaining a separate “previously configured” warning when configuredMask remains set.

Timing configuration:

    SET,TIMING,mode,intensity,minRpm,maxRpm

| mode | name | behavior |
|---:|---|---|
| 0 | STANDARD | map only |
| 1 | SOFT | up to 4° retard in the selected window |
| 2 | RESPONSIVE | up to 2° advance, still clamped by PROFILE |
| 3 | KUDA | deterministic low-RPM retard pattern; intensity 8–10 may soft-cut at most 1/12 event |

Intensity is 0..10. Range is restricted to 500..5000 RPM and the effect is disabled above 20% TPS. Firmware still clamps the result to PROFILE/map safety limits. App labels must explain that KUDA is an idle/show effect, not a power map.

AUX commands:

    AUX,KEYLESS,ON
    AUX,KEYLESS,OFF
    AUX,START,PULSE,100..3000
    AUX,ALL,OFF

START is rejected unless AUX is physically detected, keyless is active, battery/fault state is valid and RPM <300. It is cancelled by timeout, RPM >=500, battery/fault failure, I2C failure or module removal. Hardware must keep the OEM neutral/clutch/side-stand interlock and OEM starter relay in series; firmware cannot replace an input that is not present on the header.

New capability tokens: MODULE_DET_PCF8574, TIMING_PRESETS, TIMING_KUDA, AUX_RELAY.
