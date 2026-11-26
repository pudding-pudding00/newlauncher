#include "platform/net_service.hpp"

#include <utility>

#include <network.h>
#include <ogc/lwp_watchdog.h>
#include <unistd.h>

#include "core/net_error.hpp"

#if defined(NL_TLS) && NL_TLS
#include <mbedtls/x509.h>
#include <wiitls/wiitls.h>

// core/net_error.hpp führt die Zahlen ohne Fremd-Header; hier wird geprüft, dass sie zu wii_tls und Mbed TLS passen.
static_assert(nl::wtls_status::kOk == WTLS_OK);
static_assert(nl::wtls_status::kNoMem == WTLS_ERR_NOMEM);
static_assert(nl::wtls_status::kClock == WTLS_ERR_CLOCK);
static_assert(nl::wtls_status::kDns == WTLS_ERR_DNS);
static_assert(nl::wtls_status::kConnect == WTLS_ERR_CONNECT);
static_assert(nl::wtls_status::kTimeout == WTLS_ERR_TIMEOUT);
static_assert(nl::wtls_status::kHandshake == WTLS_ERR_HANDSHAKE);
static_assert(nl::wtls_status::kCert == WTLS_ERR_CERT);
static_assert(nl::wtls_status::kHttp == WTLS_ERR_HTTP);
static_assert(nl::wtls_status::kHttpLimit == WTLS_ERR_HTTP_LIMIT);
static_assert(nl::wtls_status::kHttpRedirect == WTLS_ERR_HTTP_REDIRECT);
static_assert(nl::kX509BadCertExpired == MBEDTLS_X509_BADCERT_EXPIRED);
static_assert(nl::kX509BadCertFuture == MBEDTLS_X509_BADCERT_FUTURE);
#endif

namespace nl::platform {

namespace {
constexpr u32 kStackBytes = 64 * 1024;
constexpr u8 kPriority = 40;  // wie Scan und Cover-Thread: unter dem Haupt-Thread

// M25: Router-Check (Spec 2026-11-26-m25-wlan-design.md 2).
constexpr u16 kGatewayPort = 80;
constexpr u64 kGatewayTimeoutMs = 1500;
constexpr u64 kLimitedReinitMs = 60000;
// IOS-Werte wie in wii_tls (plat_ogc.c) und http_client.cpp.
constexpr u32 kIosFGetFl = 3, kIosFSetFl = 4, kIosNonBlock = 0x04, kIosPollOut = 0x0008;

// Nicht blockierender TCP-Connect auf gateway:80. Verbunden oder abgewiesen (RST) = Router antwortet.
// IOS weckt net_poll nur für ein RST, nicht für einen fertigen Connect: deshalb in kurzen Scheiben pollen und
// den Connect jedes Mal erneut fragen (wie wii_tls). Ergebnis: Antwortzeit in ms oder -1.
s32 measureGatewayMs(u32 gatewayHost) {
    const s32 fd = net_socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (fd < 0) return -1;
    s32 fl = net_fcntl(fd, kIosFGetFl, 0);
    if (fl < 0) fl = 0;
    net_fcntl(fd, kIosFSetFl, static_cast<u32>(fl) | kIosNonBlock);
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(kGatewayPort);
    sa.sin_addr.s_addr = htonl(gatewayHost);
    const u64 start = NetService::clockMs();
    s32 result = -1;
    s32 r = net_connect(fd, reinterpret_cast<sockaddr*>(&sa), sizeof sa);
    for (;;) {
        const GatewayReply g = classifyGatewayConnect(r);
        const u64 elapsed = NetService::clockMs() - start;
        if (gatewayReachable(g)) {
            result = static_cast<s32>(elapsed);
            break;
        }
        if (g == GatewayReply::Failed || elapsed >= kGatewayTimeoutMs) break;
        pollsd p{fd, kIosPollOut, 0};
        net_poll(&p, 1, 10);
        r = net_connect(fd, reinterpret_cast<sockaddr*>(&sa), sizeof sa);
    }
    net_close(fd);
    return result;
}
}  // namespace

u64 NetService::clockMs() { return ticks_to_millisecs(gettime()); }

NetService::NetService() {
    LWP_MutexInit(&mutex_, false);
    LWP_CondInit(&cond_);
}

NetService::~NetService() {
    shutdown();
    LWP_CondDestroy(cond_);
    LWP_MutexDestroy(mutex_);
}

bool NetService::ensureThreadLocked() {
    if (thread_ != LWP_THREAD_NULL) return true;
    if (LWP_CreateThread(&thread_, &NetService::entry, this, nullptr, kStackBytes, kPriority) == 0) return true;
    thread_ = LWP_THREAD_NULL;
    return false;
}

u32 NetService::submit(NetJobFn job) {
    LWP_MutexLock(mutex_);
    if (stopped_ || !ensureThreadLocked()) {
        LWP_MutexUnlock(mutex_);
        return 0;
    }
    const u32 id = queue_.push(std::move(job));
    LWP_CondBroadcast(cond_);
    LWP_MutexUnlock(mutex_);
    return id;
}

void NetService::startWifiProbe() {
    LWP_MutexLock(mutex_);
    if (stopped_ || !ensureThreadLocked()) {
        LWP_MutexUnlock(mutex_);
        return;
    }
    probeRequested_ = true;
    LWP_CondBroadcast(cond_);
    LWP_MutexUnlock(mutex_);
}

void NetService::cancel(u32 id) {
    LWP_MutexLock(mutex_);
    queue_.cancel(id);
    LWP_MutexUnlock(mutex_);
}

void NetService::cancelAndWait(u32 id) {
    LWP_MutexLock(mutex_);
    queue_.cancel(id);
    while (thread_ != LWP_THREAD_NULL && queue_.active(id)) LWP_CondWait(cond_, mutex_);
    LWP_MutexUnlock(mutex_);
}

void NetService::shutdown() {
    LWP_MutexLock(mutex_);
    if (stopped_) {
        LWP_MutexUnlock(mutex_);
        return;
    }
    stopped_ = true;
    stop_ = true;
    probeRequested_ = false;
    reconnectRequested_ = false;
    queue_.cancelAll();
    LWP_CondBroadcast(cond_);
    const lwp_t t = thread_;
    LWP_MutexUnlock(mutex_);
    if (t != LWP_THREAD_NULL) LWP_JoinThread(t, nullptr);
    thread_ = LWP_THREAD_NULL;
}

bool NetService::busy() const {
    LWP_MutexLock(mutex_);
    const bool b = queue_.current() != 0 || queue_.pending() > 0;
    LWP_MutexUnlock(mutex_);
    return b;
}

bool NetService::active(u32 id) const {
    LWP_MutexLock(mutex_);
    const bool a = queue_.active(id);
    LWP_MutexUnlock(mutex_);
    return a;
}

void* NetService::entry(void* self) {
    static_cast<NetService*>(self)->loop();
    return nullptr;
}

void NetService::requestReconnect() {
    LWP_MutexLock(mutex_);
    if (stopped_ || !ensureThreadLocked()) {
        LWP_MutexUnlock(mutex_);
        return;
    }
    reconnectRequested_ = true;
    probeRequested_ = true;
    LWP_CondBroadcast(cond_);
    LWP_MutexUnlock(mutex_);
}

bool NetService::probePending() const {
    LWP_MutexLock(mutex_);
    const bool p = probeRequested_ || probing_;
    LWP_MutexUnlock(mutex_);
    return p;
}

WifiSnapshot NetService::snapshot() const {
    LWP_MutexLock(mutex_);
    WifiSnapshot s = snap_;
    LWP_MutexUnlock(mutex_);
#if defined(NL_TLS) && NL_TLS
    s.tlsBuild = true;
#endif
    s.tlsUp = tlsUp_;
    return s;
}

void NetService::publish(WifiStatus status) {
    LWP_MutexLock(mutex_);
    snap_.status = status;
    wifi_ = static_cast<u8>(status);
    LWP_MutexUnlock(mutex_);
}

bool NetService::bringUp(WifiProbeKind kind) {
    publish(WifiStatus::Connecting);
    const int attempts = wifiInitAttempts(kind);
    s32 r = -1;
    bool up = false;
    for (int i = 0; i < attempts && !stop_; ++i) {
        r = net_init();
        up = r >= 0 && net_gethostip() != 0;
        if (up) break;
        net_deinit();  // halb aufgebautes Netz verwerfen, der nächste Versuch beginnt frisch
        if (i + 1 < attempts && !stop_) usleep(wifiRetryDelayMs(i) * 1000);
    }
    netCode_ = r;
    netUp_ = up;
    in_addr ip{}, mask{}, gw{};
    // Nach erfolgreichem net_init ist net_init in if_configex ein No-op; es liefert nur die Adressen.
    if (up && if_configex(&ip, &mask, &gw, true, 1) < 0) {
        ip.s_addr = net_gethostip();
        mask.s_addr = 0;
        gw.s_addr = 0;
    }
    u8 mac[6] = {};
    const bool macOk = net_get_mac_address(mac) >= 0;
    const u64 now = clockMs();
    LWP_MutexLock(mutex_);
    snap_.lastInitCode = r;
    snap_.checked = true;
    snap_.lastCheckMs = now;
    if (macOk) {
        for (int i = 0; i < 6; ++i) snap_.mac[static_cast<std::size_t>(i)] = mac[i];
        snap_.macKnown = true;
    }
    if (up) {
        snap_.ip = ntohl(ip.s_addr);
        snap_.netmask = ntohl(mask.s_addr);
        snap_.gateway = ntohl(gw.s_addr);
        snap_.failedProbes = 0;
        snap_.connectedSinceMs = now;
        // gatewayFails bleibt: Ein Neuaufbau wegen Limited soll nicht zwischen Limited und Connected flackern.
        snap_.status = wifiStatusFrom(true, snap_.gatewayFails);
    } else {
        snap_.ip = snap_.netmask = snap_.gateway = 0;
        snap_.gatewayMs = -1;
        snap_.connectedSinceMs = 0;
        ++snap_.failedProbes;
        snap_.status = WifiStatus::Disconnected;
    }
    wifi_ = static_cast<u8>(snap_.status);
    LWP_MutexUnlock(mutex_);
#if defined(NL_TLS) && NL_TLS
    if (up && tlsUp_) wtls_net_reset();  // DNS-Cache und Keep-alive-Verbindungen gehören zum alten Netz
#endif
    return up;
}

void NetService::tearDown() {
    net_deinit();
    netUp_ = false;
    LWP_MutexLock(mutex_);
    ++snap_.reconnects;
    LWP_MutexUnlock(mutex_);
}

void NetService::checkGateway() {
    LWP_MutexLock(mutex_);
    const u32 gw = snap_.gateway;
    LWP_MutexUnlock(mutex_);
    const s32 ms = gw != 0 ? measureGatewayMs(gw) : -1;
    const u64 now = clockMs();
    LWP_MutexLock(mutex_);
    if (gw != 0) {  // ohne bekanntes Gateway kein Router-Check: Zustand nur nach der IP
        snap_.gatewayMs = ms;
        snap_.gatewayFails = ms >= 0 ? 0 : snap_.gatewayFails + 1;
    }
    snap_.status = wifiStatusFrom(true, snap_.gatewayFails);
    snap_.checked = true;
    snap_.lastCheckMs = now;
    wifi_ = static_cast<u8>(snap_.status);
    LWP_MutexUnlock(mutex_);
}

void NetService::probeWifi(WifiProbeKind kind) {
    if (kind == WifiProbeKind::Reconnect && netUp_) tearDown();
    if (netUp_ && net_gethostip() == 0) {
        tearDown();  // IP weg: still neu aufbauen; scheitert das, zeigt die Leiste getrennt
        kind = WifiProbeKind::Background;
    }
    if (!netUp_) {
        if (bringUp(kind)) checkGateway();
        return;
    }
    checkGateway();
    LWP_MutexLock(mutex_);
    const bool limited = snap_.status == WifiStatus::Limited;
    LWP_MutexUnlock(mutex_);
    const u64 now = clockMs();
    if (limited && (lastLimitedReinitMs_ == 0 || now - lastLimitedReinitMs_ >= kLimitedReinitMs)) {
        lastLimitedReinitMs_ = now;  // höchstens einmal je Minute, falls der Router Port 80 nur filtert
        tearDown();
        if (bringUp(WifiProbeKind::Background)) checkGateway();
    }
}

void NetService::prepare() {
    if (netUp_ && net_gethostip() == 0) tearDown();
    if (!netUp_) bringUp(WifiProbeKind::Job);
#if defined(NL_TLS) && NL_TLS
    if (netUp_ && !tlsTried_) {
        tlsTried_ = true;
        tlsUp_ = wtls_init() == WTLS_OK;
    }
#endif
}

void NetService::loop() {
    for (;;) {
        LWP_MutexLock(mutex_);
        while (!stop_ && queue_.pending() == 0 && !probeRequested_) LWP_CondWait(cond_, mutex_);
        if (stop_) {
            LWP_MutexUnlock(mutex_);
            break;
        }
        if (queue_.pending() > 0) {
            std::optional<NetJob> job = queue_.take();
            LWP_MutexUnlock(mutex_);
            prepare();
            job->run(queue_.cancelFlag());
            LWP_MutexLock(mutex_);
            queue_.finish();
            LWP_CondBroadcast(cond_);
            LWP_MutexUnlock(mutex_);
        } else if (probeRequested_) {
            probeRequested_ = false;
            probing_ = true;
            const bool reconnect = reconnectRequested_;
            reconnectRequested_ = false;
            LWP_MutexUnlock(mutex_);
            probeWifi(reconnect           ? WifiProbeKind::Reconnect
                      : initialProbeDone_ ? WifiProbeKind::Background
                                          : WifiProbeKind::Initial);
            initialProbeDone_ = true;
            LWP_MutexLock(mutex_);
            probing_ = false;
            LWP_MutexUnlock(mutex_);
        }
    }
#if defined(NL_TLS) && NL_TLS
    if (tlsUp_) wtls_shutdown();
    tlsUp_ = false;
#endif
}

}  // namespace nl::platform
