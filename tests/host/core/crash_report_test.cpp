#include "core/crash_report.hpp"
#include <doctest/doctest.h>
#include <cstring>
#include <map>
#include <string>
using namespace nl;

namespace {
std::map<u32, u32> g_mem;

bool readFake(u32 addr, u32& out) {
    auto it = g_mem.find(addr);
    if (it == g_mem.end()) return false;
    out = it->second;
    return true;
}
}  // namespace

TEST_CASE("exceptionName kennt DSI, ISI, Alignment, Program, FPU; sonst Unbekannt") {
    CHECK(std::strcmp(exceptionName(3), "DSI") == 0);
    CHECK(std::strcmp(exceptionName(4), "ISI") == 0);
    CHECK(std::strcmp(exceptionName(6), "Alignment") == 0);
    CHECK(std::strcmp(exceptionName(7), "Program") == 0);
    CHECK(std::strcmp(exceptionName(8), "FPU") == 0);
    CHECK(std::strcmp(exceptionName(99), "Unbekannt") == 0);
}

TEST_CASE("backtrace folgt der Back-Chain") {
    g_mem = {{0x1000, 0x1100}, {0x1104, 0x80001111}, {0x1100, 0x1200}, {0x1204, 0x80002222},
             {0x1200, 0x1300}, {0x1304, 0x80003333}, {0x1300, 0}};
    u32 out[8];
    REQUIRE(backtrace(0x1000, &readFake, out) == 3);
    CHECK(out[0] == 0x80001111);
    CHECK(out[1] == 0x80002222);
    CHECK(out[2] == 0x80003333);
}

TEST_CASE("backtrace stoppt bei sp == 0, nicht lesbarer Adresse und vollem Ausgabepuffer") {
    u32 out[2];
    CHECK(backtrace(0, &readFake, out) == 0);
    g_mem = {{0x1000, 0x1100}};  // LR-Slot von 0x1100 fehlt
    CHECK(backtrace(0x1000, &readFake, out) == 0);
    g_mem = {{0x1000, 0x1100}, {0x1104, 1}, {0x1100, 0x1200}, {0x1204, 2}, {0x1200, 0x1300}, {0x1304, 3}};
    CHECK(backtrace(0x1000, &readFake, out) == 2);
    CHECK(backtrace(0x1002, &readFake, out) == 0);  // nicht ausgerichtet
}

TEST_CASE("backtrace erkennt eine Schleife und verlangt aufsteigende Stack-Adressen") {
    u32 out[16];
    g_mem = {{0x1000, 0x1000}, {0x1004, 1}};
    CHECK(backtrace(0x1000, &readFake, out) == 0);
    g_mem = {{0x1000, 0x1100}, {0x1104, 1}, {0x1100, 0x0F00}, {0x0F04, 2}};
    CHECK(backtrace(0x1000, &readFake, out) == 1);
}

TEST_CASE("formatCrashReport enthält Typ, PC, LR, DAR und alle Trace-Adressen") {
    CrashInfo info{};
    info.exceptionId = 3;
    info.pc = 0x80004000;
    info.lr = 0x80005000;
    info.dar = 0x00000004;
    info.gpr[31] = 0xDEADBEEF;
    const u32 trace[] = {0x80001111, 0x80002222};
    char buf[1024];
    const int n = formatCrashReport(info, trace, buf, sizeof buf);
    const std::string s(buf);
    CHECK(n == static_cast<int>(s.size()));
    CHECK(s.find("DSI") != std::string::npos);
    CHECK(s.find("80004000") != std::string::npos);
    CHECK(s.find("80005000") != std::string::npos);
    CHECK(s.find("00000004") != std::string::npos);
    CHECK(s.find("DEADBEEF") != std::string::npos);
    CHECK(s.find("80001111") != std::string::npos);
    CHECK(s.find("80002222") != std::string::npos);
}

TEST_CASE("formatCrashReport schneidet bei kleinem Puffer sauber ab") {
    CrashInfo info{};
    char buf[16];
    const int n = formatCrashReport(info, {}, buf, sizeof buf);
    CHECK(n == 15);
    CHECK(std::strlen(buf) == 15);
    char none[1] = {'x'};
    CHECK(formatCrashReport(info, {}, none, 0) == 0);
    CHECK(none[0] == 'x');
}
