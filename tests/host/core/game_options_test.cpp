#include "core/game_options.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <string>

using namespace nl;

namespace {

constexpr std::string_view kId = "RMGP01";

WiiOptions wiiGame(const std::string& body, const std::string& config = "") {
    return resolveWiiOptions(IniDoc::parse(config), IniDoc::parse("[game RMGP01]\n" + body), kId);
}

GcOptions gcGame(const std::string& body, NinVersion v = {6, 509}, const std::string& config = "") {
    return resolveGcOptions(IniDoc::parse(config), IniDoc::parse("[game GM8P01]\n" + body), "GM8P01", v);
}

bool has(const std::vector<std::string>& v, std::string_view s) { return std::find(v.begin(), v.end(), s) != v.end(); }

}  // namespace

// --- Standard ---------------------------------------------------------------------------------

TEST_CASE("game_options: Wii-Standard entspricht den bisherigen kNl-Werten (booter_cfg.h)") {
    const WiiOptions o = resolveWiiOptions(IniDoc{}, IniDoc{}, kId);
    CHECK(o.cfg.vidMode == 1);        // kNlVidModeSystem
    CHECK(o.cfg.patchVidMode == 0);
    CHECK(o.cfg.vipatch == 0);
    CHECK(o.cfg.language == 0xCD);    // kNlConfigByteUnpatched
    CHECK(o.cfg.countryString == 0);
    CHECK(o.cfg.patchregion == 0);
    CHECK(o.cfg.aspectRatio == -1);   // kNlAspectRatioGame
    CHECK(o.cfg.deflicker == 0);      // kNlDeflickerGame
    CHECK(o.cfg.videoWidth == 0);     // kNlVideoWidthGame
    CHECK(o.cfg.patchFix480p == 0);
    CHECK(o.cfg.privateServer == 0);
    CHECK(o.cfg.returnTo == 0);
    CHECK_FALSE(o.ios.has_value());
    CHECK_FALSE(o.returnToHbc);
    CHECK(o.bad.empty());
}

TEST_CASE("game_options: GC-Standard ist die M8-Eingabe") {
    const GcOptions o = resolveGcOptions(IniDoc{}, IniDoc{}, "GM8P01", {6, 509});
    CHECK(o.nin.extraFlags == 0u);
    CHECK(o.nin.videoMode == kNinVidAuto);
    CHECK(o.nin.language == kNinLangAuto);
    CHECK(o.nin.maxPads == 4u);
    CHECK(o.nin.memcardBlocks == 2u);
    CHECK(o.nin.memcardEmu);
    CHECK_FALSE(o.memcardReal);
    CHECK_FALSE(o.nativeSi);
    CHECK(o.unsupported.empty());
    CHECK(o.bad.empty());
}

// --- Wii-Tabelle (Spec 7.2), jede Zeile mit jedem Wert -----------------------------------------

TEST_CASE("game_options: Wii video") {
    const char* v[] = {"game", "system", "pal50", "pal60", "ntsc", "progressive"};
    for (u8 i = 0; i < 6; ++i) CHECK(wiiGame(std::string("video = ") + v[i]).cfg.vidMode == i);
}

TEST_CASE("game_options: Wii video_patch") {
    const char* v[] = {"off", "normal", "more", "all"};
    for (u8 i = 0; i < 4; ++i) CHECK(wiiGame(std::string("video_patch = ") + v[i]).cfg.patchVidMode == i);
}

TEST_CASE("game_options: Wii Schalter 0/1") {
    CHECK(wiiGame("vipatch = 1").cfg.vipatch == 1);
    CHECK(wiiGame("vipatch = 0").cfg.vipatch == 0);
    CHECK(wiiGame("country_patch = 1").cfg.countryString == 1);
    CHECK(wiiGame("region_patch = 1").cfg.patchregion == 1);
    CHECK(wiiGame("fix480p = 1").cfg.patchFix480p == 1);
}

TEST_CASE("game_options: Wii language") {
    CHECK(wiiGame("language = game").cfg.language == 0xCD);
    const char* v[] = {"ja", "en", "de", "fr", "es", "it", "nl", "zh_s", "zh_t", "ko"};
    for (u8 i = 0; i < 10; ++i) CHECK(wiiGame(std::string("language = ") + v[i]).cfg.language == i);
}

TEST_CASE("game_options: Wii aspect, deflicker, width, online") {
    CHECK(wiiGame("aspect = game").cfg.aspectRatio == -1);
    CHECK(wiiGame("aspect = 4:3").cfg.aspectRatio == 0);
    CHECK(wiiGame("aspect = 16:9").cfg.aspectRatio == 1);
    const char* d[] = {"game", "off", "off_ext", "low", "medium", "high"};
    for (u8 i = 0; i < 6; ++i) CHECK(wiiGame(std::string("deflicker = ") + d[i]).cfg.deflicker == i);
    CHECK(wiiGame("width = auto").cfg.videoWidth == 0);
    CHECK(wiiGame("width = framebuffer").cfg.videoWidth == 1);
    CHECK(wiiGame("online = off").cfg.privateServer == 0);
    CHECK(wiiGame("online = nossl").cfg.privateServer == 1);
    CHECK(wiiGame("online = wiimmfi").cfg.privateServer == 2);
}

TEST_CASE("game_options: Wii ios") {
    CHECK_FALSE(wiiGame("ios = auto").ios.has_value());
    CHECK(wiiGame("ios = 250").ios == std::optional<u8>{250});
    CHECK(wiiGame("ios = 200").ios == std::optional<u8>{200});
    const WiiOptions bad = wiiGame("ios = 199");
    CHECK_FALSE(bad.ios.has_value());
    CHECK(has(bad.bad, "ios"));
}

TEST_CASE("game_options: Wii return_to") {
    const WiiOptions hbc = wiiGame("return_to = hbc");
    CHECK(hbc.cfg.returnTo == 0x4C554C5Au);
    CHECK(hbc.returnToHbc);
    const WiiOptions ch = wiiGame("return_to = 0001000148415858");
    CHECK(ch.cfg.returnTo == 0x48415858u);
    CHECK_FALSE(ch.returnToHbc);
    CHECK(wiiGame("return_to = 0001000148415858").bad.empty());
    const WiiOptions sys = wiiGame("return_to = 0000000100000002");
    CHECK(sys.cfg.returnTo == 0u);
    CHECK(has(sys.bad, "return_to"));
    CHECK(has(wiiGame("return_to = 000100014841585").bad, "return_to"));    // 15 Ziffern
    CHECK(has(wiiGame("return_to = 00010001484158XY").bad, "return_to"));   // keine Hexziffern
    CHECK(wiiGame("return_to = none").cfg.returnTo == 0u);
}

// --- GC-Tabelle (Spec 7.3) --------------------------------------------------------------------

TEST_CASE("game_options: GC video") {
    CHECK(gcGame("video = auto").nin.videoMode == kNinVidAuto);
    CHECK(gcGame("video = pal50").nin.videoMode == (kNinVidForce | kNinVidPal50));
    CHECK(gcGame("video = pal60").nin.videoMode == (kNinVidForce | kNinVidPal60));
    CHECK(gcGame("video = ntsc").nin.videoMode == (kNinVidForce | kNinVidNtsc));
    CHECK(gcGame("video = mpal").nin.videoMode == (kNinVidForce | kNinVidMpal));
    CHECK(gcGame("video = none").nin.videoMode == kNinVidNone);
}

TEST_CASE("game_options: GC Schalter setzen ihre Bits") {
    CHECK(gcGame("progressive = 1").nin.extraFlags == kNinCfgForceProg);
    CHECK(gcGame("force_wide = 1").nin.extraFlags == kNinCfgForceWide);
    CHECK(gcGame("wiiu_wide = 1").nin.extraFlags == kNinCfgWiiuWide);
    CHECK(gcGame("skip_ipl = 1").nin.extraFlags == kNinCfgSkipIpl);
    const GcOptions si = gcGame("native_si = 1");
    CHECK(si.nin.extraFlags == kNinCfgNativeSi);
    CHECK(si.nativeSi);
    CHECK(gcGame("progressive = 0\nforce_wide = 0").nin.extraFlags == 0u);
}

TEST_CASE("game_options: GC memcard") {
    const GcOptions emu = gcGame("memcard = emu");
    CHECK(emu.nin.memcardEmu);
    CHECK(emu.nin.extraFlags == 0u);
    const GcOptions multi = gcGame("memcard = multi");
    CHECK(multi.nin.memcardEmu);
    CHECK(multi.nin.extraFlags == kNinCfgMcMulti);
    const GcOptions real = gcGame("memcard = real");
    CHECK_FALSE(real.nin.memcardEmu);
    CHECK(real.nin.extraFlags == 0u);
    CHECK(real.memcardReal);
}

TEST_CASE("game_options: GC memcard_blocks, language, max_pads") {
    const char* blocks[] = {"59", "123", "251", "507", "1019", "2043"};
    for (u32 i = 0; i < 6; ++i) CHECK(gcGame(std::string("memcard_blocks = ") + blocks[i]).nin.memcardBlocks == i);
    CHECK(gcGame("language = auto").nin.language == kNinLangAuto);
    const char* lang[] = {"en", "de", "fr", "es", "it", "nl"};
    for (u32 i = 0; i < 6; ++i) CHECK(gcGame(std::string("language = ") + lang[i]).nin.language == i);
    for (u32 n = 1; n <= 4; ++n) CHECK(gcGame("max_pads = " + std::to_string(n)).nin.maxPads == n);
    CHECK(has(gcGame("max_pads = 5").bad, "max_pads"));
    CHECK(has(gcGame("memcard_blocks = 250").bad, "memcard_blocks"));
}

TEST_CASE("game_options: buildNinCfg ohne MEMCARDEMU bei memcard = real") {
    NinConfigInput in = gcGame("memcard = real").nin;
    in.gamePath = "/g.iso";
    in.id6 = "GM8P01";
    auto r = buildNinCfg(in);
    REQUIRE(r.isOk());
    CHECK(r.value()[0x00B] == 0x80);  // nur AUTO_BOOT
}

// --- Vorrang und Robustheit (Spec 7.1, Review Focus 1) -----------------------------------------

TEST_CASE("game_options: Spiel schlägt global, global schlägt Standard") {
    const std::string cfg = "[wii]\nvideo = pal50\nlanguage = de\n";
    const WiiOptions o = wiiGame("video = ntsc\n", cfg);
    CHECK(o.cfg.vidMode == 4);
    CHECK(o.cfg.language == 2);
    const WiiOptions other = resolveWiiOptions(IniDoc::parse(cfg), IniDoc::parse("[game RMGP01]\nvideo = ntsc\n"), "SOUE01");
    CHECK(other.cfg.vidMode == 2);
    const GcOptions g = gcGame("", {6, 509}, "[gc]\nmax_pads = 2\n");
    CHECK(g.nin.maxPads == 2u);
}

TEST_CASE("game_options: default im Spiel nimmt den globalen Wert") {
    const WiiOptions o = wiiGame("video = default\n", "[wii]\nvideo = pal60\n");
    CHECK(o.cfg.vidMode == 3);
    CHECK(o.bad.empty());
}

TEST_CASE("game_options: Groß-/Kleinschreibung und Leerzeichen") {
    CHECK(wiiGame("Video = PAL60").cfg.vidMode == 3);
    CHECK(wiiGame("  ASPECT   =   16:9  ").cfg.aspectRatio == 1);
    const WiiOptions lower = resolveWiiOptions(IniDoc{}, IniDoc::parse("[Game rmgp01]\nvideo = ntsc\n"), kId);
    CHECK(lower.cfg.vidMode == 4);
    CHECK(wiiGame("return_to = 0001000148415a5a").cfg.returnTo == 0x48415A5Au);
}

TEST_CASE("game_options: Unsinn nimmt den Standard und meldet den Schlüssel") {
    const WiiOptions o = wiiGame("video = blau\n", "[wii]\nvideo = pal50\n");
    CHECK(o.cfg.vidMode == 1);  // Standard, nicht global (Spec 7.1)
    CHECK(has(o.bad, "video"));
    const GcOptions g = gcGame("", {6, 509}, "[gc]\nlanguage = klingonisch\n");
    CHECK(g.nin.language == kNinLangAuto);
    CHECK(has(g.bad, "language"));
}

TEST_CASE("game_options: doppelter Abschnitt, letzter Wert gewinnt") {
    const IniDoc games = IniDoc::parse("[game RMGP01]\nvideo = pal50\n[game SOUE01]\nvideo = ntsc\n[game RMGP01]\nvideo = pal60\n");
    CHECK(resolveWiiOptions(IniDoc{}, games, kId).cfg.vidMode == 3);
}

// --- Mindestversion (Spec 15, Review Focus 2) -------------------------------------------------

TEST_CASE("game_options: GC-Option unter der Mindestversion bleibt ungesetzt") {
    const GcOptions o = gcGame("skip_ipl = 1\nforce_wide = 1\n", {4, 433});
    CHECK(o.nin.extraFlags == kNinCfgForceWide);
    CHECK(has(o.unsupported, "skip_ipl"));
    CHECK_FALSE(has(o.unsupported, "force_wide"));
    CHECK(gcGame("skip_ipl = 0", {4, 433}).unsupported.empty());  // Standardwert ist nie „nicht unterstützt“
    CHECK(gcGame("skip_ipl = 1", {4, 434}).unsupported.empty());
}

// --- listOptions ------------------------------------------------------------------------------

TEST_CASE("game_options: listOptions markiert geerbte Werte") {
    const IniDoc cfg = IniDoc::parse("[wii]\nvideo = pal50\n");
    const IniDoc games = IniDoc::parse("[game RMGP01]\nlanguage = DE\nvideo = default\n");
    const auto rows = listOptions(Console::Wii, cfg, games, kId);
    REQUIRE(rows.size() == 13);
    CHECK(rows[0].key == "video");
    CHECK(rows[0].inherited);
    CHECK(rows[0].value == "pal50");
    CHECK(rows[0].inheritedValue == "pal50");
    auto lang = std::find_if(rows.begin(), rows.end(), [](const OptionValue& r) { return r.key == "language"; });
    REQUIRE(lang != rows.end());
    CHECK_FALSE(lang->inherited);
    CHECK(lang->value == "de");
    CHECK(lang->inheritedValue == "game");
    CHECK(rows.back().key == "return_to");
}

TEST_CASE("game_options: listOptions global und GC") {
    const auto global = listOptions(Console::Wii, IniDoc::parse("[wii]\naspect = 16:9\n"), IniDoc{}, "");
    REQUIRE(global.size() == 13);
    for (const OptionValue& r : global) CHECK_FALSE(r.inherited);
    const auto gc = listOptions(Console::Gc, IniDoc{}, IniDoc{}, "GM8P01");
    REQUIRE(gc.size() == 10);
    CHECK(gc[0].key == "video");
    CHECK(gc[0].value == "auto");
    CHECK(gc[0].inherited);
    CHECK(gc[5].key == "memcard_blocks");
    CHECK(gc[5].value == "251");
    CHECK(listOptions(Console::Nes, IniDoc{}, IniDoc{}, "").empty());
}

TEST_CASE("ini: removeSection entfernt nur den Abschnitt") {
    IniDoc d = IniDoc::parse("[n64_method]\nNSME = not64\n[game RMGP01]\nvideo = pal60\n[game SOUE01]\nvideo = ntsc\n");
    d.removeSection("game RMGP01");
    CHECK(d.get("game RMGP01", "video").empty());
    CHECK(d.get("game SOUE01", "video") == "ntsc");
    CHECK(d.get("n64_method", "NSME") == "not64");
    d.removeSection("gibt es nicht");
    CHECK(d.sectionNames().size() == 2);
}

TEST_CASE("game_options: optionValues für die Szene") {
    const auto v = optionValues(Console::Wii, "aspect");
    REQUIRE(v.size() == 3);
    CHECK(v[1] == "4:3");
    CHECK(optionValues(Console::Gc, "memcard_blocks").size() == 6);
    CHECK(optionValues(Console::Wii, "ios").size() == 1);
    CHECK(optionValues(Console::Gc, "gibt_es_nicht").empty());
    CHECK(optionValues(Console::Nes, "video").empty());
}

TEST_CASE("game_options: hbcReturnTo nimmt LULZ, sonst JODI, sonst nichts") {
    const std::vector<u64> both{0x000100014C554C5Aull, 0x000100014A4F4449ull};
    const std::vector<u64> old{0x0001000148415858ull, 0x000100014A4F4449ull};
    const std::vector<u64> none{0x0001000148415858ull};
    CHECK(hbcReturnTo(both) == kReturnToHbc);
    CHECK(hbcReturnTo(old) == kReturnToHbcOld);
    CHECK(hbcReturnTo(none) == 0u);
}
