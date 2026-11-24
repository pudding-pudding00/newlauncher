#include "platform/net_service.hpp"

#include <utility>

#include <network.h>
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
static_assert(nl::kX509BadCertExpired == MBEDTLS_X509_BADCERT_EXPIRED);
static_assert(nl::kX509BadCertFuture == MBEDTLS_X509_BADCERT_FUTURE);
#endif

namespace nl::platform {

namespace {
constexpr u32 kStackBytes = 64 * 1024;
constexpr u8 kPriority = 40;  // wie Scan und Cover-Thread: unter dem Haupt-Thread
}  // namespace

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

void NetService::probeWifi() {
    if (!netUp_) {
        wifi_ = static_cast<u8>(WifiStatus::Connecting);
        const int attempts = initialProbeDone_ ? 1 : 2;
        initialProbeDone_ = true;
        s32 r = -1;
        for (int i = 0; i < attempts && r < 0 && !stop_; ++i) {
            r = net_init();
            if (r < 0 && i + 1 < attempts && !stop_) usleep(500 * 1000);
        }
        netCode_ = r;
        netUp_ = (r >= 0 && net_gethostip() != 0);
        wifi_ = static_cast<u8>(netUp_ ? WifiStatus::Connected : WifiStatus::Disconnected);
        return;
    }
    const bool alive = (net_gethostip() != 0);
    if (!alive) netUp_ = false;
    wifi_ = static_cast<u8>(alive ? WifiStatus::Connected : WifiStatus::Disconnected);
}

void NetService::prepare() {
    if (netUp_ && net_gethostip() == 0) netUp_ = false;
    if (!netUp_) {
        wifi_ = static_cast<u8>(WifiStatus::Connecting);
        s32 r = -1;
        for (int i = 0; i < 3 && r < 0 && !stop_; ++i) {
            r = net_init();
            if (r < 0 && i + 1 < 3 && !stop_) usleep(500 * 1000);
        }
        netCode_ = r;
        netUp_ = (r >= 0 && net_gethostip() != 0);
    }
    wifi_ = static_cast<u8>(netUp_ ? WifiStatus::Connected : WifiStatus::Disconnected);
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
            LWP_MutexUnlock(mutex_);
            probeWifi();
        }
    }
#if defined(NL_TLS) && NL_TLS
    if (tlsUp_) wtls_shutdown();
    tlsUp_ = false;
#endif
}

}  // namespace nl::platform
