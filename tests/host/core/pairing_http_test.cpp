#include "core/pairing_http.hpp"

#include <doctest/doctest.h>

#include <string>
#include <vector>

using namespace nl;

namespace {

const std::string kToken = "0123456789abcdef0123456789abcdef";
const std::string kAuth = "https://www.dropbox.com/oauth2/authorize?client_id=k&response_type=code";

std::string get(const std::string& path) { return "GET " + path + " HTTP/1.1\r\nHost: wii\r\n\r\n"; }

std::string post(const std::string& path, const std::string& body) {
    return "POST " + path + " HTTP/1.1\r\nHost: wii\r\nContent-Type: application/x-www-form-urlencoded\r\n" +
           "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
}

constexpr u64 kStart = 1'000'000;

}  // namespace

TEST_CASE("parseHttpHead") {
    const auto h = parseHttpHead("POST /abc HTTP/1.1\r\nHost: x\r\ncontent-length: 12\r\n\r\nbody");
    REQUIRE(h.has_value());
    CHECK(h->method == "POST");
    CHECK(h->path == "/abc");
    CHECK(h->contentLength == 12);
    CHECK(h->headerBytes == std::string("POST /abc HTTP/1.1\r\nHost: x\r\ncontent-length: 12\r\n\r\n").size());
    CHECK_FALSE(parseHttpHead("GET /abc HTTP/1.1\r\nHost: x\r\n").has_value());  // unvollständig
    CHECK_FALSE(parseHttpHead("GARBAGE\r\n\r\n").has_value());
    CHECK_FALSE(parseHttpHead("GET /a HTTP/1.1\r\nContent-Length: zwölf\r\n\r\n").has_value());
    CHECK_FALSE(parseHttpHead("GET /a HTTP/1.1\r\nContent-Length: 99999999999999999999\r\n\r\n").has_value());
}

TEST_CASE("PairingSession: GET zeigt die Seite mit maskierter Autorisierungs-URL") {
    PairingSession s(kToken, kAuth, kStart, false);
    const PairingReply r = s.handle(get("/" + kToken), kStart + 1000);
    CHECK(r.status == 200);
    CHECK(r.body.find("Bei Dropbox zulassen") != std::string::npos);
    CHECK(r.body.find("client_id=k&amp;response_type=code") != std::string::npos);
    CHECK(r.body.find("client_id=k&response_type") == std::string::npos);
    CHECK(r.body.find("action=\"/" + kToken + "\"") != std::string::npos);
    CHECK(r.body.find("name=\"code\"") != std::string::npos);
    CHECK_FALSE(r.log);

    PairingSession en(kToken, kAuth, kStart, true);
    CHECK(en.handle(get("/" + kToken), kStart).body.find("Allow on Dropbox") != std::string::npos);
}

TEST_CASE("PairingSession: POST mit Code wird genau einmal angenommen") {
    PairingSession s(kToken, kAuth, kStart, false);
    CHECK_FALSE(s.takeCode().has_value());
    const PairingReply r = s.handle(post("/" + kToken, "code=Ab3_x-Z9"), kStart + 5000);
    CHECK(r.status == 200);
    CHECK(r.log);
    CHECK(r.marker == "ok");
    CHECK(r.body.find("Code übermittelt") != std::string::npos);
    CHECK(r.body.find("Ab3_x-Z9") == std::string::npos);  // Code steht nie in der Antwort
    const auto c = s.takeCode();
    REQUIRE(c.has_value());
    CHECK(*c == "Ab3_x-Z9");
    CHECK_FALSE(s.takeCode().has_value());  // nur einmal

    const PairingReply again = s.handle(post("/" + kToken, "code=Other123"), kStart + 6000);
    CHECK(again.status == 410);
    CHECK(again.marker == "reject");
    CHECK_FALSE(s.takeCode().has_value());
    CHECK(s.handle(get("/" + kToken), kStart + 6000).status == 410);

    s.rearm();
    CHECK(s.handle(post("/" + kToken, "code=Third456"), kStart + 7000).status == 200);
    CHECK(s.takeCode() == std::optional<std::string>("Third456"));
}

TEST_CASE("PairingSession: Code wird form-dekodiert und getrimmt, Feldreihenfolge egal") {
    PairingSession s(kToken, kAuth, kStart, false);
    CHECK(s.handle(post("/" + kToken, "x=1&code=+abc%2DDEF_9++"), kStart).status == 200);
    CHECK(s.takeCode() == std::optional<std::string>("abc-DEF_9"));
}

TEST_CASE("PairingSession: ungültige Codes → 400, kein Code") {
    for (const std::string& body : std::vector<std::string>{"code=", "code=ab%20cd", "code=a.b", "nocode=abc", "code=%ZZ",
                                   "code=" + std::string(129, 'a')}) {
        CAPTURE(body);
        PairingSession s(kToken, kAuth, kStart, false);
        CHECK(s.handle(post("/" + kToken, body), kStart).status == 400);
        CHECK_FALSE(s.takeCode().has_value());
        CHECK(s.handle(post("/" + kToken, "code=ok"), kStart).status == 200);  // weiterhin offen
    }
    PairingSession s(kToken, kAuth, kStart, false);
    CHECK(s.handle(post("/" + kToken, "code=" + std::string(128, 'a')), kStart).status == 200);
}

TEST_CASE("PairingSession: falsches Token → 404") {
    PairingSession s(kToken, kAuth, kStart, false);
    std::string wrong = kToken;
    wrong[31] = '0';
    for (const std::string& path : std::vector<std::string>{"/" + wrong, "/" + kToken.substr(0, 31), "/" + kToken + "0", "/", "/favicon.ico",
                                   "/" + kToken + "?x=1"}) {
        CAPTURE(path);
        const PairingReply r = s.handle(post(path, "code=abc"), kStart);
        CHECK(r.status == 404);
        CHECK(r.marker == "reject");
        CHECK(r.body.find(kToken) == std::string::npos);
    }
    CHECK_FALSE(s.takeCode().has_value());
}

TEST_CASE("PairingSession: Ablauf nach 5 Minuten → 410") {
    PairingSession s(kToken, kAuth, kStart, false);
    CHECK_FALSE(s.expired(kStart + kPairTimeoutMs - 1));
    CHECK(s.expired(kStart + kPairTimeoutMs));
    const PairingReply r = s.handle(post("/" + kToken, "code=abc"), kStart + kPairTimeoutMs);
    CHECK(r.status == 410);
    CHECK(r.marker == "reject");
    CHECK_FALSE(s.takeCode().has_value());
}

TEST_CASE("PairingSession: Größe, Methoden, Müll") {
    PairingSession s(kToken, kAuth, kStart, false);
    const PairingReply big = s.handle(post("/" + kToken, "code=" + std::string(kPairMaxRequest, 'a')), kStart);
    CHECK(big.status == 413);
    CHECK(big.marker == "reject");
    CHECK(s.handle("PUT /" + kToken + " HTTP/1.1\r\n\r\n", kStart).status == 405);
    CHECK(s.handle("kaputt", kStart).status == 400);
    // Content-Length größer als der mitgeschickte Body → 400
    CHECK(s.handle("POST /" + kToken + " HTTP/1.1\r\nContent-Length: 50\r\n\r\ncode=abc", kStart).status == 400);
    // Deklariertes Content-Length über kPairMaxRequest → sofort 413 + reject, auch bei kurzem Body
    const PairingReply declaredBig =
        s.handle("POST /" + kToken + " HTTP/1.1\r\nContent-Length: 5000\r\n\r\n", kStart);
    CHECK(declaredBig.status == 413);
    CHECK(declaredBig.marker == "reject");
    // CR/LF um den Code wird mitgetrimmt
    CHECK(s.handle(post("/" + kToken, "code=abc-123%0D%0A"), kStart).status == 200);
    CHECK(s.takeCode() == std::optional<std::string>("abc-123"));
}

TEST_CASE("buildPairingResponse") {
    const std::string r = buildPairingResponse(PairingReply{410, "<p>x</p>", true, "reject"});
    CHECK(r.rfind("HTTP/1.1 410 Gone\r\n", 0) == 0);
    CHECK(r.find("Content-Type: text/html; charset=utf-8\r\n") != std::string::npos);
    CHECK(r.find("Content-Length: 8\r\n") != std::string::npos);
    CHECK(r.find("Connection: close\r\n") != std::string::npos);
    CHECK(r.find("Cache-Control: no-store\r\n") != std::string::npos);
    CHECK(r.substr(r.size() - 12) == "\r\n\r\n<p>x</p>");
}
