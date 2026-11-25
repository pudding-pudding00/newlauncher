#pragma once
// Fernsteuerung (M20): TCP-Server auf kRemotePort in einem eigenen Thread. Der Thread nimmt Verbindungen an,
// liest Befehlszeilen (und bei PUT die Daten samt CRC-Prüfung) und reicht jede Anfrage an den Haupt-Thread
// weiter; der führt sie aus und antwortet. So berührt der Thread weder Dateisystem noch Szenen.
// Netz: nur einfache Sockets (kein wtls_*), wartet, bis der NetService das Netz hochgefahren hat.

#include <string>
#include <vector>

#include <ogc/mutex.h>  // vor cond.h: cond.h nutzt mutex_t
#include <ogc/cond.h>
#include <ogc/lwp.h>

#include "core/remote_proto.hpp"
#include "core/types.hpp"

namespace nl::platform {

class RemoteServer {
public:
    RemoteServer();
    ~RemoteServer();
    RemoteServer(const RemoteServer&) = delete;
    RemoteServer& operator=(const RemoteServer&) = delete;

    bool start();  // Thread starten; false, wenn das nicht ging
    void stop();   // Verbindung und Port schließen, Thread beenden (vor NetService::shutdown aufrufen)
    bool running() const { return thread_ != LWP_THREAD_NULL; }

    // Haupt-Thread: nächste Anfrage (höchstens eine gleichzeitig). data enthält bei PUT die geprüften Bytes.
    bool take(RemoteRequest& out, std::vector<u8>& data);
    // Haupt-Thread: Antwort auf die zuletzt genommene Anfrage.
    void reply(bool ok, std::string info, std::vector<u8> payload = {});
    // Zahl vollständig gesendeter Antworten; vor einem Start oder Neustart darauf warten.
    u32 sentCount() const { return sent_; }
    // Vom Worker-Thread gesammelte Log-Zeilen; im Haupt-Thread über log::line ausgeben.
    std::vector<std::string> takeLogLines();

private:
    enum class State : u8 { Idle, Pending, Taken, Replied };

    static void* entry(void* self);
    void loop();
    void serve(s32 fd);
    bool recvLine(s32 fd, std::string& line);
    bool recvExact(s32 fd, u8* out, u32 size);
    bool sendAll(s32 fd, const u8* data, std::size_t size);
    bool waitReady(s32 fd, u32 events);  // false bei stop_ oder Fehler
    bool openListen();
    void closeListen();
    void log(std::string line);

    lwp_t thread_ = LWP_THREAD_NULL;
    mutable mutex_t mutex_ = LWP_MUTEX_NULL;
    cond_t cond_ = LWP_COND_NULL;
    volatile bool stop_ = false;
    s32 listen_ = -1;
    std::string pending_;  // Rest nach der letzten Zeile (z. B. Anfang der PUT-Daten)

    State state_ = State::Idle;
    RemoteRequest req_;
    std::vector<u8> data_;
    bool replyOk_ = false;
    std::string replyInfo_;
    std::vector<u8> replyPayload_;
    std::vector<std::string> logLines_;
    volatile u32 sent_ = 0;
};

}  // namespace nl::platform
