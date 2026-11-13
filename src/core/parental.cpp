#include "core/parental.hpp"

#include <algorithm>
#include <charconv>

namespace nl {

namespace {
std::array<u8, 32> pinHash(std::span<const u8, 16> salt, std::string_view pin) {
    Sha256 h;
    h.update(salt);
    h.update(std::span<const u8>(reinterpret_cast<const u8*>(pin.data()), pin.size()));
    return h.finish();
}

u16 be16(std::span<const u8> f, std::size_t at) { return static_cast<u16>((f[at] << 8) | f[at + 1]); }
}  // namespace

bool validPin(std::string_view pin) {
    return pin.size() == 4 && std::all_of(pin.begin(), pin.end(), [](char c) { return c >= '0' && c <= '9'; });
}

bool checkPin(const ParentalConfig& c, std::string_view pin) {
    return c.enabled && validPin(pin) && pinHash(c.salt, pin) == c.pinHash;
}

ParentalConfig withPin(ParentalConfig c, std::string_view pin, std::span<const u8, 16> salt) {
    if (!validPin(pin)) return c;
    std::copy(salt.begin(), salt.end(), c.salt.begin());
    c.pinHash = pinHash(salt, pin);
    c.enabled = true;
    return c;
}

ParentalConfig withoutPin(ParentalConfig c) {
    c.pinHash = {};
    c.salt = {};
    c.enabled = false;
    return c;
}

bool isLocked(const ParentalConfig& c, const Rating& r, bool lockedFlag) {
    if (!c.enabled) return false;
    if (lockedFlag) return true;
    // Alter in Jahren (prepare_meta normiert ESRB/CERO), daher über alle Systeme vergleichbar: GameTDB hat z. B. keine
    // USK-Werte, ein deutsches Wii-Menü (USK) soll PEGI-16-Spiele trotzdem sperren.
    if (r.system != RatingSystem::None) return r.age > c.maxAge;
    return c.lockUnrated;
}

bool PinGate::blocked(u32 nowMs) const { return active_ && static_cast<s32>(until_ - nowMs) > 0; }

u32 PinGate::remainingMs(u32 nowMs) const { return blocked(nowMs) ? until_ - nowMs : 0; }

bool PinUnlock::consume(std::string_view key, u32 nowMs) {
    const bool ok = !key_.empty() && key == key_ && nowMs - at_ <= kWindowMs;
    key_.clear();
    return ok;
}

void PinGate::blockFor(u32 ms, u32 nowMs) {
    fails_ = 0;
    active_ = ms != 0;
    until_ = nowMs + ms;
}

bool PinGate::submit(bool ok, u32 nowMs) {
    if (blocked(nowMs)) return false;
    active_ = false;
    if (ok) {
        fails_ = 0;
        return true;
    }
    if (++fails_ >= 3) {
        fails_ = 0;
        active_ = true;
        until_ = nowMs + 30000u;
    }
    return false;
}

namespace {
constexpr std::array<u8, 9> kAgeSteps = {0, 3, 6, 7, 12, 13, 16, 17, 18};
constexpr std::array<RatingSystem, 4> kSystems = {RatingSystem::Usk, RatingSystem::Pegi, RatingSystem::Esrb,
                                                  RatingSystem::Cero};
}  // namespace

bool isParentalAction(ParentalRow r) { return r == ParentalRow::Pin || r == ParentalRow::RemovePin; }

void stepParental(ParentalConfig& c, ParentalRow r, int dir) {
    const int step = dir < 0 ? -1 : 1;
    switch (r) {
        case ParentalRow::System: {
            int i = 0;
            for (std::size_t k = 0; k < kSystems.size(); ++k)
                if (kSystems[k] == c.system) i = static_cast<int>(k);
            const int n = static_cast<int>(kSystems.size());
            c.system = kSystems[static_cast<std::size_t>((i + step + n) % n)];
            break;
        }
        case ParentalRow::MaxAge: {
            const int n = static_cast<int>(kAgeSteps.size());
            int i = -1;
            for (int k = 0; k < n; ++k)
                if (kAgeSteps[static_cast<std::size_t>(k)] == c.maxAge) i = k;
            if (i < 0) {  // kein Stufenwert (z. B. 255 vom Wii): nächste Stufe darüber bzw. darunter
                int above = n;
                for (int k = n - 1; k >= 0; --k)
                    if (kAgeSteps[static_cast<std::size_t>(k)] > c.maxAge) above = k;
                i = step > 0 ? above - 1 : above;
            }
            c.maxAge = kAgeSteps[static_cast<std::size_t>(((i + step) % n + n) % n)];
            break;
        }
        case ParentalRow::LockUnrated: c.lockUnrated = !c.lockUnrated; break;
        case ParentalRow::UseWii: c.useWii = !c.useWii; break;
        default: break;
    }
}

bool parentalRowEnabled(const ParentalConfig& c, ParentalRow r) {
    switch (r) {
        case ParentalRow::Pin:
        case ParentalRow::MaxAge: return !c.useWii;
        case ParentalRow::RemovePin: return !c.useWii && c.enabled;
        default: return true;
    }
}

const char* ratingSystemName(RatingSystem s) {
    switch (s) {
        case RatingSystem::Usk: return "usk";
        case RatingSystem::Pegi: return "pegi";
        case RatingSystem::Esrb: return "esrb";
        case RatingSystem::Cero: return "cero";
        case RatingSystem::None: break;
    }
    return "";
}

std::optional<RatingSystem> parseRatingSystem(std::string_view s) {
    s = trim(s);
    for (RatingSystem r : {RatingSystem::Usk, RatingSystem::Pegi, RatingSystem::Esrb, RatingSystem::Cero})
        if (s == ratingSystemName(r)) return r;
    return std::nullopt;
}

ParentalConfig parentalFromIni(const IniDoc& ini) {
    ParentalConfig c;
    const bool hashOk = hexToBytes(trim(ini.get("parental", "pin_hash")), c.pinHash);
    const bool saltOk = hexToBytes(trim(ini.get("parental", "salt")), c.salt);
    c.enabled = hashOk && saltOk;
    if (!c.enabled) c = withoutPin(c);
    c.system = parseRatingSystem(ini.get("parental", "system")).value_or(RatingSystem::Pegi);
    const std::string_view age = trim(ini.get("parental", "max_age"));
    unsigned v = 0;
    const auto [p, ec] = std::from_chars(age.data(), age.data() + age.size(), v);
    if (ec == std::errc{} && p == age.data() + age.size() && v <= 18) c.maxAge = static_cast<u8>(v);
    c.lockUnrated = trim(ini.get("parental", "lock_unrated")) == "1";
    c.useWii = trim(ini.get("parental", "use_wii")) == "1";
    const std::string_view lock = trim(ini.get("parental", "lock_until"));
    u32 until = 0;
    const auto [lp, lec] = std::from_chars(lock.data(), lock.data() + lock.size(), until);
    if (lec == std::errc{} && lp == lock.data() + lock.size()) c.lockUntil = until;
    return c;
}

std::vector<std::pair<std::string, std::string>> parentalIniValues(const ParentalConfig& c) {
    return {
        {"pin_hash", c.enabled ? bytesToHex(c.pinHash) : std::string()},
        {"salt", c.enabled ? bytesToHex(c.salt) : std::string()},
        {"system", ratingSystemName(c.system)},
        {"max_age", std::to_string(std::min<unsigned>(c.maxAge, 18))},
        {"lock_unrated", c.lockUnrated ? "1" : "0"},
        {"use_wii", c.useWii ? "1" : "0"},
        {"lock_until", c.lockUntil != 0 ? std::to_string(c.lockUntil) : std::string()},
    };
}

std::optional<std::span<const u8>> sysconfItem(std::span<const u8> file, std::string_view name) {
    if (file.size() < 8 || file[0] != 'S' || file[1] != 'C' || file[2] != 'v' || file[3] != '0') return std::nullopt;
    const std::size_t count = be16(file, 4);
    if (6 + 2 * count + 2 > file.size()) return std::nullopt;
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t off = be16(file, 6 + 2 * i);
        if (off == 0 || off >= file.size()) continue;
        const u8 head = file[off];
        const std::size_t nameLen = (head & 0x1Fu) + 1u;
        const std::size_t data = off + 1 + nameLen;
        if (data > file.size()) continue;
        const std::string_view itemName(reinterpret_cast<const char*>(file.data() + off + 1), nameLen);
        if (itemName != name) continue;
        std::size_t start = data, size = 0;
        switch (head >> 5) {
            case 1:  // BIGARRAY
                if (data + 2 > file.size()) return std::nullopt;
                size = be16(file, data) + 1u;
                start = data + 2;
                break;
            case 2:  // SMALLARRAY
                if (data + 1 > file.size()) return std::nullopt;
                size = file[data] + 1u;
                start = data + 1;
                break;
            case 3: size = 1; break;
            case 4: size = 2; break;
            case 5: size = 4; break;
            case 6: size = 8; break;
            case 7: size = 1; break;
            default: return std::nullopt;
        }
        if (start + size > file.size()) return std::nullopt;
        return file.subspan(start, size);
    }
    return std::nullopt;
}

std::optional<WiiParental> parseIplPc(std::span<const u8> payload) {
    if (payload.size() < 7) return std::nullopt;
    WiiParental w;
    for (std::size_t i = 0; i < 4; ++i) w.pin[i] = static_cast<char>(payload[3 + i]);
    if (!validPin(std::string_view(w.pin.data(), w.pin.size()))) return std::nullopt;
    w.enabled = (payload[0] & 0x80u) != 0;
    switch (payload[1]) {
        case 0: w.system = RatingSystem::Cero; break;
        case 1: w.system = RatingSystem::Esrb; break;
        case 3: w.system = RatingSystem::Usk; break;
        case 4:
        case 6:
        case 7: w.system = RatingSystem::Pegi; break;
        default: w.system = RatingSystem::None; break;
    }
    w.maxAge = payload[2] >= 20 ? 255 : payload[2];
    return w;
}

ParentalConfig applyWiiParental(ParentalConfig c, const WiiParental& w, std::span<const u8, 16> salt) {
    if (!w.enabled) return withoutPin(c);
    c = withPin(c, std::string_view(w.pin.data(), w.pin.size()), salt);
    c.maxAge = w.maxAge;
    if (w.system != RatingSystem::None) c.system = w.system;
    return c;
}

}  // namespace nl
