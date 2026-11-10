#include "platform/cover_worker.hpp"

#include <cstring>

#include <ogc/cache.h>

#include "core/cmpr.hpp"
#include "core/cover_image.hpp"
#include "core/cover_paths.hpp"
#include "core/tex_file.hpp"
#include "platform/png_decode.hpp"

namespace nl::platform {

namespace {
constexpr u32 kStackBytes = 64 * 1024;
constexpr u8 kPriority = 40;  // wie der Scan: unter dem Haupt-Thread
constexpr std::size_t kMaxPngBytes = 4u * 1024 * 1024;

std::string parentDir(const std::string& path) {
    const std::size_t slash = path.rfind('/');
    return slash == std::string::npos ? std::string() : path.substr(0, slash);
}
}  // namespace

CoverWorker::CoverWorker() { LWP_MutexInit(&mutex_, false); }

CoverWorker::~CoverWorker() {
    join();
    LWP_MutexDestroy(mutex_);
}

bool CoverWorker::start(FileSystem& fs, std::string libRoot, std::string stem, CoverMemory& mem) {
    if (running_) return false;
    join();
    fs_ = &fs;
    mem_ = &mem;
    root_ = std::move(libRoot);
    stem_ = std::move(stem);
    running_ = true;
    if (LWP_CreateThread(&thread_, &CoverWorker::entry, this, nullptr, kStackBytes, kPriority) != 0) {
        running_ = false;
        thread_ = LWP_THREAD_NULL;
        return false;
    }
    return true;
}

void CoverWorker::join() {
    if (thread_ != LWP_THREAD_NULL) {
        LWP_JoinThread(thread_, nullptr);
        thread_ = LWP_THREAD_NULL;
    }
}

void* CoverWorker::entry(void* arg) {
    auto* self = static_cast<CoverWorker*>(arg);
    CoverResult r = self->run();
    LWP_MutexLock(self->mutex_);
    self->result_.emplace(std::move(r));
    self->running_ = false;
    LWP_MutexUnlock(self->mutex_);
    return nullptr;
}

CoverResult CoverWorker::run() {
    CoverResult out;
    out.stem = stem_;
    FileSystem& fs = *fs_;
    auto deliver = [&](std::span<const u8> cmpr) {
        void* p = mem_->alloc(kCoverTexBytes);
        if (p == nullptr) {
            out.error = "kein Speicher";
            return;
        }
        std::memcpy(p, cmpr.data(), kCoverTexBytes);
        DCFlushRange(p, kCoverTexBytes);  // GX liest am Cache vorbei
        out.texels = p;
        out.bytes = kCoverTexBytes;
    };

    // 1. Fertige .tex aus dem Cache
    const std::string texPath = coverTexPath(root_, stem_);
    if (auto file = fs.readAll(texPath, kTexHeaderBytes + kCoverTexBytes); file.isOk()) {
        auto tex = parseTex(file.value());
        if (tex.isOk() && tex.value().width == kCoverW && tex.value().height == kCoverH) {
            deliver(tex.value().data);
            return out;
        }
    }

    // 2. PNG-Quelle umwandeln
    auto png = fs.readAll(coverSrcPath(root_, stem_), kMaxPngBytes);
    if (!png.isOk()) return out;  // keine Quelle: Platzhalter
    u32 w = 0, h = 0;
    auto rgba = decodePng(png.value(), w, h);
    if (!rgba.isOk()) {
        out.error = rgba.error().context;
        return out;
    }
    const std::vector<u8> fitted = fitCover(rgba.value(), w, h);
    if (fitted.empty()) {
        out.error = "Einpassen fehlgeschlagen";
        return out;
    }
    const std::vector<u8> cmpr = encodeCmpr(fitted, kCoverW, kCoverH);
    out.converted = true;

    // 3. .tex anlegen; scheitert das (z. B. schreibgeschützt), wird beim nächsten Start neu umgewandelt.
    const std::vector<u8> file = writeTex(kCoverW, kCoverH, cmpr);
    if (fs.makeDirs(parentDir(texPath)).isOk()) static_cast<void>(fs.writeAtomic(texPath, file));

    // 4. Texel für GX
    deliver(cmpr);
    return out;
}

std::optional<CoverResult> CoverWorker::take() {
    if (running_) return std::nullopt;
    LWP_MutexLock(mutex_);
    std::optional<CoverResult> out = std::move(result_);
    result_.reset();
    LWP_MutexUnlock(mutex_);
    if (out) join();
    return out;
}

}  // namespace nl::platform
