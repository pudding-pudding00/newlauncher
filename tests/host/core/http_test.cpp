#include "core/http.hpp"
#include <doctest/doctest.h>
#include <string>
using namespace nl;

namespace {
std::span<const u8> bytes(std::string_view s) { return {reinterpret_cast<const u8*>(s.data()), s.size()}; }
std::string text(std::span<const u8> b) { return {reinterpret_cast<const char*>(b.data()), b.size()}; }
}  // namespace

TEST_CASE("parseUrl") {
    auto u = parseUrl("http://art.gametdb.com/wii/cover");
    REQUIRE(u.has_value());
    CHECK(u->host == "art.gametdb.com");
    CHECK(u->port == 80);
    CHECK(u->path == "/wii/cover");
    u = parseUrl("http://127.0.0.1:8123/x");
    REQUIRE(u.has_value());
    CHECK(u->host == "127.0.0.1");
    CHECK(u->port == 8123);
    CHECK(u->path == "/x");
    u = parseUrl("http://host");
    REQUIRE(u.has_value());
    CHECK(u->path == "/");
    CHECK_FALSE(u->tls);
    CHECK_FALSE(parseUrl("http://").has_value());
    CHECK_FALSE(parseUrl("http://h:99999/").has_value());
    CHECK_FALSE(parseUrl("http://h:abc/").has_value());
}

TEST_CASE("parseUrl: https:// (M11, Spec v2 4.2)") {
    auto u = parseUrl("https://h/x");
    REQUIRE(u.has_value());
    CHECK(u->tls);
    CHECK(u->host == "h");
    CHECK(u->port == 443);
    CHECK(u->path == "/x");
    u = parseUrl("https://h:8443/x");
    REQUIRE(u.has_value());
    CHECK(u->tls);
    CHECK(u->port == 8443);
    u = parseUrl("HTTPS://H/x");  // Schema ohne Groß-/Kleinschreibung, Host unverändert
    REQUIRE(u.has_value());
    CHECK(u->tls);
    CHECK(u->host == "H");
    u = parseUrl("HTTP://h/");
    REQUIRE(u.has_value());
    CHECK_FALSE(u->tls);
    CHECK_FALSE(parseUrl("https://").has_value());
    CHECK_FALSE(parseUrl("https:/x").has_value());
    CHECK_FALSE(parseUrl("https://h:0/").has_value());
    CHECK_FALSE(parseUrl("https://h:70000/").has_value());
    CHECK_FALSE(parseUrl("ftp://h/").has_value());
}

TEST_CASE("buildGetRequest: Standardport je Schema ohne Angabe im Host") {
    CHECK(buildGetRequest(*parseUrl("https://h/x")).find("Host: h\r\n") != std::string::npos);
    CHECK(buildGetRequest(*parseUrl("https://h:8443/x")).find("Host: h:8443\r\n") != std::string::npos);
    CHECK(buildGetRequest(*parseUrl("https://h:80/x")).find("Host: h:80\r\n") != std::string::npos);
}

TEST_CASE("buildGetRequest") {
    const Url u{"art.gametdb.com", 80, "/wii/cover/EN/RMCP01.png"};
    CHECK(buildGetRequest(u) ==
          "GET /wii/cover/EN/RMCP01.png HTTP/1.1\r\nHost: art.gametdb.com\r\nUser-Agent: newlauncher\r\n"
          "Connection: close\r\n\r\n");
    CHECK(buildGetRequest({"h", 8123, "/"}).find("Host: h:8123\r\n") != std::string::npos);
}

TEST_CASE("HttpParser Content-Length, Kopf in Stücken") {
    const std::string resp = "HTTP/1.1 200 OK\r\nContent-Type: image/png\r\ncontent-length: 5\r\n\r\nhello";
    HttpParser p(1024);
    HttpParser::State s = HttpParser::State::Headers;
    for (char ch : resp) s = p.feed(bytes(std::string_view(&ch, 1)));
    CHECK(s == HttpParser::State::Done);
    CHECK(p.status() == 200);
    CHECK(text(p.body()) == "hello");
}

TEST_CASE("HttpParser chunked") {
    HttpParser p(1024);
    p.feed(bytes("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n4\r\nWi"));
    const auto s = p.feed(bytes("ki\r\n5\r\npedia\r\n0\r\n\r\n"));
    CHECK(s == HttpParser::State::Done);
    CHECK(text(p.body()) == "Wikipedia");
}

TEST_CASE("HttpParser ohne Länge endet mit finish") {
    HttpParser p(1024);
    CHECK(p.feed(bytes("HTTP/1.0 200 OK\r\n\r\nabc")) == HttpParser::State::Body);
    CHECK(p.feed(bytes("def")) == HttpParser::State::Body);
    CHECK(p.finish() == HttpParser::State::Done);
    CHECK(text(p.body()) == "abcdef");
}

TEST_CASE("HttpParser: abgeschnittener Body ist ein Fehler") {
    HttpParser p(1024);
    p.feed(bytes("HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\nabc"));
    CHECK(p.finish() == HttpParser::State::Error);
}

TEST_CASE("HttpParser 404 und 301") {
    HttpParser a(1024);
    CHECK(a.feed(bytes("HTTP/1.1 404 Not Found\r\nContent-Length: 3\r\n\r\nno!")) == HttpParser::State::Done);
    CHECK(a.status() == 404);
    HttpParser b(1024);
    b.feed(bytes("HTTP/1.1 301 Moved Permanently\r\nLocation: https://x/\r\nContent-Length: 0\r\n\r\n"));
    CHECK(b.status() == 301);
}

TEST_CASE("HttpParser Body über maxBody → Error") {
    HttpParser p(4);
    CHECK(p.feed(bytes("HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\n")) == HttpParser::State::Error);
    HttpParser q(4);
    q.feed(bytes("HTTP/1.1 200 OK\r\n\r\nabc"));
    CHECK(q.feed(bytes("de")) == HttpParser::State::Error);
    HttpParser r(4);
    CHECK(r.feed(bytes("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n9\r\n")) == HttpParser::State::Error);
}

TEST_CASE("HttpParser Müll → Error") {
    HttpParser p(1024);
    CHECK(p.feed(bytes("HELLO\r\n\r\n")) == HttpParser::State::Error);
    HttpParser q(1024);
    const std::string huge(9000, 'x');
    CHECK(q.feed(bytes("HTTP/1.1 200 OK\r\nX: " + huge)) == HttpParser::State::Error);  // Kopf > 8 KiB
    HttpParser r(1024);
    CHECK(r.feed(bytes("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nzz\r\n")) == HttpParser::State::Error);
}

TEST_CASE("HttpParser mit Senke: Content-Length geht an die Senke, body() bleibt leer") {
    HttpParser p(100);
    std::string sunk;
    p.setSink([&](std::span<const u8> b) { sunk += text(b); return true; });
    CHECK(p.feed(bytes("HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\nhello")) == HttpParser::State::Body);
    CHECK(p.feed(bytes("world")) == HttpParser::State::Done);
    CHECK(sunk == "helloworld");
    CHECK(p.body().empty());
    CHECK(p.received() == 10u);
}

TEST_CASE("HttpParser mit Senke: chunked") {
    HttpParser p(100);
    std::string sunk;
    p.setSink([&](std::span<const u8> b) { sunk += text(b); return true; });
    CHECK(p.feed(bytes("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n3\r\nabc\r\n0\r\n\r\n")) ==
          HttpParser::State::Done);
    CHECK(sunk == "helloabc");
    CHECK(p.body().empty());
}

TEST_CASE("HttpParser mit Senke: Senke meldet false → Error") {
    HttpParser p(100);
    p.setSink([](std::span<const u8>) { return false; });
    CHECK(p.feed(bytes("HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nabc")) == HttpParser::State::Error);
}

TEST_CASE("HttpParser mit Senke: maxBody gilt weiter") {
    HttpParser p(4);
    std::string sunk;
    p.setSink([&](std::span<const u8> b) { sunk += text(b); return true; });
    CHECK(p.feed(bytes("HTTP/1.1 200 OK\r\n\r\nabc")) == HttpParser::State::Body);
    CHECK(p.feed(bytes("de")) == HttpParser::State::Error);
    HttpParser q(4);
    q.setSink([](std::span<const u8>) { return true; });
    CHECK(q.feed(bytes("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n9\r\n")) == HttpParser::State::Error);
}
