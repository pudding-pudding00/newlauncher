#pragma once
// Kopplungsseite (M29): eine winzige HTTP-Seite auf der Wii (Port 4301, Pfad "/<token>"), die das Handy über den
// QR-Code öffnet. Sie verlinkt die Dropbox-Freigabe und nimmt den angezeigten Code per Formular entgegen. Hier liegt
// nur das Protokoll (host-testbar); den Socket-Thread stellt platform/pairing_server.
//
// Regeln: genau eine angenommene Einsendung (danach 410 bis rearm()), Ablauf nach 5 Minuten, falsches Token → 404
// (Vergleich in konstanter Zeit), Anfrage über 4096 Bytes → 413. Code und Token stehen nie in einer Antwort für einen
// Fremden und nie im Log (die Antwort trägt nur den Marker).

#include <optional>
#include <string>
#include <string_view>

#include "core/types.hpp"

namespace nl {

constexpr std::size_t kPairMaxRequest = 4096;
constexpr u64 kPairTimeoutMs = 5ull * 60 * 1000;
constexpr std::size_t kPairMaxCode = 128;

struct HttpRequestHead {
    std::string method;
    std::string path;
    std::size_t contentLength = 0;
    std::size_t headerBytes = 0;  // bis einschließlich der Leerzeile
};

// nullopt: Kopf unvollständig (keine Leerzeile) oder kaputt (Anfragezeile, Content-Length).
std::optional<HttpRequestHead> parseHttpHead(std::string_view raw);

struct PairingReply {
    int status = 400;
    std::string body;     // HTML
    bool log = false;     // Marker ins Log schreiben?
    std::string marker;   // "ok" | "reject" | ""
};

// Ganze HTTP-Antwort (Statuszeile, Header, Body) zum Senden.
std::string buildPairingResponse(const PairingReply& reply);

class PairingSession {
public:
    PairingSession(std::string token, std::string authorizeUrl, u64 nowMs, bool english);

    // request = alles, was bisher über die Verbindung kam (Kopf + Body).
    PairingReply handle(std::string_view request, u64 nowMs);
    // Der angenommene Code, genau einmal.
    std::optional<std::string> takeCode();
    // Nach einem von Dropbox abgelehnten Code wieder eine Einsendung annehmen (gleiche Seite, gleicher Verifier).
    void rearm();
    bool expired(u64 nowMs) const;

private:
    PairingReply page(int status, std::string_view message) const;

    std::string token_, authorizeUrl_;
    u64 startMs_;
    bool english_;
    bool consumed_ = false;
    std::optional<std::string> code_;
};

}  // namespace nl
