# Temporary tuning web

Web lokal sementara untuk membaca report firmware dari ESP32 melalui `COM5` pada `115200` baud.
Folder ini sengaja berdiri sendiri. Firmware utama tidak diubah dan tidak membutuhkan Wi-Fi.

## Menjalankan

```powershell
python -m pip install pyserial
python tuning_web/server.py
```

Buka http://127.0.0.1:8080. Tutup dengan `Ctrl+C`.

Jika port bukan `COM5`:

```powershell
$env:TUNING_SERIAL_PORT = "COM4"
python tuning_web/server.py
```

Serial Monitor PlatformIO harus ditutup saat web tuning berjalan karena satu port serial
tidak dapat dipakai dua program sekaligus. Tombol di halaman meneruskan perintah firmware
`t`, `z`, `r`, `p`, `?`, `e`, dan `c <gram>` (kalibrasi anak timbangan).

## Panel "Serial terakhir"

- **Bekukan** menghentikan tampilan sepenuhnya; baris baru tetap dikumpulkan di latar
  belakang dan penghitung menunjukkan berapa yang tertahan, jadi tidak ada data hilang.
- **Filter** menyaring baris berdasarkan potongan teks, misalnya `raw 1127`.
- **Unduh** menyimpan baris yang sedang tampil (ikut filter) ke berkas `.log`.

Server menyimpan 20000 baris terakhir (`TUNING_LOG_MAX`) dan halaman hanya mengambil
baris yang belum pernah diambil, sehingga membekukan tampilan tidak memutus pengumpulan.

## Panel "Kalibrasi multi-titik"

Urutan pemakaian, diulang untuk tiap anak timbangan:

1. Kosongkan timbangan, tekan **Rekam NOL** (perintah `n`).
2. Pilih beban dari tombol preset atau ketik sendiri dalam mg, pilih arah naik/turun.
3. Letakkan beban, tunggu diam, tekan **Beban sudah diletakkan** (perintah `m <mg> <u|d>`).
4. Angkat beban, tekan **Rekam NOL** lagi.

Langkah 4 penting: tiap titik dikoreksi terhadap rata-rata nol **sebelum dan sesudah**
pembebanan. Itu yang membatalkan drift nol, yang pada hardware ini jauh lebih besar
daripada noise. Titik tanpa nol penutup tetap terpakai, tapi hanya terkoreksi sebagian.

Firmware hanya mengukur dan mencetak baris `CALZERO,` / `CALPT,` berisi **raw counts**;
seluruh fit, residual, histeresis, dan repeatability dihitung di PC oleh `analysis.py`.
Data mentah karena itu tetap berguna kalau rumus konversinya diganti kemudian.

Kolom **± (mg)** dan **rasio S/N** di tabel menunjukkan apakah sebuah titik bermakna.
Anak timbangan dengan S/N di bawah 10 ditandai merah: pada load cell 300 g ini
ketidakpastian satu titik sekitar 0,5 mg, sehingga beban di bawah ~50 mg hasilnya
didominasi noise, bukan massa.

Tombol **Terapkan ke firmware** mengirim `s <countsPerGram>` hasil fit dan menyimpannya
ke NVS. Tiap sesi server menulis `data/calibration_<tanggal>_<jam>.csv` sendiri.

Setelah tuning selesai, hapus folder `tuning_web` dan tidak ada perubahan firmware yang perlu
dibersihkan.