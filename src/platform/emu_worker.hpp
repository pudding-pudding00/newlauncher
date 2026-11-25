#pragma once
// Emulator-Verwaltung (Spec 6.9) als Auftrag an den NetService (M11): Liste laden oder Emulatoren der Reihe nach
// installieren. Ein Auftrag zugleich, Aufbau wie NetWorker: Fortschritt per volatile Zähler, Ergebnisse und
// Logzeilen Mutex-geschützt, geloggt vom Haupt-Thread (takeLogLines). Die Warteschlange des NetService
// serialisiert ihn mit dem Cover-Download.

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <ogc/mutex.h>

#include "core/emu_catalog.hpp"
#include "platform/http_client.hpp"
#include "core/emu_install.hpp"
#include "core/fs.hpp"
#include "core/self_update.hpp"
#include "platform/net_service.hpp"

namespace nl::platform {

inline constexpr u32 kMaxOscListBytes = 1024 * 1024;
inline constexpr u32 kMaxManifestBytes = 4 * 1024;

class EmuWorker {
public:
    explicit EmuWorker(NetService& service);
    ~EmuWorker();
    EmuWorker(const EmuWorker&) = delete;
    EmuWorker& operator=(const EmuWorker&) = delete;

    bool startFetchList(std::string repo);
    bool startInstall(FileSystem& fs, std::string repo, std::vector<OscApp> apps, std::string root = "sd:");
    // Selbst-Update (M20): Manifest laden, bei neuerer Version apps/newlauncher/ ersetzen (immer sd:).
    bool startSelfUpdate(FileSystem& fs, std::string manifestUrl, std::string currentVersion);
    std::optional<SelfUpdateResult> takeSelfUpdate();
    bool running() const { return running_; }
    void cancel();
    void join();  // eigenen Auftrag abbrechen und auf sein Ende warten; vor dem Aushängen

    std::optional<std::vector<OscApp>> takeList();  // nach FetchList; leer = noch nichts oder fehlgeschlagen
    bool listFailed() const { return listFailed_; }
    NetError listError() const { return static_cast<NetError>(listError_); }  // Ursache bei listFailed
    std::vector<std::pair<std::string, EmuInstallResult>> takeResults();
    std::vector<std::string> takeLogLines();

    EmuStep step() const { return static_cast<EmuStep>(step_); }
    u32 done() const { return done_; }
    u32 total() const { return total_; }
    u32 appIndex() const { return appIndex_; }  // laufende Installation (0-basiert) von appCount()
    u32 appCount() const { return appCount_; }
    std::string currentName();

private:
    enum class Job : u8 { List, Install, SelfUpdate };
    bool launch(Job job);
    bool network();
    void runList(const volatile bool& cancel);
    void runInstall(const volatile bool& cancel);
    void runSelfUpdateJob(const volatile bool& cancel);
    EmuInstallIo installIo(const volatile bool& cancel, bool net);
    void log(std::string line);
    HttpLog logFn();  // log() als HttpLog für httpGet/httpDownload

    NetService& service_;
    u32 jobId_ = 0;
    mutex_t mutex_ = LWP_MUTEX_NULL;
    Job job_ = Job::List;
    FileSystem* fs_ = nullptr;
    std::string repo_;
    std::string root_ = "sd:";  // Zielgerät der Installation
    std::vector<OscApp> apps_;
    volatile u8 step_ = 0;
    volatile u32 done_ = 0, total_ = 0, appIndex_ = 0, appCount_ = 0;
    volatile bool running_ = false, listFailed_ = false;
    volatile u8 listError_ = static_cast<u8>(NetError::Other);
    std::optional<std::vector<OscApp>> list_;
    std::vector<std::pair<std::string, EmuInstallResult>> results_;
    std::vector<std::string> logLines_;
    std::string current_;
    std::string manifestUrl_, currentVersion_;
    std::optional<SelfUpdateResult> selfUpdate_;
};

}  // namespace nl::platform
