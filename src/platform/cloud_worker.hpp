#pragma once
// Cloud-Aufträge im NetService-Thread (M27, Spec Ziel 5/6): Anmelden per Gerätecode, Abgleich vor dem Start
// (vorne in der Warteschlange), Konfliktauflösung und Uploads nach dem Spielen. Muster wie NetWorker: Ergebnisse
// unter einem Mutex, Logzeilen sammelt der Thread, der Haupt-Thread loggt sie. Tokens erscheinen nie im Log.
// Ohne NL_TLS lehnt jeder start* ab.

#include <optional>
#include <string>
#include <vector>

#include <ogc/mutex.h>

#include "core/cloud_plan.hpp"
#include "core/fs.hpp"
#include "core/oauth_device.hpp"
#include "core/token_store.hpp"
#include "core/save_locator.hpp"
#include "platform/net_service.hpp"

namespace nl::platform {

struct CloudSettings {
    std::string provider = "drive";  // M27 nur drive
    std::string dataDir;             // "<gerät>:/newlauncher"
    OAuthEndpoints endpoints;
    std::string apiBase = "https://www.googleapis.com";
    std::string device;  // Gerätename für Pakete
};

struct CloudUpload {
    std::string key;  // Spielschlüssel (pending.ini)
    SaveUnit unit;
    std::string title;
    u64 played = 0;  // session.ini started
};

enum class LinkState : u8 { Idle, Requesting, ShowCode, Linked, Denied, Expired, Failed, Offline };

class CloudWorker {
public:
    CloudWorker(NetService& service, FileSystem& fs);
    ~CloudWorker();
    CloudWorker(const CloudWorker&) = delete;
    CloudWorker& operator=(const CloudWorker&) = delete;

    static bool available();  // mit NL_TLS gebaut
    void configure(CloudSettings s);  // nur ohne laufenden Auftrag

    bool startLink();
    LinkState linkState() const { return static_cast<LinkState>(link_); }
    std::optional<DeviceCode> deviceCode() const;  // solange ShowCode

    bool startPreLaunch(SaveUnit unit, std::string title);
    bool startResolve(SaveUnit unit, std::string title, ConflictChoice choice, std::string seenRev);
    std::optional<PreLaunchResult> takePreLaunch();

    bool startUploads(std::vector<CloudUpload> items);
    std::vector<std::string> takeUploaded();  // bestätigte Schlüssel (aus pending.ini entfernen)

    std::vector<std::string> takeLogLines();
    bool running() const { return running_; }
    bool active() const { return jobId_ != 0 && service_.active(jobId_); }
    void cancel();
    void join();

    // Verknüpfung auf der SD (tokens.bin lesbar)? Liest die Datei; nur im Haupt-Thread ohne laufenden Auftrag.
    bool linked();
    // Abmelden ohne Widerruf (M28 widerruft): tokens.bin löschen, Zustand dieses Kontos leeren.
    void unlink();

private:
    bool submit(bool front, NetJobFn fn);
    void log(std::string line);
    std::string tokensPath() const;
    void runLink(const volatile bool& cancel);
    // Nimmt die Tokens der Sitzung (sonst tokens.bin), baut Transport + DriveProvider und ruft fn(ctx).
    // false = nicht angemeldet.
    template <typename Fn>
    bool withProvider(Fn&& fn);
    void storeTokens(const std::string& account, const TokenSet& t);

    NetService& service_;
    FileSystem& fs_;
    CloudSettings cfg_;
    u32 jobId_ = 0;
    mutex_t mutex_ = LWP_MUTEX_NULL;
    volatile bool running_ = false;
    volatile u8 link_ = static_cast<u8>(LinkState::Idle);
    std::optional<DeviceCode> code_;
    std::optional<PreLaunchResult> pre_;
    std::vector<std::string> uploaded_, logLines_;
    // Tokens dieser Sitzung samt Ablaufzeit (expiresAtMs wird nicht gespeichert): so braucht nicht jeder Auftrag
    // einen Refresh vorab. Nur im Netz-Thread oder ohne laufenden Auftrag angefasst.
    std::optional<StoredTokens> cached_;
};

}  // namespace nl::platform
