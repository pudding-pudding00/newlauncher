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
#include "core/cloud_sync_all.hpp"
#include "core/fs.hpp"
#include "core/oauth_device.hpp"
#include "core/token_store.hpp"
#include "core/save_locator.hpp"
#include "platform/net_service.hpp"

namespace nl::platform {

struct CloudSettings {
    std::string provider = "drive";  // drive | onedrive | dropbox
    std::string dataDir;             // "<gerät>:/newlauncher"
    OAuthEndpoints endpoints;        // des aktiven Anbieters
    std::string apiBase = "https://www.googleapis.com";
    std::string driveRevokeUrl;      // Widerruf alter Drive-Tokens nach dem Wechsel zu einem anderen Anbieter
    // Dropbox (M29): Inhalts-Server, Freigabeseite (leer = echte) und die Daten für den Widerruf alter Dropbox-Tokens
    // nach einem Wechsel (Bearer über DropboxProvider::revoke, darum Endpunkte samt App-Key).
    std::string contentBase = "https://content.dropboxapi.com";
    std::string authorizeBase;
    OAuthEndpoints dropboxEndpoints;
    std::string dropboxApiBase = "https://api.dropboxapi.com";
    std::string pairToken;  // fester Kopplungs-Token (Debug cloud_pair_token), sonst aus secureRandom
    std::string device;  // Gerätename für Pakete
};

struct CloudUpload {
    std::string key;  // Spielschlüssel (pending.ini)
    SaveUnit unit;
    std::string title;
    u64 played = 0;  // session.ini started
};

// BadCode (M29): Dropbox hat den eingegebenen Code abgelehnt; der Auftrag wartet weiter auf einen neuen.
enum class LinkState : u8 { Idle, Requesting, ShowCode, BadCode, Linked, Denied, Expired, Failed, Offline };

// Dropbox-Anmeldung (M29): Freigabeseite mit PKCE-Challenge und der Token der Kopplungsseite.
struct AuthLink {
    std::string authorizeUrl, pairToken;
    u32 expiresInS = 0;
};
constexpr u32 kAuthCodeWaitS = 10 * 60;

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
    std::optional<DeviceCode> deviceCode() const;  // solange ShowCode (Gerätecode-Anbieter)
    std::optional<AuthLink> authLink() const;      // solange ShowCode/BadCode (Dropbox)
    void submitCode(std::string code);             // Code von der Kopplungsseite oder Tastatur

    bool startPreLaunch(SaveUnit unit, std::string title);
    bool startResolve(SaveUnit unit, std::string title, ConflictChoice choice, std::string seenRev);
    std::optional<PreLaunchResult> takePreLaunch();

    bool startUploads(std::vector<CloudUpload> items);
    std::vector<std::string> takeUploaded();  // bestätigte Schlüssel (aus pending.ini entfernen)

    // "Alle abgleichen" (M28): Fortschritt fertig/gesamt, Abbruch über cancel().
    bool startSyncAll(std::vector<SyncTarget> targets, ConflictPolicy policy);
    std::optional<SyncAllResult> takeSyncAll();
    std::pair<u32, u32> syncProgress() const { return {syncDone_, syncTotal_}; }

    // Abmelden (M28): Refresh-Token widerrufen (best effort, im Netz-Thread), danach immer tokens.bin löschen und
    // den Zustand dieses Kontos leeren.
    bool startUnlink();
    std::optional<RevokeResult> takeUnlinked();

    std::vector<std::string> takeLogLines();
    bool running() const { return running_; }
    bool transferring() const { return transferring_; }  // laufender Auftrag ist bei Download/Upload
    bool active() const { return jobId_ != 0 && service_.active(jobId_); }
    void cancel();
    void join();

    // Verknüpfung auf der SD (tokens.bin lesbar)? Liest die Datei; nur im Haupt-Thread ohne laufenden Auftrag.
    bool linked();

private:
    bool submit(bool front, NetJobFn fn);
    void log(std::string line);
    std::string tokensPath() const;
    void runLink(const volatile bool& cancel);
    void runPkceLink(const volatile bool& cancel);
    // Nach erhaltenen Tokens: Konto lesen, alten Anbieter widerrufen, speichern; setzt den Endzustand.
    void completeLink(HttpTransport& http, TokenSet& tokens, const volatile bool& cancel);
    void finishLink(LinkState s);
    // Nimmt die Tokens der Sitzung (sonst tokens.bin), baut Transport + DriveProvider und ruft fn(ctx).
    // false = nicht angemeldet.
    template <typename Fn>
    bool withProvider(Fn&& fn);
    void storeTokens(const std::string& account, const TokenSet& t);
    std::string revokeUrlFor(const std::string& provider) const;
    RevokeResult revoke(HttpTransport& http, const StoredTokens& t, const volatile bool& cancel);
    void forget(const std::string& account);  // Zustand dieses Kontos in state.ini leeren

    NetService& service_;
    FileSystem& fs_;
    CloudSettings cfg_;
    u32 jobId_ = 0;
    mutex_t mutex_ = LWP_MUTEX_NULL;
    volatile bool running_ = false;
    volatile bool transferring_ = false;
    volatile u8 link_ = static_cast<u8>(LinkState::Idle);
    std::optional<DeviceCode> code_;
    std::optional<AuthLink> auth_;
    std::optional<std::string> submitted_;
    std::optional<PreLaunchResult> pre_;
    std::optional<SyncAllResult> syncAll_;
    std::optional<RevokeResult> unlinked_;
    volatile u32 syncDone_ = 0, syncTotal_ = 0;
    std::vector<std::string> uploaded_, logLines_;
    // Tokens dieser Sitzung samt Ablaufzeit (expiresAtMs wird nicht gespeichert): so braucht nicht jeder Auftrag
    // einen Refresh vorab. Nur im Netz-Thread oder ohne laufenden Auftrag angefasst.
    std::optional<StoredTokens> cached_;
};

}  // namespace nl::platform
