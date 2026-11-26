#include "core/token_store.hpp"

#include "core/aes128.hpp"
#include "core/hmac.hpp"
#include "core/ini.hpp"

namespace nl {

namespace {

constexpr u8 kVersion = 1;
constexpr std::size_t kHead = 4 + 1 + 16 + 16 + 4;  // Magic, Version, salt, iv, Länge
constexpr std::size_t kMac = 32;
constexpr std::size_t kMaxPlain = 16 * 1024;

struct Keys {
    AesBlock enc{};
    std::array<u8, 32> mac{};
};

std::span<const u8> bytes(std::string_view s) { return {reinterpret_cast<const u8*>(s.data()), s.size()}; }

Keys deriveKeys(std::span<const u8> deviceId, std::span<const u8> salt) {
    const auto k = hmacSha256(salt, deviceId);
    const auto enc = hmacSha256(k, bytes("enc"));
    Keys keys;
    for (std::size_t i = 0; i < 16; ++i) keys.enc[i] = enc[i];
    keys.mac = hmacSha256(k, bytes("mac"));
    return keys;
}

void putU32(std::vector<u8>& out, u32 v) {
    out.push_back(static_cast<u8>(v >> 24));
    out.push_back(static_cast<u8>(v >> 16));
    out.push_back(static_cast<u8>(v >> 8));
    out.push_back(static_cast<u8>(v));
}

}  // namespace

std::vector<u8> sealTokens(const StoredTokens& t, std::span<const u8> deviceId, std::span<const u8, 16> salt,
                           std::span<const u8, 16> iv) {
    IniDoc ini;
    ini.set("tokens", "provider", t.provider);
    ini.set("tokens", "account", t.account);
    ini.set("tokens", "access", t.tokens.access);
    ini.set("tokens", "refresh", t.tokens.refresh);
    const std::string text = ini.serialize();

    std::vector<u8> plain(text.begin(), text.end());
    const std::size_t pad = 16 - plain.size() % 16;  // PKCS#7: immer 1..16
    plain.insert(plain.end(), pad, static_cast<u8>(pad));

    const Keys keys = deriveKeys(deviceId, salt);
    AesBlock ivBlock{};
    for (std::size_t i = 0; i < 16; ++i) ivBlock[i] = iv[i];
    std::vector<u8> cipher(plain.size());
    aesCbcEncrypt(Aes128(keys.enc), ivBlock, plain, cipher);

    std::vector<u8> out = {'N', 'L', 'T', 'K', kVersion};
    out.insert(out.end(), salt.begin(), salt.end());
    out.insert(out.end(), iv.begin(), iv.end());
    putU32(out, static_cast<u32>(cipher.size()));
    out.insert(out.end(), cipher.begin(), cipher.end());
    const auto mac = hmacSha256(keys.mac, out);
    out.insert(out.end(), mac.begin(), mac.end());
    return out;
}

std::optional<StoredTokens> unsealTokens(std::span<const u8> blob, std::span<const u8> deviceId) {
    if (blob.size() < kHead + 16 + kMac) return std::nullopt;
    if (blob[0] != 'N' || blob[1] != 'L' || blob[2] != 'T' || blob[3] != 'K' || blob[4] != kVersion) {
        return std::nullopt;
    }
    const u32 len = (u32{blob[37]} << 24) | (u32{blob[38]} << 16) | (u32{blob[39]} << 8) | blob[40];
    if (len == 0 || len % 16 != 0 || len > kMaxPlain || blob.size() != kHead + len + kMac) return std::nullopt;

    const auto salt = blob.subspan(5, 16);
    const Keys keys = deriveKeys(deviceId, salt);
    const auto mac = hmacSha256(keys.mac, blob.first(kHead + len));
    if (!constantTimeEqual(mac, blob.subspan(kHead + len, kMac))) return std::nullopt;

    AesBlock iv{};
    for (std::size_t i = 0; i < 16; ++i) iv[i] = blob[21 + i];
    std::vector<u8> plain(len);
    if (!aesCbcDecrypt(Aes128(keys.enc), iv, blob.subspan(kHead, len), plain)) return std::nullopt;
    const u8 pad = plain.back();
    if (pad == 0 || pad > 16) return std::nullopt;
    for (std::size_t i = plain.size() - pad; i < plain.size(); ++i) {
        if (plain[i] != pad) return std::nullopt;
    }
    plain.resize(plain.size() - pad);

    const IniDoc ini = IniDoc::parse(std::string_view(reinterpret_cast<const char*>(plain.data()), plain.size()));
    StoredTokens t;
    t.provider = std::string(ini.get("tokens", "provider"));
    t.account = std::string(ini.get("tokens", "account"));
    t.tokens.access = std::string(ini.get("tokens", "access"));
    t.tokens.refresh = std::string(ini.get("tokens", "refresh"));
    if (t.provider.empty() || t.tokens.refresh.empty()) return std::nullopt;
    return t;
}

std::string redactSecret(std::string_view s) { return "<geheim:" + std::to_string(s.size()) + ">"; }

}  // namespace nl
