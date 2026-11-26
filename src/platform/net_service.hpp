#pragma once
// Ein Netz-Thread für alle Netzaufträge (M11, Spec v2 4.2): Cover, Emulator-Liste und -Installationen laufen
// nacheinander über eine Warteschlange. Der Thread macht net_init (und mit NL_TLS wtls_init); alle wtls_*-Aufrufe
// bleiben in diesem Thread. Bedienung nur vom Haupt-Thread.

#include <ogc/lwp.h>
#include <ogc/mutex.h>
// cond.h nutzt mutex_t, bindet mutex.h aber nicht selbst ein
#include <ogc/cond.h>

#include "core/job_queue.hpp"
#include "core/net_error.hpp"
#include "core/types.hpp"
#include "core/wifi_state.hpp"

namespace nl::platform {

class NetService {
public:
    NetService();
    ~NetService();
    NetService(const NetService&) = delete;
    NetService& operator=(const NetService&) = delete;

    // Reiht einen Auftrag ein und startet den Thread beim ersten Aufruf (128 KiB Stack). 0 = nicht angenommen
    // (nach shutdown oder wenn der Thread nicht startet).
    u32 submit(NetJobFn job);
    // M27: wie submit, aber vor alle wartenden Aufträge (Cloud-Abgleich vor dem Spielstart).
    u32 submitFront(NetJobFn job);
    // Startet bei Bedarf einen WLAN-Statuscheck im Netz-Thread ohne Heap-Allokation und ohne wtls_init().
    void startWifiProbe();
    // M25: Netz abbauen und neu aufbauen (Info-Fenster, A); läuft als nächster Statuscheck.
    void requestReconnect();
    // Statuscheck angefordert oder läuft gerade.
    bool probePending() const;
    // M25: Stand für Anzeige und Info-Fenster; Zeiten in clockMs().
    WifiSnapshot snapshot() const;
    static u64 clockMs();
    void cancel(u32 id);
    void cancelAndWait(u32 id);  // wartend → entfernt; laufend → abbrechen und auf sein Ende warten
    void shutdown();             // alles abbrechen, laufenden abwarten, wtls_shutdown (NL_TLS), Thread beendet
    bool busy() const;
    bool active(u32 id) const;   // wartet oder läuft

    // Nur im Netz-Thread aussagekräftig (aus einem Auftrag heraus): Stand nach dem Netzaufbau vor dem Auftrag.
    bool networkUp() const { return netUp_; }
    s32 lastNetCode() const { return netCode_; }
    bool tlsUp() const { return tlsUp_; }
    WifiStatus wifiStatus() const { return static_cast<WifiStatus>(wifi_); }

private:
    static void* entry(void* self);
    bool ensureThreadLocked();
    void loop();
    void probeWifi(WifiProbeKind kind);
    void prepare();  // vor jedem Auftrag: Netz aufbauen (3 Versuche), solange es nicht steht; dann wtls_init
    bool bringUp(WifiProbeKind kind);  // net_init mit Backoff, nach Fehlschlag net_deinit
    void tearDown();                   // net_deinit, Zustand zurücksetzen
    void checkGateway();               // Router-Antwort messen, Limited ableiten
    void publish(WifiStatus status);   // snap_ und wifi_ unter dem Mutex setzen

    lwp_t thread_ = LWP_THREAD_NULL;
    mutable mutex_t mutex_ = LWP_MUTEX_NULL;
    cond_t cond_ = LWP_COND_NULL;
    JobQueue queue_;
    bool stop_ = false;
    bool stopped_ = false;
    bool probeRequested_ = false;
    bool probing_ = false;
    bool reconnectRequested_ = false;
    bool initialProbeDone_ = false;
    u64 lastLimitedReinitMs_ = 0;
    WifiSnapshot snap_;  // nur unter mutex_; der Netz-Thread schreibt
    volatile u8 wifi_ = static_cast<u8>(WifiStatus::Connecting);
    volatile bool netUp_ = false;
    volatile s32 netCode_ = 0;
    volatile bool tlsUp_ = false;
    bool tlsTried_ = false;
};

}  // namespace nl::platform
