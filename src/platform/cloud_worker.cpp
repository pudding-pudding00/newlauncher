#include "platform/cloud_worker.hpp"

#include <array>
#include <ctime>
#include <memory>
#include <unistd.h>
#include <utility>

#include "core/cloud_state.hpp"
#include "core/drive_provider.hpp"
#include "core/onedrive_provider.hpp"
#include "core/token_store.hpp"
#include "platform/device_key.hpp"
#include "platform/http_client.hpp"

namespace nl::platform {

namespace {

constexpr std::size_t kMaxTokenFile = 64 * 1024;

u64 nowUnix() { return static_cast<u64>(std::time(nullptr)); }

// "cloud <einheit> <was>" aus core/cloud_plan → Marker "NL_CLOUD <einheit> <was>".
std::string marker(const std::string& line) {
    constexpr std::string_view kPrefix = "cloud ";
    if (line.compare(0, kPrefix.size(), kPrefix) == 0) return "NL_CLOUD " + line.substr(kPrefix.size());
    return line;
}

std::optional<StoredTokens> loadTokens(FileSystem& fs, const std::string& path) {
    auto blob = fs.readAll(path, kMaxTokenFile);
    if (!blob.isOk()) return std::nullopt;
    const std::vector<u8> key = deviceKeyMaterial();
    return unsealTokens(blob.value(), key);
}

const char* revokeName(RevokeResult r) {
    switch (r) {
        case RevokeResult::Ok: return "ok";
        case RevokeResult::Failed: return "fail";
        case RevokeResult::Unsupported: break;
    }
    return "none";
}

// Wartet ms in 100-ms-Schritten; false = abgebrochen.
bool sleepMs(u32 ms, const volatile bool& cancel) {
    for (u32 waited = 0; waited < ms; waited += 100) {
        if (cancel) return false;
        usleep(100 * 1000);
    }
    return !cancel;
}

}  // namespace

// Ohne RTTI und ohne std::variant: der Anbieter kommt als Zeiger auf die Basisklasse.
std::unique_ptr<CloudProvider> makeProvider(const CloudSettings& cfg, HttpTransport& http, TokenSet& tokens) {
    if (cfg.provider == "onedrive") {
        return std::make_unique<OneDriveProvider>(http, cfg.endpoints, tokens, NetService::clockMs, cfg.apiBase);
    }
    return std::make_unique<DriveProvider>(http, cfg.endpoints, tokens, NetService::clockMs, cfg.apiBase);
}

CloudWorker::CloudWorker(NetService& service, FileSystem& fs) : service_(service), fs_(fs) {
    LWP_MutexInit(&mutex_, false);
}

CloudWorker::~CloudWorker() {
    join();
    LWP_MutexDestroy(mutex_);
}

bool CloudWorker::available() {
#ifdef NL_TLS
    return true;
#else
    return false;
#endif
}

void CloudWorker::configure(CloudSettings s) {
    if (running_) return;
    cfg_ = std::move(s);
    cached_.reset();  // anderer Anbieter oder Ordner
}

std::string CloudWorker::tokensPath() const { return cfg_.dataDir + "/cloud/tokens.bin"; }

bool CloudWorker::submit(bool front, NetJobFn fn) {
#ifdef NL_TLS
    if (running_ || cfg_.dataDir.empty()) return false;
    join();
    running_ = true;
    transferring_ = false;
    NetJobFn job = [this, fn = std::move(fn)](const volatile bool& cancel) {
        fn(cancel);
        LWP_MutexLock(mutex_);
        running_ = false;
        transferring_ = false;
        LWP_MutexUnlock(mutex_);
    };
    jobId_ = front ? service_.submitFront(std::move(job)) : service_.submit(std::move(job));
    if (jobId_ == 0) {
        running_ = false;
        return false;
    }
    return true;
#else
    (void)front;
    (void)fn;
    return false;
#endif
}

void CloudWorker::log(std::string line) {
    LWP_MutexLock(mutex_);
    logLines_.push_back(std::move(line));
    LWP_MutexUnlock(mutex_);
}

std::vector<std::string> CloudWorker::takeLogLines() {
    LWP_MutexLock(mutex_);
    std::vector<std::string> out = std::move(logLines_);
    logLines_.clear();
    LWP_MutexUnlock(mutex_);
    return out;
}

void CloudWorker::cancel() {
    if (jobId_ != 0) service_.cancel(jobId_);
}

void CloudWorker::join() {
    if (jobId_ == 0) return;
    service_.cancelAndWait(jobId_);
    jobId_ = 0;
    running_ = false;
}

std::optional<DeviceCode> CloudWorker::deviceCode() const {
    LWP_MutexLock(mutex_);
    std::optional<DeviceCode> c = code_;
    LWP_MutexUnlock(mutex_);
    return c;
}

void CloudWorker::storeTokens(const std::string& account, const TokenSet& t) {
    StoredTokens st;
    st.provider = cfg_.provider;
    st.account = account;
    st.tokens = t;
    cached_ = st;
    std::array<u8, 16> salt{}, iv{};
    saltRandom(salt);
    saltRandom(iv);
    const std::vector<u8> key = deviceKeyMaterial();
    const std::vector<u8> blob = sealTokens(st, key, salt, iv);
    if (!fs_.makeDirs(cfg_.dataDir + "/cloud").isOk() || !fs_.writeAtomic(tokensPath(), blob).isOk()) {
        log("NL_CLOUD_TOKENS write_fail");
    }
}

// ---- Anmelden ---------------------------------------------------------------------------------------------

bool CloudWorker::startLink() {
    if (running_) return false;
    link_ = static_cast<u8>(LinkState::Requesting);
    if (!submit(false, [this](const volatile bool& cancel) { runLink(cancel); })) {
        link_ = static_cast<u8>(LinkState::Failed);
        return false;
    }
    return true;
}

void CloudWorker::runLink(const volatile bool& cancel) {
    auto finish = [this](LinkState s) {
        LWP_MutexLock(mutex_);
        code_.reset();
        LWP_MutexUnlock(mutex_);
        link_ = static_cast<u8>(s);
    };
    if (!service_.networkUp()) {
        log("NL_CLOUD_LINK offline");
        finish(LinkState::Offline);
        return;
    }
    WtlsTransport http;  // ohne URL-Log: Gerätecode und Tokens stehen in Formularen und Antworten
    auto dc = requestDeviceCode(http, cfg_.endpoints, cancel);
    if (!dc.isOk()) {
        log("NL_CLOUD_LINK failed");
        finish(cancel ? LinkState::Idle : LinkState::Failed);
        return;
    }
    const DeviceCode code = dc.value();
    LWP_MutexLock(mutex_);
    code_ = code;
    LWP_MutexUnlock(mutex_);
    link_ = static_cast<u8>(LinkState::ShowCode);
    log("NL_CLOUD_LINK code");

    const u64 deadline = NetService::clockMs() + static_cast<u64>(code.expiresInS) * 1000;
    u32 intervalS = code.intervalS == 0 ? 5 : code.intervalS;
    TokenSet tokens;
    for (;;) {
        if (!sleepMs(intervalS * 1000, cancel)) {
            finish(LinkState::Idle);
            return;
        }
        if (NetService::clockMs() >= deadline) {
            log("NL_CLOUD_LINK expired");
            finish(LinkState::Expired);
            return;
        }
        const PollState p = pollToken(http, cfg_.endpoints, code.deviceCode, NetService::clockMs(), tokens, cancel);
        if (p == PollState::Pending) continue;
        if (p == PollState::SlowDown) {
            intervalS += 5;  // RFC 8628 3.5
            continue;
        }
        if (p == PollState::Denied || p == PollState::Expired) {
            log(p == PollState::Denied ? "NL_CLOUD_LINK denied" : "NL_CLOUD_LINK expired");
            finish(p == PollState::Denied ? LinkState::Denied : LinkState::Expired);
            return;
        }
        if (p == PollState::Failed) {
            if (cancel) {
                finish(LinkState::Idle);
                return;
            }
            continue;  // einzelner Netzfehler: bis zum Ablauf weiter fragen
        }
        break;  // Ok
    }

    const std::unique_ptr<CloudProvider> provider = makeProvider(cfg_, http, tokens);
    std::string id;
    if (provider->account(id, cancel) != CloudStatus::Ok || id.empty()) {
        log("NL_CLOUD_LINK failed");
        finish(LinkState::Failed);
        return;
    }
    // Wechsel des Anbieters: das alte Token best effort widerrufen, bevor tokens.bin überschrieben wird.
    if (const auto old = loadTokens(fs_, tokensPath()); old && old->provider != cfg_.provider) {
        log(std::string("NL_CLOUD_UNLINKED revoke=") + revokeName(revoke(http, *old, cancel)));
    }
    storeTokens(cfg_.provider + ":" + id, tokens);
    log("NL_CLOUD_LINKED " + cfg_.provider);
    finish(LinkState::Linked);
}

// ---- Aufträge mit angemeldetem Anbieter ---------------------------------------------------------------------

template <typename Fn>
bool CloudWorker::withProvider(Fn&& fn) {
    if (!cached_ || cached_->provider != cfg_.provider) cached_ = loadTokens(fs_, tokensPath());
    if (cached_ && cached_->provider != cfg_.provider) cached_.reset();  // Tokens des vorigen Anbieters
    if (!cached_) return false;
    StoredTokens* stored = &*cached_;  // der Anbieter schreibt erneuerte Tokens hier hinein
    WtlsTransport http;
    const std::unique_ptr<CloudProvider> provider = makeProvider(cfg_, http, stored->tokens);
    CloudCtx ctx{fs_, cfg_.dataDir, *provider, stored->account, cfg_.device, nowUnix(),
                 [this](std::string line) { log(marker(line)); }, [this] { transferring_ = true; }};
    fn(ctx);
    if (provider->refreshed()) log("NL_CLOUD_REFRESH ok");
    if (provider->tokensChanged()) storeTokens(stored->account, stored->tokens);
    return true;
}

namespace {
PreLaunchResult skippedResult(std::string reason) {
    PreLaunchResult r;
    r.kind = PreLaunch::Skipped;
    r.reason = std::move(reason);
    return r;
}
}  // namespace

bool CloudWorker::startPreLaunch(SaveUnit unit, std::string title) {
    return submit(true, [this, unit = std::move(unit), title = std::move(title)](const volatile bool& cancel) {
        PreLaunchResult r;
        if (!service_.networkUp()) {
            log("NL_CLOUD " + unit.id + " skip offline");
            r = skippedResult("offline");
        } else if (!withProvider([&](CloudCtx& ctx) { r = preLaunchSync(ctx, unit, title, cancel); })) {
            log("NL_CLOUD " + unit.id + " skip unlinked");
            r = skippedResult("unlinked");
        }
        if (r.kind == PreLaunch::Skipped && r.reason == "auth") log("NL_CLOUD_REFRESH fail");
        LWP_MutexLock(mutex_);
        pre_ = std::move(r);
        LWP_MutexUnlock(mutex_);
    });
}

bool CloudWorker::startResolve(SaveUnit unit, std::string title, ConflictChoice choice, std::string seenRev) {
    return submit(true, [this, unit = std::move(unit), title = std::move(title), choice,
                         seenRev = std::move(seenRev)](const volatile bool& cancel) {
        PreLaunchResult r;
        if (!service_.networkUp()) {
            r = skippedResult("offline");
        } else if (!withProvider(
                                 [&](CloudCtx& ctx) { r = resolveConflict(ctx, unit, title, choice, seenRev, cancel); })) {
            r = skippedResult("unlinked");
        }
        LWP_MutexLock(mutex_);
        pre_ = std::move(r);
        LWP_MutexUnlock(mutex_);
    });
}

std::optional<PreLaunchResult> CloudWorker::takePreLaunch() {
    LWP_MutexLock(mutex_);
    std::optional<PreLaunchResult> r = std::move(pre_);
    pre_.reset();
    LWP_MutexUnlock(mutex_);
    return r;
}

bool CloudWorker::startUploads(std::vector<CloudUpload> items) {
    if (items.empty()) return false;
    return submit(false, [this, items = std::move(items)](const volatile bool& cancel) {
        if (!service_.networkUp()) {
            log("NL_CLOUD_UPLOAD offline");
            return;
        }
        const bool linkedOk = withProvider([&](CloudCtx& ctx) {
            for (const CloudUpload& it : items) {
                if (cancel) break;
                if (it.played != 0) notePlayed(ctx, it.unit.id, it.played);
                const CloudStatus s = uploadIfChanged(ctx, it.unit, it.title, cancel);
                log("NL_CLOUD_UPLOAD " + it.unit.id + " " + cloudStatusName(s));
                if (s == CloudStatus::Ok) {
                    LWP_MutexLock(mutex_);
                    uploaded_.push_back(it.key);
                    LWP_MutexUnlock(mutex_);
                } else if (s == CloudStatus::Offline || s == CloudStatus::Unauthorized) {
                    break;  // die übrigen scheitern genauso
                }
            }
        });
        if (!linkedOk) log("NL_CLOUD_UPLOAD unlinked");
    });
}

std::vector<std::string> CloudWorker::takeUploaded() {
    LWP_MutexLock(mutex_);
    std::vector<std::string> out = std::move(uploaded_);
    uploaded_.clear();
    LWP_MutexUnlock(mutex_);
    return out;
}

// ---- Alle abgleichen ------------------------------------------------------------------------------------------

bool CloudWorker::startSyncAll(std::vector<SyncTarget> targets, ConflictPolicy policy) {
    syncDone_ = 0;
    syncTotal_ = static_cast<u32>(targets.size());
    return submit(false, [this, targets = std::move(targets), policy](const volatile bool& cancel) {
        SyncAllResult res;
        if (!service_.networkUp()) {
            res.stopped = true;
            log("NL_CLOUD_SYNCALL offline");
        } else if (!withProvider([&](CloudCtx& ctx) {
                       res = syncAll(ctx, targets, policy, cancel, [this](u32 done, u32 total) {
                           syncDone_ = done;
                           syncTotal_ = total;
                       });
                   })) {
            res.stopped = true;
            log("NL_CLOUD_SYNCALL unlinked");
        }
        log(syncAllLine(res));
        LWP_MutexLock(mutex_);
        syncAll_ = std::move(res);
        LWP_MutexUnlock(mutex_);
    });
}

std::optional<SyncAllResult> CloudWorker::takeSyncAll() {
    LWP_MutexLock(mutex_);
    std::optional<SyncAllResult> r = std::move(syncAll_);
    syncAll_.reset();
    LWP_MutexUnlock(mutex_);
    return r;
}

// ---- Abmelden ---------------------------------------------------------------------------------------------------

std::string CloudWorker::revokeUrlFor(const std::string& provider) const {
    if (provider == cfg_.provider) return cfg_.endpoints.revokeUrl;
    if (provider == "drive") return cfg_.driveRevokeUrl;
    return std::string();  // OneDrive: kein Widerruf
}

RevokeResult CloudWorker::revoke(HttpTransport& http, const StoredTokens& t, const volatile bool& cancel) {
    OAuthEndpoints ep;
    ep.revokeUrl = revokeUrlFor(t.provider);
    if (ep.revokeUrl.empty()) return RevokeResult::Unsupported;
    if (!service_.networkUp()) return RevokeResult::Failed;
    return revokeToken(http, ep, t.tokens.refresh, cancel);
}

void CloudWorker::forget(const std::string& account) {
    const std::string statePath = cfg_.dataDir + "/cloud/state.ini";
    auto r = fs_.readAll(statePath, 1024 * 1024);
    if (!r.isOk()) return;
    const auto& b = r.value();
    CloudState state = CloudState::parse(std::string_view(reinterpret_cast<const char*>(b.data()), b.size()));
    state.clearAccount(account);
    const std::string text = state.serialize();
    (void)fs_.writeAtomic(statePath, {reinterpret_cast<const u8*>(text.data()), text.size()});
}

bool CloudWorker::startUnlink() {
    return submit(false, [this](const volatile bool& cancel) {
        const auto stored = loadTokens(fs_, tokensPath());
        RevokeResult res = RevokeResult::Unsupported;
        if (stored) {
            WtlsTransport http;
            res = revoke(http, *stored, cancel);
        }
        // Lokal abmelden gelingt immer, auch offline oder wenn der Widerruf scheitert.
        (void)fs_.removeFile(tokensPath());
        cached_.reset();
        if (stored) forget(stored->account);
        link_ = static_cast<u8>(LinkState::Idle);
        log(std::string("NL_CLOUD_UNLINKED revoke=") + revokeName(res));
        LWP_MutexLock(mutex_);
        unlinked_ = res;
        LWP_MutexUnlock(mutex_);
    });
}

std::optional<RevokeResult> CloudWorker::takeUnlinked() {
    LWP_MutexLock(mutex_);
    std::optional<RevokeResult> r = unlinked_;
    unlinked_.reset();
    LWP_MutexUnlock(mutex_);
    return r;
}

// ---- Haupt-Thread ---------------------------------------------------------------------------------------------

bool CloudWorker::linked() {
    if (cfg_.dataDir.empty() || running_) return false;
    const auto t = loadTokens(fs_, tokensPath());
    return t && t->provider == cfg_.provider;  // nach einem Wechsel erst neu anmelden
}

}  // namespace nl::platform
