#pragma once
// Minimales HTTP/1.1 für den Cover-Download (Spec 6.4): GET-Anfrage bauen, Antwort schrittweise parsen.

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
};

std::optional<Url> parseUrl(std::string_view url);  // nur http://, Port optional
std::string buildGetRequest(const Url& url);

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

private:
    enum class Chunk : u8 { Size, Data, DataEnd, Trailer };

    State fail() { return state_ = State::Error; }
    bool parseHead();
    State feedBody(const u8* p, std::size_t n);
    bool append(const u8* p, std::size_t n);

    u32 maxBody_;
    State state_ = State::Headers;
    std::string head_;
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
