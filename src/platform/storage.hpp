#pragma once
// Mount von SD und USB über libfat (Spec 6.1). USB wird nicht blockierend alle 500 ms versucht,
// damit die Startszene „Warte auf USB …" weiter animiert.

#include "core/types.hpp"

namespace nl::platform {

bool mountSd();  // Gerät "sd:"

class UsbMount {
public:
    enum class State { Waiting, Ready, TimedOut };
    void begin(int timeoutSec);
    State poll(float dtMs);  // Gerät "usb:"
    int secondsLeft() const;
    u32 waitedMs() const { return static_cast<u32>(waitedMs_); }  // Perf: Wartezeit über alle Versuche


private:
    State state_ = State::Waiting;
    float elapsedMs_ = 0.0f, nextTryMs_ = 0.0f, timeoutMs_ = 0.0f, waitedMs_ = 0.0f;
};

void unmountAll();

}  // namespace nl::platform
