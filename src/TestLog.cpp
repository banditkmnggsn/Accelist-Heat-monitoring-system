#include "TestLog.h"

#include <LittleFS.h>
#include <Preferences.h>
#include <esp_task_wdt.h>

#include <cinttypes>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstring>

namespace {

constexpr const char* kDir = "/log";
constexpr const char* kNvsNamespace = "hblog";
constexpr const char* kNvsNext = "next";
constexpr size_t kMinFreeBytes = 128u * 1024u;  // ruang untuk 1 tes + cadangan

uint32_t resultCrc(const hbcycle::CycleResult& r) {
    return hbcycle::crc32(reinterpret_cast<const uint8_t*>(&r), offsetof(hbcycle::CycleResult, crc));
}

bool resultValid(const hbcycle::CycleResult& r) {
    return r.magic == hbcycle::kResultMagic && r.version == hbcycle::kResultVersion && r.crc == resultCrc(r);
}

class Lock {
public:
    explicit Lock(SemaphoreHandle_t m) : m_(m) {
        if (m_ != nullptr) xSemaphoreTake(m_, portMAX_DELAY);
    }
    ~Lock() {
        if (m_ != nullptr) xSemaphoreGive(m_);
    }

private:
    SemaphoreHandle_t m_;
};

}  // namespace

void TestLog::pathOf(char (&out)[24], uint32_t id, const char* ext) {
    snprintf(out, sizeof(out), "%s/t%05" PRIu32 ".%s", kDir, id, ext);
}

hbcycle::HistoryEntry TestLog::entryOf(const hbcycle::CycleResult& r) {
    hbcycle::HistoryEntry e;
    e.id = r.id;
    e.startEpoch = r.startEpoch;
    e.moisturePct = r.moisturePct;
    e.status = r.status;
    e.reason = r.reason;
    e.hasMoisture = r.hasMoisture;
    e.validated = r.validated;
    e.flags = r.flags;
    return e;
}

bool TestLog::begin() {
    lock_ = xSemaphoreCreateMutex();
    mounted_ = LittleFS.begin(true);  // format otomatis kalau belum pernah dipakai
    if (!mounted_) {
        return false;
    }
    if (!LittleFS.exists(kDir)) {
        LittleFS.mkdir(kDir);
    }

    // Kumpulkan dulu, tulis ulang (tes terputus) setelah direktori ditutup
    static hbcycle::CycleResult scratch;
    uint32_t interrupted[4] = {};
    uint8_t interruptedCount = 0;
    File dir = LittleFS.open(kDir);
    if (dir) {
        for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
            const char* name = f.name();
            const size_t len = strlen(name);
            const bool isResult = len > 4 && strcmp(name + len - 4, ".res") == 0;
            if (!isResult || f.size() != sizeof(hbcycle::CycleResult)) {
                f.close();
                continue;
            }
            const bool read = f.read(reinterpret_cast<uint8_t*>(&scratch), sizeof(scratch)) == sizeof(scratch);
            f.close();
            if (!read || !resultValid(scratch)) {
                continue;
            }
            if (scratch.status == hbcycle::Status::Running && interruptedCount < 4) {
                interrupted[interruptedCount++] = scratch.id;
            }
            indexPut(scratch);
        }
        dir.close();
    }
    for (uint8_t i = 0; i < interruptedCount; ++i) {
        char path[24];
        pathOf(path, interrupted[i], "res");
        if (readResult(path, scratch)) {
            scratch.status = hbcycle::Status::Error;
            scratch.reason = hbcycle::Reason::Reset;
            scratch.hasMoisture = 0;
            scratch.validated = 0;
            scratch.crc = resultCrc(scratch);
            writeResult(scratch);
            indexPut(scratch);
            if (scratch.id > interruptedId_) interruptedId_ = scratch.id;
        }
    }
    Preferences prefs;
    if (prefs.begin(kNvsNamespace, true)) {
        const uint32_t next = prefs.getUInt(kNvsNext, 1);
        prefs.end();
        totalTests_.store(next > 0 ? next - 1 : 0);
    }
    refreshUsage();
    return true;
}

void TestLog::refreshUsage() {
    if (!mounted_) return;
    const size_t total = LittleFS.totalBytes();
    const size_t used = LittleFS.usedBytes();
    totalKb_.store(static_cast<uint32_t>(total / 1024u));
    freeKb_.store(static_cast<uint32_t>((total > used ? total - used : 0) / 1024u));
}

bool TestLog::clearAll() {
    if (!mounted_) return false;
    const uint32_t keep = loggingId_.load();
    uint32_t ids[kMaxTests];
    uint8_t n = 0;
    {
        Lock lock(lock_);
        for (uint8_t i = 0; i < indexCount_; ++i) {
            if (index_[i].id != keep) ids[n++] = index_[i].id;
        }
    }
    bool ok = true;
    for (uint8_t i = 0; i < n; ++i) {
        char path[24];
        pathOf(path, ids[i], "res");
        if (LittleFS.exists(path) && !LittleFS.remove(path)) ok = false;
        pathOf(path, ids[i], "sec");
        if (LittleFS.exists(path) && !LittleFS.remove(path)) ok = false;
        indexRemove(ids[i]);
    }
    interruptedId_ = 0;
    refreshUsage();
    return ok;
}

uint32_t TestLog::nextId() {
    uint32_t id = 1;
    Preferences prefs;
    if (prefs.begin(kNvsNamespace, false)) {
        id = prefs.getUInt(kNvsNext, 1);
        {
            Lock lock(lock_);
            if (indexCount_ > 0 && index_[indexCount_ - 1].id >= id) {
                id = index_[indexCount_ - 1].id + 1;
            }
        }
        if (id == 0 || id > 99999) id = 1;
        prefs.putUInt(kNvsNext, id + 1);
        prefs.end();
    }
    totalTests_.store(id);
    return id;
}

// =====================================================================
//  CycleSink
// =====================================================================
void TestLog::onStarted(const hbcycle::CycleResult& header) {
    if (!mounted_) return;
    activeId_ = header.id;
    loggingId_.store(header.id);
    buffered_ = 0;
    secondsWritten_ = 0;
    rotate();
    char path[24];
    pathOf(path, header.id, "sec");
    LittleFS.remove(path);  // id dipakai ulang setelah penghitung kembali ke 1
    writeResult(header);
    indexPut(header);
}

void TestLog::onSecond(const hbcycle::SecondLog& s) {
    if (!mounted_ || activeId_ == 0) return;
    SecondRecord& rec = buffer_[buffered_];
    if (s.valid && std::isfinite(s.rawMean)) {
        const double scaled = s.rawMean * 10.0;
        rec.raw10 = scaled > INT32_MAX ? INT32_MAX : (scaled < -INT32_MAX ? -INT32_MAX : static_cast<int32_t>(lround(scaled)));
    } else {
        rec.raw10 = INT32_MIN;
    }
    rec.temp100 = std::isfinite(s.tempC) ? static_cast<int16_t>(lroundf(s.tempC * 100.0f)) : INT16_MIN;
    const float duty = s.dutyPct < 0.0f ? 0.0f : (s.dutyPct > 100.0f ? 100.0f : s.dutyPct);
    rec.duty = static_cast<uint8_t>(lroundf(duty * 2.0f));
    rec.flags = static_cast<uint8_t>((static_cast<uint8_t>(s.phase) & 0x0F) | (s.valid ? 0 : 0x10) |
                                     (s.lidOpen ? 0x20 : 0));
    ++buffered_;
    if (buffered_ >= 60) {
        flush();  // sekali per menit (RANCANGAN §10: tulis flash jarang)
    }
}

void TestLog::onFinished(const hbcycle::CycleResult& result) {
    if (!mounted_) return;
    flush();
    hbcycle::CycleResult copy = result;
    copy.secondsLogged = secondsWritten_;
    copy.crc = resultCrc(copy);
    writeResult(copy);
    indexPut(copy);
    refreshUsage();
}

void TestLog::onLogClosed(uint32_t testId) {
    if (!mounted_ || testId != activeId_ || activeId_ == 0) return;
    flush();
    static hbcycle::CycleResult scratch;
    char path[24];
    pathOf(path, testId, "res");
    if (readResult(path, scratch) && scratch.status != hbcycle::Status::Running) {
        scratch.secondsLogged = secondsWritten_;  // termasuk log pendinginan
        scratch.crc = resultCrc(scratch);
        writeResult(scratch);
    }
    activeId_ = 0;
    loggingId_.store(0);
    refreshUsage();
}

void TestLog::flush() {
    if (buffered_ == 0 || activeId_ == 0) return;
    char path[24];
    pathOf(path, activeId_, "sec");
    File f = LittleFS.open(path, FILE_APPEND);
    if (f) {
        f.write(reinterpret_cast<const uint8_t*>(buffer_), buffered_ * sizeof(SecondRecord));
        f.close();
        secondsWritten_ += buffered_;
    }
    buffered_ = 0;
}

void TestLog::rotate() {
    for (;;) {
        uint32_t oldest = 0;
        uint8_t count = 0;
        {
            Lock lock(lock_);
            count = indexCount_;
            if (count > 0) oldest = index_[0].id;
        }
        const size_t freeBytes = LittleFS.totalBytes() - LittleFS.usedBytes();
        if (count == 0 || (count < kMaxTests && freeBytes >= kMinFreeBytes)) {
            return;
        }
        char path[24];
        pathOf(path, oldest, "res");
        LittleFS.remove(path);
        pathOf(path, oldest, "sec");
        LittleFS.remove(path);
        indexRemove(oldest);
    }
}

// =====================================================================
//  Indeks (urut naik menurut id)
// =====================================================================
void TestLog::indexPut(const hbcycle::CycleResult& r) {
    Lock lock(lock_);
    const hbcycle::HistoryEntry e = entryOf(r);
    for (uint8_t i = 0; i < indexCount_; ++i) {
        if (index_[i].id == r.id) {
            index_[i] = e;
            return;
        }
    }
    if (indexCount_ == kMaxTests) {
        memmove(&index_[0], &index_[1], (kMaxTests - 1) * sizeof(index_[0]));  // buang yang tertua
        --indexCount_;
    }
    uint8_t pos = indexCount_;
    while (pos > 0 && index_[pos - 1].id > r.id) {
        index_[pos] = index_[pos - 1];
        --pos;
    }
    index_[pos] = e;
    ++indexCount_;
}

void TestLog::indexRemove(uint32_t id) {
    Lock lock(lock_);
    for (uint8_t i = 0; i < indexCount_; ++i) {
        if (index_[i].id == id) {
            memmove(&index_[i], &index_[i + 1], (indexCount_ - i - 1) * sizeof(index_[0]));
            --indexCount_;
            return;
        }
    }
}

uint8_t TestLog::list(hbcycle::HistoryEntry* out, uint8_t max) {
    Lock lock(lock_);
    uint8_t n = 0;
    for (int i = static_cast<int>(indexCount_) - 1; i >= 0 && n < max; --i) {
        if (index_[i].status != hbcycle::Status::Running) {
            out[n++] = index_[i];
        }
    }
    return n;
}

uint8_t TestLog::count() {
    Lock lock(lock_);
    uint8_t n = 0;
    for (uint8_t i = 0; i < indexCount_; ++i) {
        if (index_[i].status != hbcycle::Status::Running) ++n;
    }
    return n;
}

bool TestLog::last(hbcycle::HistoryEntry& out) { return list(&out, 1) == 1; }

bool TestLog::load(uint32_t id, hbcycle::CycleResult& out) {
    char path[24];
    pathOf(path, id, "res");
    return readResult(path, out);
}

bool TestLog::readResult(const char* path, hbcycle::CycleResult& out) {
    if (!mounted_) return false;
    File f = LittleFS.open(path, FILE_READ);
    if (!f) return false;
    const bool ok = f.size() == sizeof(out) &&
                    f.read(reinterpret_cast<uint8_t*>(&out), sizeof(out)) == sizeof(out);
    f.close();
    return ok && resultValid(out);
}

bool TestLog::writeResult(const hbcycle::CycleResult& r) {
    char path[24];
    pathOf(path, r.id, "res");
    File f = LittleFS.open(path, FILE_WRITE);
    if (!f) return false;
    const bool ok = f.write(reinterpret_cast<const uint8_t*>(&r), sizeof(r)) == sizeof(r);
    f.close();
    return ok;
}

// =====================================================================
//  Serial: l (daftar), d <id> (ambil log)
//    LOGL,<id>,<epoch>,<status>,<kode>,<kadar air %>,<tervalidasi>
//    LOGH,<id>,<epoch>,<profil>,<preheatC>,<heatC>,<nol raw>,<cpg>,<W0 mg>,
//         <Wf mg>,<kadar air %>,<status>,<kode>,<durasi heat s>,<detik log>
//    LOG,<detik>,<raw x10 | ->,<suhu x100 | ->,<duty 0,5%>,<bendera>
//    LOGM,<menit>,<massa mg>,<suhu C>
//    LOGE,<jumlah LOG>
//  Status: 1 BERJALAN, 2 OK, 3 TIMEOUT, 4 ERROR.
// =====================================================================
void TestLog::printList(Print& out) {
    hbcycle::HistoryEntry entries[kMaxTests];
    const uint8_t n = list(entries, kMaxTests);
    out.printf("# %u tes tersimpan (maks %u). Ambil satu: d <id>\n", static_cast<unsigned>(n),
               static_cast<unsigned>(kMaxTests));
    for (uint8_t i = 0; i < n; ++i) {
        const hbcycle::HistoryEntry& e = entries[i];
        out.printf("LOGL,%" PRIu32 ",%" PRIu32 ",%d,%s,%.3f,%u\n", e.id, e.startEpoch, static_cast<int>(e.status),
                   hbcycle::reasonCode(e.reason), e.hasMoisture ? e.moisturePct : NAN,
                   static_cast<unsigned>(e.validated));
    }
}

bool TestLog::dump(uint32_t id, Print& out) {
    static hbcycle::CycleResult r;
    if (!load(id, r)) {
        return false;
    }
    out.printf("LOGH,%" PRIu32 ",%" PRIu32 ",%u,%u,%u,%.1f,%.3f,%.2f,%.2f,%.3f,%d,%s,%.0f,%" PRIu32 "\n", r.id,
               r.startEpoch, static_cast<unsigned>(r.profileIndex + 1), static_cast<unsigned>(r.preheatC),
               static_cast<unsigned>(r.heatC), r.zeroRaw, r.countsPerGram, r.w0Mg, r.wfMg, r.moisturePct,
               static_cast<int>(r.status), hbcycle::reasonCode(r.reason), r.heatSec, r.secondsLogged);
    char path[24];
    pathOf(path, id, "sec");
    uint32_t lines = 0;
    File f = LittleFS.open(path, FILE_READ);
    if (f) {
        SecondRecord rec;
        while (f.read(reinterpret_cast<uint8_t*>(&rec), sizeof(rec)) == sizeof(rec)) {
            char raw[16] = "-";
            char temp[12] = "-";
            if (rec.raw10 != INT32_MIN) snprintf(raw, sizeof(raw), "%" PRId32, rec.raw10);
            if (rec.temp100 != INT16_MIN) snprintf(temp, sizeof(temp), "%d", static_cast<int>(rec.temp100));
            out.printf("LOG,%" PRIu32 ",%s,%s,%u,%u\n", lines, raw, temp, static_cast<unsigned>(rec.duty),
                       static_cast<unsigned>(rec.flags));
            ++lines;
            if ((lines & 0x3F) == 0) {
                delay(1);               // beri waktu UART mengosongkan buffer
                esp_task_wdt_reset();   // dipanggil dari task sensor (ber-watchdog)
            }
        }
        f.close();
    }
    for (uint8_t k = 0; k < r.minuteCount && k < hbcycle::kMaxMinutes; ++k) {
        out.printf("LOGM,%u,%.2f,%.2f\n", static_cast<unsigned>(k), r.minutes[k].massMg, r.minutes[k].tempC);
    }
    out.printf("LOGE,%" PRIu32 "\n", lines);
    return true;
}
