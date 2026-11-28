#include <doctest/doctest.h>

#include "core/ftp_proto.hpp"

using namespace nl;

TEST_CASE("parseFtpCommand parses case-insensitive verbs and arguments") {
    auto c1 = parseFtpCommand("user wii\r\n");
    REQUIRE(c1.has_value());
    CHECK(c1->verb == "USER");
    CHECK(c1->arg == "wii");

    auto c2 = parseFtpCommand("STOR my game.iso\n");
    REQUIRE(c2.has_value());
    CHECK(c2->verb == "STOR");
    CHECK(c2->arg == "my game.iso");

    CHECK_FALSE(parseFtpCommand("   \r\n").has_value());
    CHECK_FALSE(parseFtpCommand("123 arg").has_value());
}

TEST_CASE("FtpSession enforces auth lockout, PWD escaping, and one-shot REST") {
    FtpSession s;
    s.setCredentials("wii", "246810");
    s.setPendingUser("wii");

    for (int i = 0; i < kFtpMaxAuthFailures - 1; ++i) {
        CHECK(s.authenticate("wrong", 1000) == FtpAuthResult::BadCredentials);
    }
    CHECK(s.authenticate("wrong", 1000) == FtpAuthResult::LockedOut);
    CHECK(s.isLockedOut(2000));
    CHECK(s.authenticate("246810", 2000) == FtpAuthResult::LockedOut);
    CHECK(s.authenticate("246810", 1000 + kFtpLockoutMs) == FtpAuthResult::Ok);
    CHECK(s.loggedIn());

    s.setCwd("/sd/a\"b");
    CHECK(s.formatPwdReply() == "257 \"/sd/a\"\"b\" is current directory.");

    s.setRest(4096);
    CHECK(s.consumeRest() == 4096);
    CHECK(s.consumeRest() == 0);
}

TEST_CASE("FTP formatting helpers format PASV, LIST, MLSD, and URI") {
    const u32 ip = (192u << 24) | (168u << 16) | (1u << 8) | 50u;
    CHECK(formatPasvReply(ip, 50000) == "227 Entering Passive Mode (192,168,1,50,195,80).");
    CHECK(formatFtpUri("wii", "123456", ip, 21) == "ftp://wii:123456@192.168.1.50/");
    CHECK(formatFtpUri("wii", "123456", ip, 2121) == "ftp://wii:123456@192.168.1.50:2121/");
    CHECK(formatUnixListLine("sd", true, 0).starts_with("drwxr-xr-x"));
    CHECK(formatMlsdLine("boot.dol", false, 1024) == "type=file;size=1024; boot.dol\r\n");
}
