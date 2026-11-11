#include "platform/http_client.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <string>

#include <network.h>
#include <ogc/lwp_watchdog.h>
#include <unistd.h>

#ifdef NL_TLS
#include <malloc.h>
#include <wiitls/wiitls.h>
#endif

namespace nl::platform {

namespace {
constexpr s32 kTimeoutMs = 10000;
constexpr s32 kSliceMs = 100;  // Abbruch-Prüfung beim Warten
constexpr u32 kIosNonBlock = 4;  // IOS-Wert für O_NONBLOCK; libogc reicht net_fcntl-Flags unverändert durch
constexpr s32 kPollIn = 0x0001, kPollOut = 0x0008;

// Nur vom Haupt-Thread vor dem ersten Auftrag geschrieben (submit ordnet über den Mutex), danach nur gelesen.
std::string g_tlsCaPath;
bool g_perf = false;  // [debug] perf: NL_PERF tls je Handshake

void emit(const HttpLog& log, std::string line) {
    if (log) log(std::move(line));
}

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

bool isCancelled(const volatile bool* cancel) { return cancel != nullptr && *cancel; }

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

#ifdef NL_TLS
constexpr u32 kTlsConnectMs = 8000, kTlsHandshakeMs = 10000, kTlsIoMs = 15000, kSntpMs = 3000;
constexpr int kMaxSntpAttempts = 2;  // je Sitzung: der erste und genau ein neuer Versuch

// Sitzungszustand, nur im Netz-Thread benutzt.
bool g_caTried = false;
bool g_clockSynced = false;
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

NetError tlsError(int status, u32 flags) {
    if (status == WTLS_ERR_STATE || status == WTLS_ERR_NET) return NetError::TlsMissing;
    if (tlsClockSuspect(status, flags)) return NetError::Clock;
    return netErrorFromTls(status);
}

class TlsTransport final : public Transport {
public:
    explicit TlsTransport(const volatile bool* cancel) : cancel_(cancel) {}
    ~TlsTransport() override { close(); }

    bool open(const Url& url, const HttpLog& log) {
        loadTestCa(log);
        int st = connect(url, log);
        if (st != WTLS_OK && tlsClockSuspect(st, wtls_last_verify_flags()) && !g_clockSynced &&
            g_sntpAttempts < kMaxSntpAttempts && !isCancelled(cancel_)) {
            ++g_sntpAttempts;
            g_clockSynced = wtls_time_sync_sntp(nullptr, kSntpMs) == WTLS_OK;
            emit(log, g_clockSynced ? "NL_TLS_TIMESYNC ok" : "NL_TLS_TIMESYNC fail");
            if (g_clockSynced) st = connect(url, log);
        }
        if (st != WTLS_OK) {
            error_ = tlsError(st, wtls_last_verify_flags());
            return false;
        }
        wtls_set_nonblocking(conn_, true);
        return true;
    }

    s32 send(std::span<const u8> data) override {
        for (;;) {
            if (isCancelled(cancel_)) return fail(NetError::Other);
            const int r = wtls_write(conn_, data.data(), data.size());
            if (r > 0) return r;
            if (!wait(r)) return -1;
        }
    }

    s32 recv(std::span<u8> buf) override {
        for (;;) {
            if (isCancelled(cancel_)) return fail(NetError::Other);
            const int r = wtls_read(conn_, buf.data(), buf.size());
            if (r > 0) return r;
            if (r == WTLS_ERR_CLOSED) return 0;  // close_notify: sauberes Ende
            if (r == WTLS_WANT_READ && wtls_pending(conn_) > 0) continue;
            if (!wait(r)) return -1;  // auch WTLS_ERR_IO: abgeschnittener Strom gilt nicht als Ende
        }
    }

    void close() override {
        if (conn_ != nullptr) wtls_close(conn_);
        conn_ = nullptr;
    }

private:
    int connect(const Url& url, const HttpLog& log) {
        close();
        wtls_options opt{};
        opt.connect_timeout_ms = kTlsConnectMs;
        opt.handshake_timeout_ms = kTlsHandshakeMs;
        opt.io_timeout_ms = kTlsIoMs;
        // Spec v2 4.6: Heap, den die Verbindung nach dem Handshake hält (uordblks-Differenz; der Haupt-Thread kann
        // dazwischen allozieren, die Zahl ist ein Richtwert), und Dauer von DNS + TCP + Handshake.
        const int heapBefore = g_perf ? mallinfo().uordblks : 0;
        const u64 start = gettime();
        const int st = wtls_connect(&conn_, url.host.c_str(), url.port, &opt);
        if (g_perf && st == WTLS_OK) {
            char line[64];
            std::snprintf(line, sizeof line, "NL_PERF tls heap=%d ms=%u", mallinfo().uordblks - heapBefore,
                          static_cast<unsigned>(elapsedMs(start)));
            emit(log, line);
        }
        return st;
    }

    // WANT_READ/WANT_WRITE: auf den Socket warten; sonst Fehler übernehmen.
    bool wait(int r) {
        if (r != WTLS_WANT_READ && r != WTLS_WANT_WRITE) {
            error_ = tlsError(r, 0);
            return false;
        }
        const NetError w = waitFor(wtls_get_fd(conn_), r == WTLS_WANT_READ ? kPollIn : kPollOut, cancel_,
                                   static_cast<s32>(kTlsIoMs));
        if (w == NetError::None) return true;
        error_ = w;
        return false;
    }

    s32 fail(NetError e) {
        error_ = e;
        return -1;
    }

    const volatile bool* cancel_;
    wtls_conn* conn_ = nullptr;
};
#endif

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
    std::unique_ptr<Transport> transport;
    if (url.tls) {
#ifdef NL_TLS
        auto tls = std::make_unique<TlsTransport>(cancel);
        if (!tls->open(url, log)) return {HttpOutcome::Failed, tls->error(), 0};
        transport = std::move(tls);
#else
        static_cast<void>(cancel);
        emit(log, "NL_TLS_MISSING");
        return {HttpOutcome::Failed, NetError::TlsMissing, 0};
#endif
    } else {
        auto plain = std::make_unique<PlainTransport>(cancel);
        if (!plain->open(url)) return {HttpOutcome::Failed, plain->error(), 0};
        transport = std::move(plain);
    }
    if (!sendAll(*transport, buildGetRequest(url))) return {HttpOutcome::Failed, transport->error(), 0};

    u8 buf[4096];
    HttpParser::State state = HttpParser::State::Headers;
    while (state == HttpParser::State::Headers || state == HttpParser::State::Body) {
        // Auch bei durchgehend ankommenden Daten (kein EAGAIN, z. B. schnelles LAN) muss der Abbruch sofort greifen.
        if (isCancelled(cancel)) return {HttpOutcome::Failed, NetError::Other, 0};
        const s32 r = transport->recv(std::span<u8>(buf, sizeof buf));
        if (r > 0) {
            state = parser.feed(std::span<const u8>(buf, static_cast<std::size_t>(r)));
        } else if (r == 0) {
            state = parser.finish();
        } else {
            return {HttpOutcome::Failed, transport->error(), 0};
        }
    }
    const u16 status = static_cast<u16>(parser.status() > 0 ? parser.status() : 0);
    if (state != HttpParser::State::Done) return {HttpOutcome::Failed, NetError::Other, status};
    if (status == 404) return {HttpOutcome::NotFound, NetError::Http, status};
    if (status != 200) return {HttpOutcome::Failed, NetError::Http, status};
    return {HttpOutcome::Ok, NetError::None, status};
}

HttpResult request(const Url& url, HttpParser& parser, const volatile bool* cancel, const HttpLog& log) {
    const HttpResult r = exchange(url, parser, cancel, log);
    // Abbruch ist kein Netzfehler; TlsMissing hat seinen eigenen Marker.
    if (r.error != NetError::None && r.error != NetError::Http && r.error != NetError::TlsMissing &&
        !isCancelled(cancel))
        emit(log, std::string("NL_NET_FAIL ") + netErrorName(r.error));
    return r;
}

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
}  // namespace

void setTlsCaPath(std::string path) { g_tlsCaPath = std::move(path); }
void setNetPerf(bool on) { g_perf = on; }

HttpResult httpGet(const Url& url, u32 maxBody, std::vector<u8>& body, const volatile bool* cancel,
                   const HttpLog& log) {
    body.clear();
    HttpParser parser(maxBody);
    const HttpResult r = request(url, parser, cancel, log);
    if (r.outcome == HttpOutcome::Ok) body.assign(parser.body().begin(), parser.body().end());
    return r;
}

HttpResult httpDownload(const Url& url, const std::string& destFile, u32 maxBody, const volatile bool* cancel,
                        volatile u32* received, const HttpLog& log) {
    FileSink sink;
    sink.f = std::fopen(destFile.c_str(), "wb");
    if (sink.f == nullptr) return {HttpOutcome::Failed, NetError::Other, 0};
    sink.buf.reserve(FileSink::kBuf);
    HttpParser parser(maxBody);
    parser.setSink([&](std::span<const u8> data) {
        if (received != nullptr) *received = *received + static_cast<u32>(data.size());
        return sink.put(data);
    });
    HttpResult r = request(url, parser, cancel, log);
    // Ein Fehlerstatus (404 o. ä.) liefert eine Fehlerseite in die Senke; die Datei wird unten gelöscht.
    const bool flushed = sink.flush();
    if (std::fclose(sink.f) != 0 || !flushed) r.outcome = HttpOutcome::Failed;
    if (r.outcome != HttpOutcome::Ok) std::remove(destFile.c_str());
    return r;
}

}  // namespace nl::platform
