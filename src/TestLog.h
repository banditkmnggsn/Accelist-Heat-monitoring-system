#pragma once

// =====================================================================
//  TestLog.h — log tes di flash (LittleFS), RANCANGAN §10
//
//  Per tes dua berkas di /log:
//    tNNNNN.res  CycleResult (biner, CRC32). Ditulis saat MULAI dengan
//                status BERJALAN, ditimpa saat tes selesai. Dipakai layar
//                Riwayat & "hasil terakhir" Beranda.
//    tNNNNN.sec  catatan per detik 8 bait (raw x10, suhu x100, duty, bendera),
//                ditambahkan sekali per menit supaya flash jarang ditulis.
//  Maksimal kMaxTests tes; sebelum tes baru yang tertua dihapus.
//  Saat boot, .res yang masih BERJALAN = alat mati di tengah tes ->
//  ditandai ERROR (RESET) dan id-nya dilaporkan ke layar (02-01d).
//
//  Thread: onStarted/onSecond/onFinished/onLogClosed dari task sensor
//  (core 0); list()/load() dari UI (core 1). Indeks di RAM dijaga mutex;
//  LittleFS sendiri punya kunci.
// =====================================================================

#include <Arduino.h>
#include <CycleController.h>

#include <atomic>

class TestLog : public hbcycle::CycleSink {
public:
    static constexpr uint8_t kMaxTests = 40;

    // Mount (format kalau belum ada), bangun indeks, tandai tes terputus.
    bool begin();
    bool mounted() const { return mounted_; }

    uint32_t nextId();  // id untuk tes baru (penghitung di NVS "hblog")

    // ---- CycleSink (task sensor) ----
    void onStarted(const hbcycle::CycleResult& header) override;
    void onSecond(const hbcycle::SecondLog& second) override;
    void onFinished(const hbcycle::CycleResult& result) override;
    void onLogClosed(uint32_t testId) override;

    // ---- UI ----
    // Terbaru lebih dulu. Return jumlah yang diisi.
    uint8_t list(hbcycle::HistoryEntry* out, uint8_t max);
    uint8_t count();
    bool last(hbcycle::HistoryEntry& out);
    bool load(uint32_t id, hbcycle::CycleResult& out);
    uint32_t interruptedId() const { return interruptedId_; }
    void clearInterrupted() { interruptedId_ = 0; }
    // PENGATURAN > OPSI PENGEMBANG > MEMORI LOG
    uint32_t totalTests() const { return totalTests_.load(); }  // tes yang pernah dimulai
    uint32_t freeKb() const { return freeKb_.load(); }
    uint32_t totalKb() const { return totalKb_.load(); }
    // Hapus semua log kecuali tes yang sedang dicatat. Nomor tes berlanjut.
    // false kalau ada berkas yang gagal dihapus.
    bool clearAll();

    // ---- serial (task sensor) ----
    void printList(Print& out);
    bool dump(uint32_t id, Print& out);

private:
    struct SecondRecord {
        int32_t raw10;
        int16_t temp100;
        uint8_t duty;   // 0,5 % per langkah
        uint8_t flags;  // bit0-3 fase, bit4 detik tidak sah, bit5 pintu terbuka
    };
    static_assert(sizeof(SecondRecord) == 8, "catatan per detik harus 8 bait");

    void flush();
    void rotate();
    void refreshUsage();  // ruang LittleFS (mahal: dipanggil setelah menulis saja)
    void indexPut(const hbcycle::CycleResult& r);
    void indexRemove(uint32_t id);
    bool readResult(const char* path, hbcycle::CycleResult& out);
    bool writeResult(const hbcycle::CycleResult& r);
    static hbcycle::HistoryEntry entryOf(const hbcycle::CycleResult& r);
    static void pathOf(char (&out)[24], uint32_t id, const char* ext);

    bool mounted_ = false;
    uint32_t interruptedId_ = 0;
    std::atomic<uint32_t> totalTests_{0};
    std::atomic<uint32_t> freeKb_{0};
    std::atomic<uint32_t> totalKb_{0};
    std::atomic<uint32_t> loggingId_{0};  // salinan activeId_ untuk clearAll (task UI)

    SemaphoreHandle_t lock_ = nullptr;
    hbcycle::HistoryEntry index_[kMaxTests] = {};  // urut naik menurut id
    uint8_t indexCount_ = 0;

    // tes yang sedang dicatat (hanya task sensor)
    uint32_t activeId_ = 0;
    SecondRecord buffer_[60] = {};
    uint8_t buffered_ = 0;
    uint32_t secondsWritten_ = 0;
};
