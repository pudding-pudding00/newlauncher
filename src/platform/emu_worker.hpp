#pragma once
// Netz-Thread der Emulator-Verwaltung (Spec 6.9): Liste laden oder Emulatoren der Reihe nach installieren.
// Ein Auftrag zugleich, Aufbau wie NetWorker: Fortschritt per volatile Zähler, Ergebnisse und Logzeilen
// Mutex-geschützt, geloggt vom Haupt-Thread (takeLogLines). Nicht gleichzeitig mit NetWorker starten.

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <ogc/lwp.h>
#include <ogc/mutex.h>

#include "core/emu_catalog.hpp"
#include "core/emu_install.hpp"
#include "core/fs.hpp"

namespace nl::platform {

inline constexpr u32 kMaxOscListBytes = 1024 * 1024;

class EmuWorker {
public:
    EmuWorker();
    ~EmuWorker();
    EmuWorker(const EmuWorker&) = delete;
    EmuWorker& operator=(const EmuWorker&) = delete;

    bool startFetchList(std::string repo);
    bool startInstall(FileSystem& fs, std::string repo, std::vector<OscApp> apps);
    bool running() const { return running_; }
    void cancel() { cancel_ = true; }
    void join();  // bricht ab und wartet; vor dem Aushängen

    std::optional<std::vector<OscApp>> takeList();  // nach FetchList; leer = noch nichts oder fehlgeschlagen
    bool listFailed() const { return listFailed_; }
    std::vector<std::pair<std::string, EmuInstallResult>> takeResults();
    std::vector<std::string> takeLogLines();

    EmuStep step() const { return static_cast<EmuStep>(step_); }
    u32 done() const { return done_; }
    u32 total() const { return total_; }
    u32 appIndex() const { return appIndex_; }  // laufende Installation (0-basiert) von appCount()
    u32 appCount() const { return appCount_; }
    std::string currentName();

private:
    enum class Job : u8 { List, Install };
    static void* entry(void* self);
    bool launch(Job job);
    bool network();
    void runList();
    void runInstall();
    void log(std::string line);

    lwp_t thread_ = LWP_THREAD_NULL;
    mutex_t mutex_ = LWP_MUTEX_NULL;
    Job job_ = Job::List;
    FileSystem* fs_ = nullptr;
    std::string repo_;
    std::vector<OscApp> apps_;
    volatile u8 step_ = 0;
    volatile u32 done_ = 0, total_ = 0, appIndex_ = 0, appCount_ = 0;
    volatile bool running_ = false, cancel_ = false, listFailed_ = false;
    std::optional<std::vector<OscApp>> list_;
    std::vector<std::pair<std::string, EmuInstallResult>> results_;
    std::vector<std::string> logLines_;
    std::string current_;
};

}  // namespace nl::platform
