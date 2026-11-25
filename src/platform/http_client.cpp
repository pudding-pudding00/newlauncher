#include "platform/http_client.hpp"

#include <cstdio>
#include <functional>
#include <span>
#include <string>
#include <vector>

#ifdef NL_TLS
#include <malloc.h>
#include <wiitls/http.h>
#else
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <network.h>
#include <ogc/lwp_watchdog.h>
#include <unistd.h>
#endif

namespace nl::platform {

namespace {
// Nur vom Haupt-Thread vor dem ersten Auftrag geschrieben (submit ordnet über den Mutex), danach nur gelesen.
std::string g_tlsCaPath;
bool g_perf = false;  // [debug] perf: NL_PERF tls je Handshake
// M23: Stand der letzten Anfrage im Netz-Thread (nur dort gelesen und geschrieben)
bool g_lastReused = false;
u32 g_lastHandshakeMs = 0;

void emit(const HttpLog& log, std::string line) {
    if (log) log(std::move(line));
}

bool isCancelled(const volatile bool* cancel) { return cancel != nullptr && *cancel; }

#ifdef NL_TLS
// M24: wii_tls fragt das während Verbindungsaufbau, Handshake und E/A ab; ein Abbruch wirkt so ohne Zeitlimit.
bool tlsAbort(void* ctx) { return isCancelled(static_cast<const volatile bool*>(ctx)); }
#endif

struct FileSink {
    static constexpr std::size_t kBuf = 64 * 1024;
    std::FILE* f = nullptr;
    std::vector<u8> buf;
    bool ok = true;
    bool flush() {
        if (!buf.empty() && std::fwrite(buf.data(), 1, buf.size(), f) != buf.size()) ok = false;
        buf.clear();
        return ok;
    }
    bool put(std::span<const u8> data) {
        if (buf.size() + data.size() > kBuf && !flush()) return false;
        buf.insert(buf.end(), data.begin(), data.end());
        return true;
    }
};

#ifdef NL_TLS
constexpr u32 kTlsConnectMs = 8000, kTlsHandshakeMs = 10000, kTlsIoMs = 15000, kSntpMs = 3000;
constexpr int kMaxSntpAttempts = 2;  // je Sitzung: der erste und genau ein neuer Versuch
constexpr int kMaxRedirects = 5;     // GitHub-Releases: github.com → CDN (M20)
constexpr int64_t kMaxDrainBytes = 16384;

// Sitzungszustand, nur im Netz-Thread benutzt.
bool g_caTried = false;
volatile bool g_clockSynced = false;  // einziger Schreiber: Netz-Thread; clockSynced() liest den Stand
int g_sntpAttempts = 0;

void loadTestCa(const HttpLog& log) {
    if (g_caTried) return;
    g_caTried = true;
    if (g_tlsCaPath.empty()) return;
    std::vector<u8> pem;
    if (std::FILE* f = std::fopen(g_tlsCaPath.c_str(), "rb")) {
        u8 buf[1024];
        std::size_t n = 0;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0 && pem.size() < 64 * 1024) pem.insert(pem.end(), buf, buf + n);
        std::fclose(f);
    }
    bool ok = !pem.empty();
    if (ok) {
        pem.push_back(0);  // PEM-Puffer brauchen das abschließende NUL in len
        ok = wtls_add_trusted_ca(pem.data(), pem.size()) == WTLS_OK;
    }
    emit(log, ok ? "NL_TLS_CA ok" : "NL_TLS_CA fail");
}

NetError tlsError(int status, u32 flags, bool secure) {
    if (status == WTLS_ERR_STATE || status == WTLS_ERR_NET) return secure ? NetError::TlsMissing : NetError::Other;
    if (tlsClockSuspect(status, flags)) return NetError::Clock;
    return netErrorFromTls(status);
}

// Kleine Nicht-200-Antworten (z. B. 404 bei der Cover-Suche) weglesen, damit wtls_http_close die Verbindung im
// Keep-Alive-Pool behält.
void drainSmallBody(wtls_http_response* res, const volatile bool* cancel) {
    if (wtls_http_content_length(res) > kMaxDrainBytes) return;
    u8 buf[1024];
    int64_t total = 0;
    while (!isCancelled(cancel) && total < kMaxDrainBytes) {
        const int n = wtls_http_read(res, buf, sizeof buf);
        if (n <= 0) break;
        total += n;
    }
}

HttpResult wtlsRequest(const Url& url, u32 maxBody, const volatile bool* cancel, const HttpLog& log,
                       const std::function<bool(std::span<const u8>)>& sink) {
    loadTestCa(log);
    if (isCancelled(cancel)) return {HttpOutcome::Failed, NetError::Other, 0};

    const std::string urlStr = formatUrl(url);
    static const wtls_http_header kHeaders[] = {{"User-Agent", "newlauncher"}};
    wtls_options tlsOpt{};
    tlsOpt.connect_timeout_ms = kTlsConnectMs;
    tlsOpt.handshake_timeout_ms = kTlsHandshakeMs;
    tlsOpt.io_timeout_ms = kTlsIoMs;
    if (cancel != nullptr) {
        tlsOpt.abort_cb = tlsAbort;
        tlsOpt.abort_ctx = const_cast<bool*>(cancel);  // nur gelesen (tlsAbort)
    }

    wtls_http_request req{};
    req.url = urlStr.c_str();
    req.headers = kHeaders;
    req.n_headers = 1;
    req.max_redirects = kMaxRedirects;
    req.header_timeout_ms = kTlsIoMs;
    req.tls = &tlsOpt;

    wtls_http_response* res = nullptr;
    g_lastReused = false;
    g_lastHandshakeMs = 0;
    int heapBefore = 0;
    auto openOnce = [&]() {
        heapBefore = g_perf ? mallinfo().uordblks : 0;
        return wtls_http_open(&res, &req);
    };

    // Der Handshake selbst ist nicht unterbrechbar: vor jedem open auf Abbruch prüfen (wie bisher).
    int st = openOnce();
    if (st != WTLS_OK && tlsClockSuspect(st, wtls_last_verify_flags()) && !g_clockSynced &&
        g_sntpAttempts < kMaxSntpAttempts && !isCancelled(cancel)) {
        ++g_sntpAttempts;
        g_clockSynced = wtls_time_sync_sntp(nullptr, kSntpMs) == WTLS_OK;
        emit(log, g_clockSynced ? "NL_TLS_TIMESYNC ok" : "NL_TLS_TIMESYNC fail");
        if (isCancelled(cancel)) return {HttpOutcome::Failed, NetError::Other, 0};
        if (g_clockSynced) st = openOnce();
    }
    if (st != WTLS_OK) {
        if (st == WTLS_ERR_HTTP_REDIRECT) emit(log, "NL_HTTP_REDIRECT abgelehnt");
        const NetError err = tlsError(st, wtls_last_verify_flags(), url.tls);
        if (err != NetError::TlsMissing && !isCancelled(cancel))
            emit(log, std::string("NL_NET_FAIL ") + netErrorName(err));
        return {HttpOutcome::Failed, err, 0};
    }

    wtls_http_info info{};
    wtls_http_get_info(res, &info);
    g_lastReused = info.reused_connection;
    g_lastHandshakeMs = info.secure && !info.reused_connection ? static_cast<u32>(info.tls.handshake_ms) : 0;
    if (g_perf && info.secure && !info.reused_connection) {
        char line[64];
        std::snprintf(line, sizeof line, "NL_PERF tls heap=%d ms=%u", mallinfo().uordblks - heapBefore,
                      static_cast<unsigned>(info.tls.handshake_ms));
        emit(log, line);
    }
    if (info.redirects > 0 && info.url != nullptr) {
        const auto finalUrl = parseUrl(info.url);
        emit(log, "NL_HTTP_REDIRECT " + (finalUrl ? finalUrl->host : std::string(info.url)));
    }

    const int rawStatus = wtls_http_status(res);
    const u16 status = static_cast<u16>(rawStatus > 0 ? rawStatus : 0);
    if (status != 200) {
        drainSmallBody(res, cancel);
        wtls_http_close(res);
        return {status == 404 ? HttpOutcome::NotFound : HttpOutcome::Failed, NetError::Http, status};
    }

    const int64_t cl = wtls_http_content_length(res);
    if (cl > static_cast<int64_t>(maxBody)) {
        wtls_http_close(res);
        emit(log, "NL_NET_FAIL other");
        return {HttpOutcome::Failed, NetError::Other, status};
    }

    u8 buf[4096];
    u64 total = 0;
    for (;;) {
        if (isCancelled(cancel)) {
            wtls_http_close(res);
            return {HttpOutcome::Failed, NetError::Other, status};
        }
        const int n = wtls_http_read(res, buf, sizeof buf);
        if (n == 0) break;
        if (n < 0) {
            wtls_http_close(res);
            const NetError err = tlsError(n, 0, info.secure);
            if (!isCancelled(cancel) && err != NetError::TlsMissing)
                emit(log, std::string("NL_NET_FAIL ") + netErrorName(err));
            return {HttpOutcome::Failed, err, status};
        }
        if (total + static_cast<u64>(n) > maxBody || !sink(std::span<const u8>(buf, static_cast<std::size_t>(n)))) {
            wtls_http_close(res);
            if (!isCancelled(cancel)) emit(log, "NL_NET_FAIL other");
            return {HttpOutcome::Failed, NetError::Other, status};
        }
        total += static_cast<u64>(n);
    }
    wtls_http_close(res);
    return {HttpOutcome::Ok, NetError::None, status};
}
#else
constexpr s32 kTimeoutMs = 10000;
constexpr s32 kSliceMs = 100;    // Abbruch-Prüfung beim Warten
constexpr u32 kIosNonBlock = 4;  // IOS-Wert für O_NONBLOCK; libogc reicht net_fcntl-Flags unverändert durch
constexpr s32 kPollIn = 0x0001, kPollOut = 0x0008;

class Transport {
public:
    virtual ~Transport() = default;
    virtual s32 send(std::span<const u8> data) = 0;
    virtual s32 recv(std::span<u8> buf) = 0;
    virtual void close() = 0;
    NetError error() const { return error_; }

protected:
    NetError error_ = NetError::None;
};

bool isIpLiteral(const std::string& host) {
    if (host.empty()) return false;
    for (char c : host)
        if ((c < '0' || c > '9') && c != '.') return false;
    return true;
}

bool resolve(const std::string& host, in_addr& out) {
    if (isIpLiteral(host)) return inet_aton(host.c_str(), &out) != 0;
    hostent* h = net_gethostbyname(host.c_str());
    if (h == nullptr || h->h_addr_list == nullptr || h->h_addr_list[0] == nullptr) return false;
    std::memcpy(&out, h->h_addr_list[0], sizeof out);
    return true;
}

u64 elapsedMs(u64 start) { return ticks_to_millisecs(diff_ticks(start, gettime())); }

// Wartet auf ein Ereignis auf sock. None = bereit; sonst Timeout, Other (Fehler oder Abbruch).
NetError waitFor(s32 sock, u32 events, const volatile bool* cancel, s32 timeoutMs) {
    const u64 start = gettime();
    while (!isCancelled(cancel)) {
        pollsd p{sock, events, 0};
        const s32 r = net_poll(&p, 1, kSliceMs);
        if (r > 0) return (p.revents & events) != 0 ? NetError::None : NetError::Other;
        if (r < 0) return NetError::Other;
        if (elapsedMs(start) > static_cast<u64>(timeoutMs)) return NetError::Timeout;
    }
    return NetError::Other;
}

class PlainTransport final : public Transport {
public:
    explicit PlainTransport(const volatile bool* cancel) : cancel_(cancel) {}
    ~PlainTransport() override { close(); }

    bool open(const Url& url) {
        in_addr ip{};
        if (!resolve(url.host, ip)) return fail(NetError::Dns);
        fd_ = net_socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
        if (fd_ < 0) return fail(NetError::Other);
        net_fcntl(fd_, F_SETFL, kIosNonBlock);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(url.port);
        addr.sin_addr = ip;
        const u64 start = gettime();
        for (;;) {
            if (isCancelled(cancel_)) return fail(NetError::Other);
            const s32 r = net_connect(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof addr);
            if (r >= 0 || r == -EISCONN) return true;
            if (r != -EINPROGRESS && r != -EALREADY && r != -EAGAIN) return fail(NetError::Connect);
            if (elapsedMs(start) > static_cast<u64>(kTimeoutMs)) return fail(NetError::Timeout);
            usleep(20 * 1000);
        }
    }

    s32 send(std::span<const u8> data) override {
        for (;;) {
            const s32 r = net_send(fd_, data.data(), static_cast<s32>(data.size()), 0);
            if (r > 0) return r;
            if (r != -EAGAIN) return err(NetError::Other);
            const NetError w = waitFor(fd_, kPollOut, cancel_, kTimeoutMs);
            if (w != NetError::None) return err(w);
        }
    }

    s32 recv(std::span<u8> buf) override {
        for (;;) {
            const s32 r = net_recv(fd_, buf.data(), static_cast<s32>(buf.size()), 0);
            if (r >= 0) return r;
            if (r != -EAGAIN) return err(NetError::Other);
            const NetError w = waitFor(fd_, kPollIn, cancel_, kTimeoutMs);
            if (w != NetError::None) return err(w);
        }
    }

    void close() override {
        if (fd_ >= 0) net_close(fd_);
        fd_ = -1;
    }

private:
    bool fail(NetError e) {
        error_ = e;
        return false;
    }
    s32 err(NetError e) {
        error_ = e;
        return -1;
    }

    const volatile bool* cancel_;
    s32 fd_ = -1;
};

bool sendAll(Transport& t, const std::string& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        const s32 r = t.send(std::span<const u8>(reinterpret_cast<const u8*>(data.data()) + sent, data.size() - sent));
        if (r <= 0) return false;
        sent += static_cast<std::size_t>(r);
    }
    return true;
}

// Verbinden, Anfrage senden, Antwort in den Parser füttern.
HttpResult exchange(const Url& url, HttpParser& parser, const volatile bool* cancel, const HttpLog& log) {
    if (url.tls) {
        emit(log, "NL_TLS_MISSING");
        return {HttpOutcome::Failed, NetError::TlsMissing, 0};
    }
    auto plain = std::make_unique<PlainTransport>(cancel);
    if (!plain->open(url)) return {HttpOutcome::Failed, plain->error(), 0};
    if (!sendAll(*plain, buildGetRequest(url))) return {HttpOutcome::Failed, plain->error(), 0};

    u8 buf[4096];
    HttpParser::State state = HttpParser::State::Headers;
    while (state == HttpParser::State::Headers || state == HttpParser::State::Body) {
        // Auch bei durchgehend ankommenden Daten (kein EAGAIN, z. B. schnelles LAN) muss der Abbruch sofort greifen.
        if (isCancelled(cancel)) return {HttpOutcome::Failed, NetError::Other, 0};
        const s32 r = plain->recv(std::span<u8>(buf, sizeof buf));
        if (r > 0) {
            state = parser.feed(std::span<const u8>(buf, static_cast<std::size_t>(r)));
        } else if (r == 0) {
            state = parser.finish();
        } else {
            return {HttpOutcome::Failed, plain->error(), 0};
        }
    }
    const u16 status = static_cast<u16>(parser.status() > 0 ? parser.status() : 0);
    if (state != HttpParser::State::Done) return {HttpOutcome::Failed, NetError::Other, status};
    if (status == 404) return {HttpOutcome::NotFound, NetError::Http, status};
    if (status != 200) return {HttpOutcome::Failed, NetError::Http, status};
    return {HttpOutcome::Ok, NetError::None, status};
}

HttpResult request(const Url& first, HttpParser& parser, const volatile bool* cancel, const HttpLog& log) {
    constexpr int kMaxRedirects = 5;  // GitHub-Releases: github.com → CDN (M20)
    Url url = first;
    HttpResult r = exchange(url, parser, cancel, log);
    for (int hop = 0; hop < kMaxRedirects && r.error == NetError::Http && isRedirectStatus(r.status); ++hop) {
        const auto next = redirectTarget(url, parser.location());
        if (!next) {
            emit(log, "NL_HTTP_REDIRECT abgelehnt");  // z. B. HTTPS → HTTP
            break;
        }
        url = *next;
        emit(log, "NL_HTTP_REDIRECT " + url.host);
        parser.reset();
        r = exchange(url, parser, cancel, log);
    }
    // Abbruch ist kein Netzfehler; TlsMissing hat seinen eigenen Marker.
    if (r.error != NetError::None && r.error != NetError::Http && r.error != NetError::TlsMissing &&
        !isCancelled(cancel))
        emit(log, std::string("NL_NET_FAIL ") + netErrorName(r.error));
    return r;
}
#endif
}  // namespace

void setTlsCaPath(std::string path) { g_tlsCaPath = std::move(path); }
void setNetPerf(bool on) { g_perf = on; }
bool netPerf() { return g_perf; }
bool lastRequestReused() { return g_lastReused; }
u32 lastHandshakeMs() { return g_lastHandshakeMs; }

HttpResult httpGet(const Url& url, u32 maxBody, std::vector<u8>& body, const volatile bool* cancel,
                   const HttpLog& log) {
    body.clear();
#ifdef NL_TLS
    const HttpResult r = wtlsRequest(url, maxBody, cancel, log, [&](std::span<const u8> data) {
        body.insert(body.end(), data.begin(), data.end());
        return true;
    });
    if (r.outcome != HttpOutcome::Ok) body.clear();
    return r;
#else
    HttpParser parser(maxBody);
    const HttpResult r = request(url, parser, cancel, log);
    if (r.outcome == HttpOutcome::Ok) body.assign(parser.body().begin(), parser.body().end());
    return r;
#endif
}

HttpResult httpDownload(const Url& url, const std::string& destFile, u32 maxBody, const volatile bool* cancel,
                        volatile u32* received, const HttpLog& log) {
    FileSink sink;
    sink.f = std::fopen(destFile.c_str(), "wb");
    if (sink.f == nullptr) return {HttpOutcome::Failed, NetError::Other, 0};
    sink.buf.reserve(FileSink::kBuf);
#ifdef NL_TLS
    HttpResult r = wtlsRequest(url, maxBody, cancel, log, [&](std::span<const u8> data) {
        if (received != nullptr) *received = *received + static_cast<u32>(data.size());
        return sink.put(data);
    });
#else
    HttpParser parser(maxBody);
    parser.setSink([&](std::span<const u8> data) {
        if (received != nullptr) *received = *received + static_cast<u32>(data.size());
        return sink.put(data);
    });
    HttpResult r = request(url, parser, cancel, log);
#endif
    // Ein Fehlerstatus (404 o. ä.) liefert eine Fehlerseite in die Senke; die Datei wird unten gelöscht.
    const bool flushed = sink.flush();
    if (std::fclose(sink.f) != 0 || !flushed) r.outcome = HttpOutcome::Failed;
    if (r.outcome != HttpOutcome::Ok) std::remove(destFile.c_str());
    return r;
}

bool clockSynced() {
#ifdef NL_TLS
    return g_clockSynced;
#else
    return false;
#endif
}

bool syncClockOnce(const HttpLog& log) {
#ifdef NL_TLS
    if (!g_clockSynced && g_sntpAttempts < kMaxSntpAttempts) {
        ++g_sntpAttempts;
        g_clockSynced = wtls_time_sync_sntp(nullptr, kSntpMs) == WTLS_OK;
        emit(log, g_clockSynced ? "NL_TLS_TIMESYNC ok" : "NL_TLS_TIMESYNC fail");
    }
    return g_clockSynced;
#else
    (void)log;
    return false;
#endif
}

}  // namespace nl::platform
