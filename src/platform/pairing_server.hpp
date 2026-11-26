#pragma once
// Kopplungsseite (M29): HTTP-Server auf kPairPort in einem eigenen Thread, nur solange die Kopplungsszene offen ist.
// Ein Client zur Zeit, 5 s je Verbindung, Anfrage höchstens kPairMaxRequest Bytes; das Protokoll (Token, eine
// Einsendung, Ablauf) steckt in core/pairing_http. Der angenommene Code geht über takeCode() an den Haupt-Thread.
// Netz: nur einfache Sockets (kein wtls_*). Log: nur Marker NL_PAIR listen|ok|reject|stop, nie Token oder Code.

#include <optional>
#include <string>
#include <vector>

#include <ogc/lwp.h>
#include <ogc/mutex.h>

#include "core/pairing_http.hpp"
#include "core/types.hpp"

namespace nl::platform {

constexpr u16 kPairPort = 4301;

// "http://<ip>:4301/<token>" mit der aktuellen IP der Wii; leer ohne Netz (dann nur der direkte QR).
std::string pairingUrl(std::string_view token);

class PairingServer {
public:
    PairingServer();
    ~PairingServer();
    PairingServer(const PairingServer&) = delete;
    PairingServer& operator=(const PairingServer&) = delete;

    // Neue Sitzung und Thread; ein laufender Server wird vorher gestoppt.
    bool start(std::string token, std::string authorizeUrl, bool english);
    void stop();  // Port schließen, Thread beenden (vor NetService::shutdown aufrufen)
    bool running() const { return thread_ != LWP_THREAD_NULL; }

    std::optional<std::string> takeCode();  // Haupt-Thread: angenommener Code, genau einmal
    void rearm();                           // Haupt-Thread: Code war ungültig, wieder eine Einsendung annehmen
    std::vector<std::string> takeLogLines();

private:
    static void* entry(void* self);
    void loop();
    void serve(s32 fd);
    void log(std::string line);

    lwp_t thread_ = LWP_THREAD_NULL;
    mutable mutex_t mutex_ = LWP_MUTEX_NULL;
    volatile bool stop_ = false;
    s32 listen_ = -1;
    std::optional<PairingSession> session_;  // unter mutex_
    std::vector<std::string> logLines_;
};

}  // namespace nl::platform
