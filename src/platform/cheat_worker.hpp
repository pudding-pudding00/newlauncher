#pragma once
// Gecko-Codeliste eines Spiels laden (M15 Task 3) als Auftrag an den NetService, Aufbau wie EmuWorker. Nur auf
// Knopfdruck. Gespeichert wird erst, wenn die Antwort als Codeliste mit passender ID6 lesbar ist (.tmp + Umbenennen);
// eine vorhandene Datei in <data>/codes/ wird nie überschrieben.

#include <optional>
#include <string>
#include <vector>

#include <ogc/mutex.h>

#include "core/cheat_source.hpp"
#include "core/fs.hpp"
#include "platform/http_client.hpp"
#include "platform/net_service.hpp"

namespace nl::platform {

class CheatWorker {
public:
    explicit CheatWorker(NetService& service);
    ~CheatWorker();
    CheatWorker(const CheatWorker&) = delete;
    CheatWorker& operator=(const CheatWorker&) = delete;

    // url: fertige Adresse (cheatUrl), destTxt: <data>/codes/<ID6>.txt. false = läuft schon / nicht angenommen.
    bool start(FileSystem& fs, std::string url, std::string id6, std::string destTxt);
    bool running() const { return running_; }
    void join();  // abbrechen und auf das Ende warten; vor dem Aushängen

    std::optional<CheatFetchResult> takeResult();  // einmal je Auftrag
    std::vector<std::string> takeLogLines();

private:
    void run(const volatile bool& cancel);
    void finish(CheatFetchResult r);
    void log(std::string line);

    NetService& service_;
    u32 jobId_ = 0;
    mutex_t mutex_ = LWP_MUTEX_NULL;
    FileSystem* fs_ = nullptr;
    std::string url_, id6_, dest_;
    volatile bool running_ = false;
    std::optional<CheatFetchResult> result_;
    std::vector<std::string> logLines_;
};

}  // namespace nl::platform
