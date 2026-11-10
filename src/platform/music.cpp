#include "platform/music.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <malloc.h>
#include <unistd.h>

#include <asndlib.h>
#include <gccore.h>
#include <tremor/ivorbisfile.h>

#include "core/playlist.hpp"

namespace nl::platform {

namespace {
constexpr u32 kStackBytes = 64 * 1024;
constexpr u8 kPriority = 70;  // über dem Haupt-Thread: ein Aussetzer wäre hörbar, Dekodieren ist kurz
constexpr s32 kVoice = 0;

size_t readCb(void* ptr, size_t size, size_t n, void* f) { return std::fread(ptr, size, n, static_cast<FILE*>(f)); }
int seekCb(void* f, ogg_int64_t off, int whence) { return std::fseek(static_cast<FILE*>(f), static_cast<long>(off), whence); }
int closeCb(void* f) { return std::fclose(static_cast<FILE*>(f)); }
long tellCb(void* f) { return std::ftell(static_cast<FILE*>(f)); }
}  // namespace

Music::Music() { LWP_MutexInit(&mutex_, false); }

Music::~Music() {
    stop();
    LWP_MutexDestroy(mutex_);
}

bool Music::start(std::vector<std::string> files, u32 seed) {
    if (files.empty() || started()) return false;
    for (u8*& b : buffers_) {
        b = static_cast<u8*>(memalign(32, kBufferBytes));
        if (b == nullptr) {
            stop();
            log("NL_MUSIC_FAIL kein Speicher");
            return false;
        }
    }
    files_ = std::move(files);
    seed_ = seed;
    quit_ = false;
    if (LWP_CreateThread(&thread_, &Music::entry, this, nullptr, kStackBytes, kPriority) != 0) {
        thread_ = LWP_THREAD_NULL;
        stop();
        return false;
    }
    return true;
}

void Music::setVolume(int percent) { volume_ = asndVolume(percent); }

void Music::stop() {
    quit_ = true;
    if (thread_ != LWP_THREAD_NULL) {
        LWP_JoinThread(thread_, nullptr);
        thread_ = LWP_THREAD_NULL;
        ASND_StopVoice(kVoice);  // erst danach die Puffer freigeben: der DSP liest sie per DMA
    }
    for (u8*& b : buffers_) {
        std::free(b);
        b = nullptr;
    }
}

std::vector<std::string> Music::takeLogLines() {
    LWP_MutexLock(mutex_);
    std::vector<std::string> out = std::move(logLines_);
    logLines_.clear();
    LWP_MutexUnlock(mutex_);
    return out;
}

void Music::log(std::string line) {
    LWP_MutexLock(mutex_);
    logLines_.push_back(std::move(line));
    LWP_MutexUnlock(mutex_);
}

void* Music::entry(void* self) {
    static_cast<Music*>(self)->run();
    return nullptr;
}

bool Music::idleWhileDisabled() {
    if (enabled_) return quit_;
    ASND_StopVoice(kVoice);
    while (!enabled_ && !quit_) usleep(50 * 1000);
    return quit_;
}

void Music::run() {
    Playlist playlist(files_, seed_);
    while (!quit_) {
        if (idleWhileDisabled()) break;
        const std::string path = playlist.next();
        if (playFile(path)) continue;
        if (quit_) break;
        playlist.markBroken();  // nicht erneut versuchen: kein Fehler-Log je Runde
        log("NL_MUSIC_FAIL " + path);
        if (playlist.allBroken()) {
            log("NL_MUSIC_OFF");
            break;
        }
    }
    ASND_StopVoice(kVoice);
}

bool Music::playFile(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) return false;
    OggVorbis_File vf;
    const ov_callbacks cb{readCb, seekCb, closeCb, tellCb};
    if (ov_open_callbacks(f, &vf, nullptr, 0, cb) != 0) {
        std::fclose(f);  // bei Fehlschlag gehört die Datei noch uns
        return false;
    }
    const vorbis_info* info = ov_info(&vf, -1);
    if (info == nullptr || info->channels < 1 || info->channels > 2) {
        ov_clear(&vf);
        return false;
    }
    const s32 format = info->channels == 2 ? VOICE_STEREO_16BIT : VOICE_MONO_16BIT;
    const s32 rate = static_cast<s32>(info->rate);
    bool ok = true;
    bool first = true;
    int bitstream = 0;
    int applied = -1;
    for (int cur = 0; !quit_ && enabled_; cur = (cur + 1) % kBuffers) {
        // Füllen: Tremor liefert 16 Bit in Wirts-Byteordnung (Big-Endian), passend zu VOICE_*_16BIT.
        u8* buf = buffers_[static_cast<std::size_t>(cur)];
        u32 filled = 0;
        while (filled < kBufferBytes) {
            const long n = ov_read(&vf, reinterpret_cast<char*>(buf + filled), static_cast<int>(kBufferBytes - filled), &bitstream);
            if (n == OV_HOLE) continue;
            if (n < 0) {
                ok = false;
                break;
            }
            if (n == 0) break;  // Stückende
            filled += static_cast<u32>(n);
        }
        if (!ok || filled == 0) break;
        if (first) {
            // Nur bei Stückwechsel melden: ein einzelnes Stück wiederholt sich sonst alle paar Sekunden im Log,
            // und SYS_Report allokiert auf dem Haupt-Thread (Leerlauf-Zähler im Selbsttest).
            if (path != lastLogged_) {
                lastLogged_ = path;
                char line[200];
                std::snprintf(line, sizeof line, "NL_MUSIC play %s rate=%d ch=%d", path.c_str(),
                              static_cast<int>(rate), info->channels);
                log(line);
            }
        }
        const u32 bytes = (filled + 31) & ~31u;  // ASND will Vielfache von 32 Byte
        std::memset(buf + filled, 0, bytes - filled);
        DCFlushRange(buf, bytes);

        const int vol = volume_;
        if (first) {
            ASND_SetVoice(kVoice, format, rate, 0, buf, static_cast<s32>(bytes), vol, vol, nullptr);
            applied = vol;
            first = false;
        } else {
            // 4 Puffer: einer spielt, einer wartet, die übrigen werden gefüllt – nie einer, den der DSP noch liest.
            while (!quit_ && enabled_ && ASND_StatusVoice(kVoice) == SND_WORKING && !ASND_TestVoiceBufferReady(kVoice))
                usleep(2000);
            if (quit_ || !enabled_) break;
            if (ASND_StatusVoice(kVoice) != SND_WORKING) {  // Unterlauf: neu ansetzen
                ASND_SetVoice(kVoice, format, rate, 0, buf, static_cast<s32>(bytes), vol, vol, nullptr);
            } else {
                ASND_AddVoice(kVoice, buf, static_cast<s32>(bytes));
            }
        }
        if (vol != applied) {
            ASND_ChangeVolumeVoice(kVoice, vol, vol);
            applied = vol;
        }
        if (filled < kBufferBytes) break;  // letzter, kurzer Puffer
    }
    // Ausklingen lassen, damit das nächste Stück den Rest nicht abschneidet.
    while (ok && !quit_ && enabled_ && ASND_StatusVoice(kVoice) == SND_WORKING) {
        if (volume_ != applied) {
            applied = volume_;
            ASND_ChangeVolumeVoice(kVoice, applied, applied);
        }
        usleep(10 * 1000);
    }
    ov_clear(&vf);  // schließt die Datei über closeCb
    // Kopf ohne Audiodaten (z. B. abgebrochene Kopie) ist ein Fehler: sonst kreiste der Thread mit
    // Priorität 70 ohne Pause und hungerte den Haupt-Thread aus (Review M6/I1).
    return ok && !first;
}

}  // namespace nl::platform
