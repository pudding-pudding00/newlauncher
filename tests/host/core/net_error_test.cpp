#include "core/net_error.hpp"

#include <doctest/doctest.h>

#include <cstring>
#include <set>
#include <string_view>

#include "ui/strings.hpp"
using namespace nl;

TEST_CASE("netErrorFromTls: Statuscodes aus wiitls.h") {
    CHECK(netErrorFromTls(0) == NetError::None);
    CHECK(netErrorFromTls(-31) == NetError::Dns);
    CHECK(netErrorFromTls(-32) == NetError::Connect);
    CHECK(netErrorFromTls(-33) == NetError::Timeout);
    CHECK(netErrorFromTls(-41) == NetError::Cert);
    CHECK(netErrorFromTls(-21) == NetError::Clock);
    CHECK(netErrorFromTls(-40) == NetError::Handshake);
    CHECK(netErrorFromTls(-12) == NetError::NoMem);
    for (int other : {-1, -2, -10, -11, -20, -30, -34, -35, -99, -1000, 5}) {
        CAPTURE(other);
        CHECK(netErrorFromTls(other) == NetError::Other);
    }
}

TEST_CASE("tlsClockSuspect: Uhr-Fehler oder Zertifikat abgelaufen/aus der Zukunft") {
    CHECK(tlsClockSuspect(-21, 0));
    CHECK(tlsClockSuspect(-41, 0x01));    // MBEDTLS_X509_BADCERT_EXPIRED
    CHECK(tlsClockSuspect(-41, 0x0200));  // MBEDTLS_X509_BADCERT_FUTURE
    CHECK(tlsClockSuspect(-41, 0x0201));
    CHECK_FALSE(tlsClockSuspect(-41, 0x08));  // NOT_TRUSTED
    CHECK_FALSE(tlsClockSuspect(-41, 0));
    CHECK_FALSE(tlsClockSuspect(-40, 0x01));  // Flags zählen nur bei CERT
    CHECK_FALSE(tlsClockSuspect(0, 0));
}

TEST_CASE("netErrorText: jeder NetError hat DE- und EN-Text") {
    constexpr NetError kAll[] = {NetError::None,    NetError::Dns,       NetError::Connect, NetError::Timeout,
                                 NetError::Cert,    NetError::Clock,     NetError::Handshake, NetError::NoMem,
                                 NetError::TlsMissing, NetError::Http,   NetError::Other};
    for (ui::Lang lang : {ui::Lang::De, ui::Lang::En}) {
        ui::setLanguage(lang);
        for (NetError e : kAll) {
            CAPTURE(static_cast<int>(e));
            CHECK(std::strlen(ui::tr(ui::netErrorText(e))) > 0);
        }
    }
    ui::setLanguage(ui::Lang::De);
    // Die Uhr-Meldung nennt die Ursache, damit niemand ein „Zertifikat ungültig“ für einen Angriff hält.
    CHECK(ui::netErrorText(NetError::Clock) != ui::netErrorText(NetError::Cert));
}

TEST_CASE("netErrorName: kurze Log-Namen für NL_NET_FAIL (M11 Task 7)") {
    CHECK(std::string_view(nl::netErrorName(nl::NetError::Cert)) == "cert");
    CHECK(std::string_view(nl::netErrorName(nl::NetError::Clock)) == "clock");
    CHECK(std::string_view(nl::netErrorName(nl::NetError::TlsMissing)) == "tls_missing");
    CHECK(std::string_view(nl::netErrorName(nl::NetError::Dns)) == "dns");
    std::set<std::string_view> seen;
    for (int i = 0; i <= static_cast<int>(nl::NetError::Other); ++i) {
        const std::string_view n = nl::netErrorName(static_cast<nl::NetError>(i));
        CHECK_FALSE(n.empty());
        for (char c : n) CHECK(((c >= 'a' && c <= 'z') || c == '_'));
        CHECK(seen.insert(n).second);  // eindeutig
    }
}
