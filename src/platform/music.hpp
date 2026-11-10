#pragma once
// Hintergrundmusik (Spec 3.7): eigener Thread dekodiert Ogg Vorbis (Tremor) von der SD und streamt
// über ASND-Stimme 0 (die UI-Klänge nutzen 1..3). log::line ist nicht threadsicher: der Thread sammelt
// seine Marker, der Haupt-Thread loggt sie (takeLogLines), wie beim Netz-Thread.

#include <array>
#include <string>
#include <vector>

#include <ogc/lwp.h>
#include <ogc/mutex.h>

#include "core/types.hpp"

namespace nl::platform {

class Music {
public:
    Music();
    ~Music();
    Music(const Music&) = delete;
    Music& operator=(const Music&) = delete;

    // false bei leerer Liste (dann kein Thread), laufendem Thread oder fehlgeschlagenem Start.
    bool start(std::vector<std::string> files, u32 seed);
    bool started() const { return thread_ != LWP_THREAD_NULL; }
    void setEnabled(bool on) { enabled_ = on; }
    void setVolume(int percent);
    void stop();  // Thread beenden und joinen, Stimme 0 stoppen; vor audio.shutdown()
    std::vector<std::string> takeLogLines();

private:
    static constexpr int kBuffers = 4;
    static constexpr u32 kBufferBytes = 8192;

    static void* entry(void* self);
    void run();
    bool playFile(const std::string& path);  // false bei Fehler (Öffnen, Format, Dekodieren)
    void log(std::string line);
    bool idleWhileDisabled();  // true, wenn beendet werden soll

    lwp_t thread_ = LWP_THREAD_NULL;
    mutex_t mutex_ = LWP_MUTEX_NULL;
    std::vector<std::string> files_, logLines_;
    u32 seed_ = 1;
    std::string lastLogged_;  // nur im Musik-Thread
    std::array<u8*, kBuffers> buffers_{};
    volatile bool quit_ = false;
    volatile bool enabled_ = true;
    volatile int volume_ = 153;  // ASND 0..255
};

}  // namespace nl::platform
