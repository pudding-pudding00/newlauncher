#pragma once
// OAuth 2.0 Gerätecode-Flow nach RFC 8628 und Token-Refresh (M27, Spec Ziel 6). Die Uhr kommt von außen (monotone
// ms, NetService::clockMs), nie von der RTC. Tokens und Codes erscheinen nie in Fehlertexten.

#include <string>
#include <string_view>

#include "core/http_types.hpp"
#include "core/result.hpp"
#include "core/types.hpp"

namespace nl {

struct OAuthEndpoints {
    std::string deviceUrl, tokenUrl;
    std::string clientId, clientSecret, scope;
};

inline constexpr std::string_view kDriveScope = "https://www.googleapis.com/auth/drive.file";

// base leer → https://oauth2.googleapis.com; deviceUrl = base + "/device/code", tokenUrl = base + "/token".
OAuthEndpoints googleEndpoints(std::string_view base, std::string_view clientId, std::string_view clientSecret);

struct DeviceCode {
    std::string deviceCode, userCode, verifyUrl;
    u32 expiresInS = 0, intervalS = 5;
};

struct TokenSet {
    std::string access, refresh;
    u64 expiresAtMs = 0;  // monotone Uhr; 0 = unbekannt (gilt als abgelaufen)
};

enum class PollState : u8 { Pending, SlowDown, Denied, Expired, Ok, Failed };

Result<DeviceCode> requestDeviceCode(HttpTransport& http, const OAuthEndpoints& ep, const volatile bool& cancel);
PollState pollToken(HttpTransport& http, const OAuthEndpoints& ep, std::string_view deviceCode, u64 nowMs,
                    TokenSet& out, const volatile bool& cancel);
// invalid_grant (widerrufen/abgelaufen) → Denied; ohne neues refresh_token in der Antwort bleibt das alte.
PollState refreshToken(HttpTransport& http, const OAuthEndpoints& ep, TokenSet& io, u64 nowMs,
                       const volatile bool& cancel);
bool tokenFresh(const TokenSet& t, u64 nowMs);  // mit 60 s Puffer

}  // namespace nl
