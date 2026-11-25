#pragma once
// Minimales HTTP/1.1 für Cover und Emulatoren (Spec 6.4, über TLS ab M11): GET-Anfrage bauen, Antwort schrittweise parsen.

#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/types.hpp"

namespace nl {

// Ergebnis eines GET (Plattform: platform::httpGet; Tests: Attrappe).
enum class HttpOutcome : u8 { Ok, NotFound, Failed };

struct Url {
    std::string host;
    u16 port = 80;
    std::string path;
    bool tls = false;  // https:// (M11)
};

// http:// (Port 80) und https:// (Port 443), Schema ohne Groß-/Kleinschreibung, Port 1..65535 optional.
std::optional<Url> parseUrl(std::string_view url);
std::string buildGetRequest(const Url& url);

// M20: 301/302/303/307/308.
bool isRedirectStatus(int status);
// Ziel einer Weiterleitung: absolute URL oder Pfad ab "/" auf demselben Server. Nie von HTTPS auf HTTP (Regel: kein
// Rückfall auf Klartext); protokoll-relative ("//…") und andere relative Angaben → nullopt.
std::optional<Url> redirectTarget(const Url& from, std::string_view location);

class HttpParser {
public:
    enum class State : u8 { Headers, Body, Done, Error };

    explicit HttpParser(u32 maxBody) : maxBody_(maxBody) {}

    State feed(std::span<const u8> data);
    State finish();  // Verbindung geschlossen
    int status() const { return status_; }
    std::span<const u8> body() const { return body_; }
    // Body-Bytes gehen an die Senke statt in body(); false von der Senke → Error. maxBody gilt weiter.
    void setSink(std::function<bool(std::span<const u8>)> sink) { sink_ = std::move(sink); }
    u32 received() const { return received_; }  // Body-Bytes bisher (mit und ohne Senke)
    State state() const { return state_; }
    const std::string& location() const { return location_; }  // Location-Kopf (Weiterleitung), sonst leer
    // Für den nächsten Sprung einer Weiterleitung: alles zurück außer maxBody und Senke.
    void reset();

private:
    enum class Chunk : u8 { Size, Data, DataEnd, Trailer };

    State fail() { return state_ = State::Error; }
    bool parseHead();
    State feedBody(const u8* p, std::size_t n);
    bool append(const u8* p, std::size_t n);

    u32 maxBody_;
    State state_ = State::Headers;
    std::string head_;
    std::string location_;
    int status_ = 0;
    bool chunked_ = false;
    std::optional<u32> length_;
    std::vector<u8> body_;
    std::function<bool(std::span<const u8>)> sink_;
    u32 received_ = 0;
    Chunk chunk_ = Chunk::Size;
    std::string line_;
    u32 chunkLeft_ = 0;
};

}  // namespace nl
