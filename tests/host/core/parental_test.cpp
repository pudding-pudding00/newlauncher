#include "core/parental.hpp"

#include <doctest/doctest.h>

#include <array>
#include <string>
#include <vector>

#include "core/ini.hpp"

using namespace nl;

namespace {
constexpr std::array<u8, 16> kSalt = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};

ParentalConfig pegi12() {
    ParentalConfig c = withPin(ParentalConfig{}, "1234", kSalt);
    c.system = RatingSystem::Pegi;
    c.maxAge = 12;
    return c;
}

// SYSCONF mit IPL.LNG (BYTE) und IPL.PC (SMALLARRAY), wie im Spike-Dokument beschrieben.
std::vector<u8> sysconf(u8 enabled, u8 org, u8 age, const char* pin) {
    std::vector<u8> f(0x4000, 0);
    f[0] = 'S'; f[1] = 'C'; f[2] = 'v'; f[3] = '0';
    f[5] = 2;  // zwei Einträge
    std::size_t at = 0x0C;
    const auto put16 = [&](std::size_t pos, u16 v) { f[pos] = static_cast<u8>(v >> 8); f[pos + 1] = static_cast<u8>(v); };
    put16(0x06, static_cast<u16>(at));
    f[at++] = static_cast<u8>((3 << 5) | (7 - 1));
    for (char ch : std::string("IPL.LNG")) f[at++] = static_cast<u8>(ch);
    f[at++] = 1;
    put16(0x08, static_cast<u16>(at));
    f[at++] = static_cast<u8>((2 << 5) | (6 - 1));
    for (char ch : std::string("IPL.PC")) f[at++] = static_cast<u8>(ch);
    f[at++] = 0x49;
    const std::size_t payload = at;
    f[payload + 0] = enabled;
    f[payload + 1] = org;
    f[payload + 2] = age;
    for (int i = 0; i < 4; ++i) f[payload + 3 + static_cast<std::size_t>(i)] = static_cast<u8>(pin[i]);
    at += 0x4A;
    put16(0x0A, static_cast<u16>(at));
    f[0x3FFC] = 'S'; f[0x3FFD] = 'C'; f[0x3FFE] = 'e'; f[0x3FFF] = 'd';
    return f;
}
}  // namespace

TEST_CASE("Kindersicherung: withPin + checkPin; nur genau 4 Ziffern") {
    CHECK_FALSE(ParentalConfig{}.enabled);
    const ParentalConfig c = withPin(ParentalConfig{}, "1234", kSalt);
    CHECK(c.enabled);
    CHECK(bytesToHex(c.pinHash) == "bad0b4f7cae08eb7c1b5acc763a8ed25242e2c9173e81f3f624b1fa5303cf6ce");
    CHECK(checkPin(c, "1234"));
    CHECK_FALSE(checkPin(c, "1235"));
    CHECK_FALSE(checkPin(c, "12a4"));
    CHECK_FALSE(checkPin(c, "12345"));
    CHECK_FALSE(checkPin(c, ""));
    CHECK_FALSE(checkPin(ParentalConfig{}, "1234"));  // ohne PIN keine Prüfung möglich
    const ParentalConfig same = withPin(c, "12a4", kSalt);  // ungültige PIN ändert nichts
    CHECK(checkPin(same, "1234"));
}

TEST_CASE("isLocked: Alter in Jahren über alle Systeme, unbewertet nur ohne Bewertung, Sperre pro Spiel") {
    ParentalConfig c = pegi12();
    CHECK(isLocked(c, Rating{RatingSystem::Pegi, 16}, false));
    CHECK_FALSE(isLocked(c, Rating{RatingSystem::Pegi, 12}, false));
    CHECK(isLocked(c, Rating{RatingSystem::Usk, 18}, false));   // fremdes System zählt nach Jahren (Review M17)
    CHECK(isLocked(c, Rating{RatingSystem::Esrb, 17}, false));  // ESRB M bei PEGI 12
    CHECK_FALSE(isLocked(c, Rating{RatingSystem::Cero, 12}, false));
    CHECK_FALSE(isLocked(c, Rating{}, false));
    CHECK(isLocked(c, Rating{}, true));  // locked = 1
    c.lockUnrated = true;
    CHECK(isLocked(c, Rating{}, false));
    CHECK_FALSE(isLocked(c, Rating{RatingSystem::Usk, 0}, false));  // bewertet, ab 0
    ParentalConfig off = c;
    off.enabled = false;
    CHECK_FALSE(isLocked(off, Rating{RatingSystem::Pegi, 18}, true));  // ohne PIN keine Sperre
}

TEST_CASE("PinGate: drei Fehler sperren die Eingabe 30 s, danach frei") {
    PinGate g;
    CHECK_FALSE(g.blocked(0));
    CHECK_FALSE(g.submit(false, 100));
    CHECK_FALSE(g.submit(false, 200));
    CHECK_FALSE(g.blocked(250));
    CHECK_FALSE(g.submit(false, 300));
    CHECK(g.blocked(300));
    CHECK(g.blocked(30299));
    CHECK_FALSE(g.submit(true, 1000));  // gesperrt: auch die richtige PIN zählt nicht
    CHECK_FALSE(g.blocked(30300));
    CHECK(g.submit(true, 30300));
    CHECK_FALSE(g.submit(false, 30400));  // Zähler nach Erfolg zurückgesetzt
    CHECK_FALSE(g.blocked(30400));
}

// --- M17-Review: Freigabe nach dem Preflight-Hinweis gilt nur einmal und nur kurz ---
TEST_CASE("PinUnlock: gilt einmal, nur für dasselbe Spiel und nur innerhalb des Fensters") {
    PinUnlock u;
    CHECK_FALSE(u.consume("nes/a", 0));
    u.grant("nes/a", 1000);
    CHECK_FALSE(u.consume("nes/b", 1500));  // anderes Spiel: Freigabe verfällt
    CHECK_FALSE(u.consume("nes/a", 1600));
    u.grant("nes/a", 1000);
    CHECK(u.consume("nes/a", 1000 + PinUnlock::kWindowMs));
    CHECK_FALSE(u.consume("nes/a", 1000 + PinUnlock::kWindowMs));  // einmal
    u.grant("nes/a", 1000);
    CHECK_FALSE(u.consume("nes/a", 1001 + PinUnlock::kWindowMs));  // zu spät
    u.grant("nes/a", 1000);
    u.clear();
    CHECK_FALSE(u.consume("nes/a", 1000));
}

TEST_CASE("PinGate: blockFor sperrt für die Restzeit (Sperre über Neustart), 0 sperrt nicht") {
    PinGate g;
    g.blockFor(0, 100);
    CHECK_FALSE(g.blocked(100));
    g.blockFor(12000, 100);
    CHECK(g.blocked(100));
    CHECK(g.remainingMs(100) == 12000);
    CHECK_FALSE(g.submit(true, 5000));
    CHECK_FALSE(g.blocked(12100));
    CHECK(g.submit(true, 12100));
}

TEST_CASE("PinGate: Umlauf der Millisekunden-Uhr") {
    PinGate g;
    const u32 t = 0xFFFFF000u;
    for (int i = 0; i < 3; ++i) g.submit(false, t);
    CHECK(g.blocked(t + 1000));
    CHECK(g.blocked(t + 29999));
    CHECK_FALSE(g.blocked(t + 30000));
}

TEST_CASE("parentalFromIni/parentalIniValues: Rundreise; kaputter Hash → aus") {
    const ParentalConfig c = [] {
        ParentalConfig p = pegi12();
        p.lockUnrated = true;
        return p;
    }();
    std::string text;
    for (const auto& [key, value] : parentalIniValues(c)) text += key + " = " + value + "\n";
    const ParentalConfig r = parentalFromIni(IniDoc::parse("[parental]\n" + text));
    CHECK(r.enabled);
    CHECK(checkPin(r, "1234"));
    CHECK(r.system == RatingSystem::Pegi);
    CHECK(r.maxAge == 12);
    CHECK(r.lockUnrated);
    CHECK_FALSE(r.useWii);
    const ParentalConfig bad = parentalFromIni(IniDoc::parse("[parental]\npin_hash = zz\nsalt = 00\nmax_age = 300\nsystem = bbfc\n"));
    CHECK_FALSE(bad.enabled);
    CHECK(bad.maxAge == 18);
    CHECK(bad.system == RatingSystem::Pegi);
    CHECK(parentalFromIni(IniDoc::parse("[parental]\nsystem = usk\nmax_age = 6\nuse_wii = 1\n")).system == RatingSystem::Usk);
}

TEST_CASE("SYSCONF: IPL.PC liefert Schalter, System, Alter und PIN") {
    const std::vector<u8> f = sysconf(0x80, 4, 12, "4711");
    const auto item = sysconfItem(f, "IPL.PC");
    REQUIRE(item.has_value());
    CHECK(item->size() == 0x4A);
    const auto pc = parseIplPc(*item);
    REQUIRE(pc.has_value());
    CHECK(pc->enabled);
    CHECK(pc->system == RatingSystem::Pegi);
    CHECK(pc->maxAge == 12);
    CHECK(std::string(pc->pin.data(), 4) == "4711");
    CHECK_FALSE(sysconfItem(f, "IPL.P").has_value());
    CHECK(sysconfItem(f, "IPL.LNG").has_value());
}

TEST_CASE("SYSCONF: Alter 20 = frei, unbekannte Stelle, aus, kaputte Datei") {
    auto pc = parseIplPc(*sysconfItem(sysconf(0x80, 1, 20, "0000"), "IPL.PC"));
    REQUIRE(pc.has_value());
    CHECK(pc->system == RatingSystem::Esrb);
    CHECK(pc->maxAge == 255);
    CHECK(parseIplPc(*sysconfItem(sysconf(0x80, 8, 12, "0000"), "IPL.PC"))->system == RatingSystem::None);
    CHECK_FALSE(parseIplPc(*sysconfItem(sysconf(0x00, 4, 12, "0000"), "IPL.PC"))->enabled);
    CHECK_FALSE(parseIplPc(*sysconfItem(sysconf(0x80, 4, 12, "12a4"), "IPL.PC")).has_value());  // PIN keine Ziffern
    std::vector<u8> broken = sysconf(0x80, 4, 12, "1234");
    broken[0] = 'X';
    CHECK_FALSE(sysconfItem(broken, "IPL.PC").has_value());
    std::vector<u8> shortFile(16, 0);
    CHECK_FALSE(sysconfItem(shortFile, "IPL.PC").has_value());
    std::vector<u8> badOffset = sysconf(0x80, 4, 12, "1234");
    badOffset[0x08] = 0x3F;  // Offset von IPL.PC zeigt hinter die Daten
    badOffset[0x09] = 0xF0;
    CHECK_FALSE(sysconfItem(badOffset, "IPL.PC").has_value());
}

TEST_CASE("applyWiiParental: Wii-Werte ersetzen Stufe und PIN; unbekannte Stelle behält das System") {
    ParentalConfig c;
    c.system = RatingSystem::Usk;
    c.useWii = true;
    const WiiParental w{true, RatingSystem::None, 16, {'4', '7', '1', '1'}};
    const ParentalConfig r = applyWiiParental(c, w, kSalt);
    CHECK(r.enabled);
    CHECK(r.system == RatingSystem::Usk);
    CHECK(r.maxAge == 16);
    CHECK(checkPin(r, "4711"));
    const ParentalConfig off = applyWiiParental(withPin(c, "1234", kSalt), WiiParental{false, RatingSystem::Pegi, 12, {'1', '1', '1', '1'}}, kSalt);
    CHECK_FALSE(off.enabled);
}

TEST_CASE("Kindersicherungs-Zeilen: System und Altersgrenze zyklisch, Schalter, Aktionen") {
    ParentalConfig c = pegi12();
    CHECK(isParentalAction(ParentalRow::Pin));
    CHECK(isParentalAction(ParentalRow::RemovePin));
    CHECK_FALSE(isParentalAction(ParentalRow::MaxAge));
    stepParental(c, ParentalRow::System, 1);
    CHECK(c.system == RatingSystem::Esrb);
    stepParental(c, ParentalRow::System, -1);
    stepParental(c, ParentalRow::System, -1);
    CHECK(c.system == RatingSystem::Usk);
    stepParental(c, ParentalRow::System, -1);
    CHECK(c.system == RatingSystem::Cero);  // Umlauf
    stepParental(c, ParentalRow::MaxAge, 1);
    CHECK(c.maxAge == 13);
    stepParental(c, ParentalRow::MaxAge, -1);
    stepParental(c, ParentalRow::MaxAge, -1);
    CHECK(c.maxAge == 7);
    c.maxAge = 18;
    stepParental(c, ParentalRow::MaxAge, 1);
    CHECK(c.maxAge == 0);  // Umlauf
    c.maxAge = 9;          // Wert außerhalb der Stufen: nächste Stufe darüber
    stepParental(c, ParentalRow::MaxAge, 1);
    CHECK(c.maxAge == 12);
    stepParental(c, ParentalRow::LockUnrated, 1);
    CHECK(c.lockUnrated);
    stepParental(c, ParentalRow::UseWii, -1);
    CHECK(c.useWii);
    // Ohne PIN nichts zu entfernen; mit Wii-Jugendschutz gelten dessen PIN und Alter.
    CHECK_FALSE(parentalRowEnabled(ParentalConfig{}, ParentalRow::RemovePin));
    CHECK(parentalRowEnabled(pegi12(), ParentalRow::RemovePin));
    CHECK_FALSE(parentalRowEnabled(c, ParentalRow::Pin));
    CHECK_FALSE(parentalRowEnabled(c, ParentalRow::MaxAge));
    CHECK(parentalRowEnabled(c, ParentalRow::System));
    CHECK(parentalRowEnabled(c, ParentalRow::UseWii));
}

TEST_CASE("withoutPin schaltet die Kindersicherung aus, Einstellungen bleiben") {
    ParentalConfig c = pegi12();
    c.lockUnrated = true;
    const ParentalConfig off = withoutPin(c);
    CHECK_FALSE(off.enabled);
    CHECK_FALSE(checkPin(off, "1234"));
    CHECK(off.maxAge == 12);
    CHECK(off.lockUnrated);
}
