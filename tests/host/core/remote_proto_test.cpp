#include "core/remote_proto.hpp"
#include <doctest/doctest.h>
#include <string>
#include <vector>
using namespace nl;

TEST_CASE("parseRemoteLine: einfache Befehle ohne Argumente, Groß/klein egal, CR am Ende") {
    CHECK(parseRemoteLine("HELLO").value().cmd == RemoteCmd::Hello);
    CHECK(parseRemoteLine("log\r").value().cmd == RemoteCmd::Log);
    CHECK(parseRemoteLine("Shot").value().cmd == RemoteCmd::Shot);
    CHECK(parseRemoteLine("STATE").value().cmd == RemoteCmd::State);
    CHECK(parseRemoteLine("GAMES").value().cmd == RemoteCmd::Games);
    CHECK(parseRemoteLine("RESTART").value().cmd == RemoteCmd::Restart);
    CHECK(parseRemoteLine("EXIT").value().cmd == RemoteCmd::Exit);
    CHECK_FALSE(parseRemoteLine("").isOk());
    CHECK_FALSE(parseRemoteLine("FORMAT sd:").isOk());
}

TEST_CASE("parseRemoteLine: KEY mit Tasten und optionaler Dauer in Frames") {
    const auto k = parseRemoteLine("KEY a");
    REQUIRE(k.isOk());
    CHECK(k.value().cmd == RemoteCmd::Key);
    CHECK(k.value().buttons == RawButtons::A);
    CHECK(k.value().frames == kRemoteDefaultKeyFrames);

    const auto combo = parseRemoteLine("KEY one,two 30");
    REQUIRE(combo.isOk());
    CHECK(combo.value().buttons == (RawButtons::One | RawButtons::Two));
    CHECK(combo.value().frames == 30);

    CHECK_FALSE(parseRemoteLine("KEY").isOk());
    CHECK_FALSE(parseRemoteLine("KEY turbo").isOk());
    CHECK_FALSE(parseRemoteLine("KEY a 0").isOk());
    CHECK_FALSE(parseRemoteLine("KEY a 100000").isOk());  // höchstens kRemoteMaxKeyFrames
}

TEST_CASE("parseRemoteButtons: alle Namen der Wiimote und des GC-Pads") {
    CHECK(parseRemoteButtons("up,down,left,right").value() ==
          (RawButtons::DUp | RawButtons::DDown | RawButtons::DLeft | RawButtons::DRight));
    CHECK(parseRemoteButtons("minus,plus,home,b").value() ==
          (RawButtons::Minus | RawButtons::Plus | RawButtons::HomeBtn | RawButtons::B));
    CHECK(parseRemoteButtons("gc_a,gc_start,gc_z,gc_r").value() ==
          (RawButtons::GcA | RawButtons::GcStart | RawButtons::GcZ | RawButtons::GcR));
    CHECK_FALSE(parseRemoteButtons("").has_value());
    CHECK_FALSE(parseRemoteButtons("a,,b").has_value());
}

TEST_CASE("parseRemoteLine: LAUNCH nimmt den Rest der Zeile als Spielschlüssel (mit Leerzeichen)") {
    const auto l = parseRemoteLine("LAUNCH nes:Super Mario Bros. (Europe)");
    REQUIRE(l.isOk());
    CHECK(l.value().cmd == RemoteCmd::Launch);
    CHECK(l.value().text == "nes:Super Mario Bros. (Europe)");
    CHECK_FALSE(parseRemoteLine("LAUNCH").isOk());
}

TEST_CASE("parseRemoteLine: PUT <größe> <crc32-hex> <pfad>") {
    const auto p = parseRemoteLine("PUT 1024 deadbeef sd:/apps/newlauncher/boot.dol");
    REQUIRE(p.isOk());
    CHECK(p.value().cmd == RemoteCmd::Put);
    CHECK(p.value().size == 1024);
    CHECK(p.value().crc == 0xdeadbeefu);
    CHECK(p.value().text == "sd:/apps/newlauncher/boot.dol");
    CHECK_FALSE(parseRemoteLine("PUT 1024 deadbeef").isOk());
    CHECK_FALSE(parseRemoteLine("PUT x deadbeef sd:/a").isOk());
    CHECK_FALSE(parseRemoteLine("PUT 10 xyz sd:/a").isOk());
    CHECK_FALSE(parseRemoteLine("PUT 0 0 sd:/apps/newlauncher/a").isOk());
    CHECK_FALSE(parseRemoteLine("PUT 99999999 0 sd:/apps/newlauncher/a").isOk());  // über kRemoteMaxPut
}

TEST_CASE("remotePutAllowed: nur unter apps/newlauncher und dem Datenordner, ohne ..") {
    CHECK(remotePutAllowed("sd:/apps/newlauncher/boot.dol", "sd:/newlauncher"));
    CHECK(remotePutAllowed("sd:/apps/newlauncher/meta.xml", "sd:/newlauncher"));
    CHECK(remotePutAllowed("sd:/newlauncher/config.ini", "sd:/newlauncher"));
    CHECK(remotePutAllowed("usb:/newlauncher/themes/x.ini", "usb:/newlauncher"));
    CHECK_FALSE(remotePutAllowed("sd:/apps/newlauncher/", "sd:/newlauncher"));          // kein Dateiname
    CHECK_FALSE(remotePutAllowed("sd:/apps/newlauncherx/boot.dol", "sd:/newlauncher"));
    CHECK_FALSE(remotePutAllowed("sd:/apps/newlauncher/../fceugx/boot.dol", "sd:/newlauncher"));
    CHECK_FALSE(remotePutAllowed("sd:/newlauncher/../SYSCONF", "sd:/newlauncher"));
    CHECK_FALSE(remotePutAllowed("sd:/bootmii/armboot.bin", "sd:/newlauncher"));
    CHECK_FALSE(remotePutAllowed("sd:/newlauncher/a\\b", "sd:/newlauncher"));
    CHECK_FALSE(remotePutAllowed("sd:/newlauncher/x.ini", ""));                         // ohne Datenordner nur apps/
}

TEST_CASE("remoteReplyHeader: OK/ERR mit Länge und Info in einer Zeile") {
    CHECK(remoteReplyHeader(true, 0, "") == "OK 0\n");
    CHECK(remoteReplyHeader(true, 12, "640 480") == "OK 12 640 480\n");
    CHECK(remoteReplyHeader(false, 0, "nicht\nerlaubt") == "ERR 0 nicht erlaubt\n");
}

TEST_CASE("hasRemoteFlag: --remote in argv ab Index 1") {
    const char* withFlag[] = {"sd:/apps/newlauncher/boot.dol", "--remote"};
    const char* without[] = {"--remote"};  // argv[0] ist der Pfad, nie ein Schalter
    CHECK(hasRemoteFlag(2, withFlag));
    CHECK_FALSE(hasRemoteFlag(1, without));
    CHECK_FALSE(hasRemoteFlag(0, nullptr));
}
