#include "platform/storage.hpp"

#include <fat.h>
#include <ogc/usbstorage.h>
#include <sdcard/wiisd_io.h>

namespace nl::platform {

namespace {
constexpr float kRetryMs = 500.0f;
bool g_sd = false, g_usb = false;
}  // namespace

bool mountSd() {
    if (!g_sd) g_sd = fatMountSimple("sd", &__io_wiisd);
    return g_sd;
}

void UsbMount::begin(int timeoutSec) {
    state_ = State::Waiting;
    elapsedMs_ = 0.0f;
    nextTryMs_ = 0.0f;
    timeoutMs_ = static_cast<float>(timeoutSec) * 1000.0f;
}

UsbMount::State UsbMount::poll(float dtMs) {
    if (state_ != State::Waiting) return state_;
    if (elapsedMs_ >= nextTryMs_) {
        nextTryMs_ += kRetryMs;
        if (g_usb || (__io_usbstorage.startup() && __io_usbstorage.isInserted() && fatMountSimple("usb", &__io_usbstorage))) {
            g_usb = true;
            state_ = State::Ready;
            return state_;
        }
    }
    elapsedMs_ += dtMs;
    if (elapsedMs_ >= timeoutMs_) state_ = State::TimedOut;
    return state_;
}

int UsbMount::secondsLeft() const {
    const float left = timeoutMs_ - elapsedMs_;
    return left <= 0.0f ? 0 : static_cast<int>((left + 999.0f) / 1000.0f);
}

void unmountAll() {
    if (g_usb) fatUnmount("usb");
    if (g_sd) fatUnmount("sd");
    g_usb = g_sd = false;
}

}  // namespace nl::platform
