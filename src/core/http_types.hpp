#pragma once
// HTTP-Anfragen mit Methode, Headern und Body (M27, Cloud-Anbieter). core/ kennt nur diese Typen und die
// Schnittstelle HttpTransport; platform/ setzt sie über wii_tls um (WtlsTransport), Host-Tests mit FakeTransport.

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/net_error.hpp"
#include "core/types.hpp"

namespace nl {

enum class HttpMethod : u8 { Get, Post, Put, Patch, Delete };

const char* httpMethodName(HttpMethod m);  // "GET", "POST" …

using HttpHeaders = std::vector<std::pair<std::string, std::string>>;

struct HttpRequest {
    HttpMethod method = HttpMethod::Get;
    std::string url;
    HttpHeaders headers;
    std::vector<u8> body;
    u32 maxBody = 1024 * 1024;  // größere Antwort-Bodies → status 0, error Other
};

// status == 0: keine (vollständige) Antwort, error nennt die Ursache. Sonst jeder Status unverändert (auch 4xx/5xx).
struct HttpResponse {
    u16 status = 0;
    NetError error = NetError::Other;
    HttpHeaders headers;
    std::vector<u8> body;

    std::string_view header(std::string_view name) const;  // erster Treffer, ohne Groß/klein; leer, wenn fehlt
    std::string_view text() const { return {reinterpret_cast<const char*>(body.data()), body.size()}; }
};

class HttpTransport {
public:
    virtual ~HttpTransport() = default;
    virtual HttpResponse send(const HttpRequest& req, const volatile bool& cancel) = 0;
};

}  // namespace nl
