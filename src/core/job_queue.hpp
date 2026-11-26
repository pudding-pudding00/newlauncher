#pragma once
// Warteschlange der Netzaufträge (M11, Spec v2 4.2). Plattformfrei und ohne eigene Sperre: NetService
// schützt sie mit einem LWP-Mutex. Höchstens ein Auftrag läuft zur Zeit (take … finish).

#include <cstddef>
#include <deque>
#include <functional>
#include <optional>

#include "core/types.hpp"

namespace nl {

using NetJobFn = std::function<void(const volatile bool& cancel)>;

struct NetJob {
    u32 id = 0;
    NetJobFn run;
};

class JobQueue {
public:
    u32 push(NetJobFn run);         // neue ID, nie 0, streng steigend
    u32 pushFront(NetJobFn run);    // wie push, aber vor alle wartenden (Cloud-Abgleich vor dem Start)
    std::optional<NetJob> take();   // nächster wartender Auftrag (FIFO) wird der laufende; nullopt = leer
    void finish();                  // der laufende Auftrag ist fertig
    void cancel(u32 id);            // wartend → entfernt; laufend → Abbruch-Flag gesetzt
    void cancelAll();               // Warteschlange leeren, laufenden markieren
    bool cancelled(u32 id) const;   // id läuft und ist abgebrochen
    bool active(u32 id) const;      // id wartet oder läuft
    std::size_t pending() const { return queue_.size(); }
    u32 current() const { return current_; }  // 0 = keiner läuft
    // Das Flag, das der laufende Auftrag sieht. Der Netz-Thread liest es ohne Sperre (volatile, ein Byte).
    const volatile bool& cancelFlag() const { return cancel_; }

private:
    std::deque<NetJob> queue_;
    u32 nextId_ = 1;
    u32 current_ = 0;
    volatile bool cancel_ = false;
};

}  // namespace nl
