#pragma once
// Netzfehler für HTTP und HTTPS (M11, Spec v2 4.4): eine Ursache je Fehlschlag, die die UI in Klartext zeigt.
// core/ kennt wiitls.h nicht; die Statuscodes stehen hier als Zahlen und werden in platform/ per static_assert
// gegen die Header geprüft.

#include "core/types.hpp"

namespace nl {

enum class WifiStatus : u8 { Disconnected, Connecting, Connected };

enum class NetError : u8 { None, Dns, Connect, Timeout, Cert, Clock, Handshake, NoMem, TlsMissing, Http, Other };

// Statuscodes aus wiitls.h (WTLS_*), die netErrorFromTls auswertet.
namespace wtls_status {
inline constexpr int kOk = 0, kNoMem = -12, kClock = -21, kDns = -31, kConnect = -32, kTimeout = -33,
                     kHandshake = -40, kCert = -41, kHttp = -50, kHttpLimit = -51, kHttpRedirect = -52;
}  // namespace wtls_status

// MBEDTLS_X509_BADCERT_EXPIRED / _FUTURE (Mbed TLS 4.1.1).
inline constexpr u32 kX509BadCertExpired = 0x01;
inline constexpr u32 kX509BadCertFuture = 0x0200;

NetError netErrorFromTls(int status);
// Kurzer ASCII-Name für Log-Marker (NL_NET_FAIL <name>), z. B. "cert"; nie leer.
const char* netErrorName(NetError e);
// Liegt der Fehler vermutlich an der Wii-Uhr? WTLS_ERR_CLOCK, oder ein Zertifikatsfehler mit EXPIRED/FUTURE.
bool tlsClockSuspect(int status, u32 verifyFlags);

}  // namespace nl
