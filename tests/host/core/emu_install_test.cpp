#include "core/emu_install.hpp"
#include <doctest/doctest.h>

#include "core/crc32.hpp"
#include "core/emu_catalog.hpp"
#include "support/mem_fs.hpp"
#include "support/zip_builder.hpp"

using namespace nl;
using nl::test::MemFileSystem;

namespace {

std::vector<u8> bytes(std::string_view s) { return std::vector<u8>(s.begin(), s.end()); }
std::string text(const MemFileSystem::File* f) { return f ? std::string(f->data.begin(), f->data.end()) : "<fehlt>"; }

std::vector<u8> fceuZip() {
    return test::buildZip({
        test::dirEntry("apps/"),
        test::dirEntry("apps/fceugx/"),
        test::storedEntry("apps/fceugx/boot.dol", "NEUES-DOL"),
        test::storedEntry("apps/fceugx/meta.xml", "<app>neu</app>"),
        test::storedEntry("fceugx/fceugx.cfg", "standard-cfg"),
    });
}

OscApp appFor(const std::vector<u8>& zip, u32 unzipped = 64) {
    OscApp a;
    a.name = "fceugx";
    a.timestamp = 1793664000u;
    a.unzippedSize = unzipped;
    a.zipSize = static_cast<u32>(zip.size());
    a.displayName = "FCE Ultra GX";
    a.version = "4.0.4";
    return a;
}

struct Harness {
    MemFileSystem fs;
    std::vector<u8> zip = fceuZip();
    int downloads = 0;
    bool cancelAfterDownload = false;
    bool downloadFails = false;
    u64 free = 1ull << 30;
    std::vector<std::string> log;
    std::string lastUrl;
    EmuInstallIo io;

    Harness() {
        fs.addDir("sd:/newlauncher");
        io.download = [this](const std::string& url, const std::string& dest, u32 maxBytes) {
            ++downloads;
            lastUrl = url;
            if (downloadFails) return HttpOutcome::Failed;
            if (zip.size() > maxBytes) return HttpOutcome::Failed;
            fs.addFile(dest, zip);
            return HttpOutcome::Ok;
        };
        io.freeBytes = [this] { return free; };
        io.cancelled = [this] { return cancelAfterDownload && downloads > 0; };
        io.log = [this](std::string line) { log.push_back(std::move(line)); };
        io.progress = [](EmuStep, u32, u32) {};
    }

    EmuInstallResult run(const OscApp& a) { return installEmulator(fs, io, "http://h/hbb", a); }
    EmuInstallResult run() { return run(appFor(zip)); }

    bool logHas(std::string_view s) const {
        for (const auto& l : log)
            if (l.find(s) != std::string::npos) return true;
        return false;
    }
    bool tmpEmpty() const { return fs.filesUnder("sd:/newlauncher/tmp/").empty(); }
};

}  // namespace

TEST_CASE("installEmulator: Erfolg legt Dateien an und trägt den Stand ein") {
    Harness h;
    const auto r = h.run();
    CHECK(r.error == EmuInstallError::None);
    CHECK(h.lastUrl == "http://h/hbb/fceugx/fceugx.zip");
    CHECK(text(h.fs.file("sd:/apps/fceugx/boot.dol")) == "NEUES-DOL");
    CHECK(text(h.fs.file("sd:/apps/fceugx/meta.xml")) == "<app>neu</app>");
    CHECK(text(h.fs.file("sd:/fceugx/fceugx.cfg")) == "standard-cfg");
    const auto inst = parseInstalledEmus(text(h.fs.file(std::string(kInstalledEmusPath))));
    REQUIRE(inst.count("fceugx") == 1);
    CHECK(inst.at("fceugx").timestamp == 1793664000u);
    CHECK(inst.at("fceugx").version == "4.0.4");
    CHECK(h.tmpEmpty());
    CHECK(h.logHas("NL_EMU_INSTALL fceugx ok files=3"));
}

TEST_CASE("installEmulator: Nutzerdaten bleiben, apps/ wird ersetzt") {
    Harness h;
    h.fs.addFile("sd:/fceugx/fceugx.cfg", bytes("meine-cfg"));
    h.fs.addFile("sd:/apps/fceugx/meta.xml", bytes("<app>alt</app>"));
    h.fs.addFile("sd:/apps/fceugx/boot.dol", bytes("ALTES-DOL"));
    CHECK(h.run().error == EmuInstallError::None);
    CHECK(text(h.fs.file("sd:/fceugx/fceugx.cfg")) == "meine-cfg");
    CHECK(text(h.fs.file("sd:/apps/fceugx/meta.xml")) == "<app>neu</app>");
    CHECK(text(h.fs.file("sd:/apps/fceugx/boot.dol")) == "NEUES-DOL");
    CHECK(h.fs.filesUnder("sd:/apps/fceugx/boot.dol.").empty());  // keine .old-Reste
}

TEST_CASE("installEmulator: boot.dol wird als letzte Datei unter apps/ verschoben") {
    Harness h;
    REQUIRE(h.run().error == EmuInstallError::None);
    std::string lastApps;
    for (const auto& [from, to] : h.fs.renames)
        if (to.rfind("sd:/apps/", 0) == 0) lastApps = to;
    CHECK(lastApps == "sd:/apps/fceugx/boot.dol");
}

TEST_CASE("installEmulator: zu wenig Platz → NoSpace ohne Download") {
    Harness h;
    h.free = 1000;
    const auto r = h.run();
    CHECK(r.error == EmuInstallError::NoSpace);
    CHECK(h.downloads == 0);
    CHECK(h.logHas("NL_EMU_INSTALL fceugx fail"));
}

TEST_CASE("installEmulator: Download-Fehler → Download") {
    Harness h;
    h.downloadFails = true;
    CHECK(h.run().error == EmuInstallError::Download);
    CHECK(h.tmpEmpty());
    CHECK(h.fs.file(std::string(kInstalledEmusPath)) == nullptr);
}

TEST_CASE("installEmulator: falsche Größe → BadSize") {
    Harness h;
    OscApp a = appFor(h.zip);
    a.zipSize += 1;
    CHECK(h.run(a).error == EmuInstallError::BadSize);
    CHECK(h.tmpEmpty());
}

TEST_CASE("installEmulator: unsicherer Pfad → BadZip, nichts außerhalb von tmp") {
    Harness h;
    h.zip = test::buildZip({test::storedEntry("../evil", "x"), test::storedEntry("apps/fceugx/boot.dol", "D")});
    CHECK(h.run().error == EmuInstallError::BadZip);
    CHECK(h.fs.file("sd:/evil") == nullptr);
    CHECK(h.fs.file("sd:/newlauncher/evil") == nullptr);
    CHECK(h.fs.file("sd:/apps/fceugx/boot.dol") == nullptr);
    CHECK(h.tmpEmpty());
}

TEST_CASE("installEmulator: CRC-Fehler → BadZip, keine Datei außerhalb von tmp geändert") {
    Harness h;
    h.fs.addFile("sd:/apps/fceugx/boot.dol", bytes("ALTES-DOL"));
    auto e = test::storedEntry("apps/fceugx/meta.xml", "<app>neu</app>");
    e.crc ^= 1u;
    h.zip = test::buildZip({test::storedEntry("apps/fceugx/boot.dol", "NEUES-DOL"), e});
    CHECK(h.run().error == EmuInstallError::BadZip);
    CHECK(text(h.fs.file("sd:/apps/fceugx/boot.dol")) == "ALTES-DOL");
    CHECK(h.fs.file("sd:/apps/fceugx/meta.xml") == nullptr);
    CHECK(h.fs.renames.empty());
    CHECK(h.tmpEmpty());
}

TEST_CASE("installEmulator: Abbruch nach dem Download → Cancelled, nichts verschoben") {
    Harness h;
    h.fs.addFile(std::string(kInstalledEmusPath), bytes("[vbagx]\ntimestamp = 5\nversion = v\n"));
    h.cancelAfterDownload = true;
    CHECK(h.run().error == EmuInstallError::Cancelled);
    CHECK(h.fs.renames.empty());
    CHECK(h.fs.file("sd:/apps/fceugx/boot.dol") == nullptr);
    CHECK(text(h.fs.file(std::string(kInstalledEmusPath))) == "[vbagx]\ntimestamp = 5\nversion = v\n");
    CHECK(h.tmpEmpty());
}

TEST_CASE("installEmulator: Fehler beim Verschieben → Write, Stand unverändert") {
    Harness h;
    h.fs.addFile("sd:/apps/fceugx/boot.dol", bytes("ALTES-DOL"));
    h.fs.failRenameAt = 1;
    CHECK(h.run().error == EmuInstallError::Write);
    CHECK(h.fs.file(std::string(kInstalledEmusPath)) == nullptr);
    CHECK(text(h.fs.file("sd:/apps/fceugx/boot.dol")) == "ALTES-DOL");  // boot.dol kommt zuletzt, nie erreicht
    CHECK(h.tmpEmpty());
}

TEST_CASE("installEmulator: Einträge anderer Emulatoren bleiben erhalten") {
    Harness h;
    h.fs.addFile(std::string(kInstalledEmusPath), bytes("[vbagx]\ntimestamp = 5\nversion = v\n"));
    REQUIRE(h.run().error == EmuInstallError::None);
    const auto inst = parseInstalledEmus(text(h.fs.file(std::string(kInstalledEmusPath))));
    CHECK(inst.size() == 2);
    CHECK(inst.at("vbagx").timestamp == 5u);
}

TEST_CASE("installEmulator: Paket ohne die Katalog-DOL → BadZip, alte Version bleibt") {
    // Review M10: sonst meldet die Zeile „aktuell“, gestartet wird aber weiter die alte boot.dol.
    Harness h;
    h.fs.addFile("sd:/apps/fceugx/boot.dol", bytes("ALTES-DOL"));
    h.zip = test::buildZip({test::storedEntry("apps/fceugx/boot.elf", "ELF"),
                            test::storedEntry("apps/fceugx/meta.xml", "<app>neu</app>")});
    const auto r = h.run();
    CHECK(r.error == EmuInstallError::BadZip);
    CHECK(r.detail.find("apps/fceugx/boot.dol") != std::string::npos);
    CHECK(text(h.fs.file("sd:/apps/fceugx/boot.dol")) == "ALTES-DOL");
    CHECK(h.fs.file(std::string(kInstalledEmusPath)) == nullptr);
    CHECK(h.fs.renames.empty());
    CHECK(h.tmpEmpty());
}

TEST_CASE("installEmulator: Apps/ in anderer Schreibweise wird wie apps/ ersetzt") {
    // FAT unterscheidet keine Groß-/Kleinschreibung: „Apps/fceugx/boot.dol“ ist dieselbe Datei.
    Harness h;
    h.fs.addFile("sd:/apps/fceugx/boot.dol", bytes("ALTES-DOL"));
    h.zip = test::buildZip({test::storedEntry("Apps/fceugx/boot.dol", "NEUES-DOL")});
    CHECK(h.run().error == EmuInstallError::None);
    CHECK(text(h.fs.file("sd:/apps/fceugx/boot.dol")) == "NEUES-DOL");
}

TEST_CASE("installEmulator: Abbruch während des Downloads → Cancelled statt Download") {
    Harness h;
    h.downloadFails = true;  // httpGet liefert bei Abbruch Failed
    h.cancelAfterDownload = true;
    CHECK(h.run().error == EmuInstallError::Cancelled);
    CHECK(h.tmpEmpty());
}

TEST_CASE("recoverEmuInstall: Stromausfall zwischen den Umbenennungen") {
    MemFileSystem fs;
    // boot.dol → boot.dol.old geschehen, staged → boot.dol nicht: alte DOL zurückholen.
    fs.addFile("sd:/apps/fceugx/boot.dol.old", bytes("ALTES-DOL"));
    // Beide da: die neue DOL ist schon an ihrem Platz, .old ist ein Rest.
    fs.addFile("sd:/apps/snes9xgx/boot.dol", bytes("NEU"));
    fs.addFile("sd:/apps/snes9xgx/boot.dol.old", bytes("ALT"));
    recoverEmuInstall(fs);
    CHECK(text(fs.file("sd:/apps/fceugx/boot.dol")) == "ALTES-DOL");
    CHECK(fs.file("sd:/apps/fceugx/boot.dol.old") == nullptr);
    CHECK(text(fs.file("sd:/apps/snes9xgx/boot.dol")) == "NEU");
    CHECK(fs.file("sd:/apps/snes9xgx/boot.dol.old") == nullptr);
}

TEST_CASE("clearEmuTmp: löscht alle Dateien rekursiv") {
    MemFileSystem fs;
    fs.addFile("sd:/newlauncher/tmp/a.zip", bytes("x"));
    fs.addFile("sd:/newlauncher/tmp/a/b/c.txt", bytes("y"));
    fs.addFile("sd:/newlauncher/config.ini", bytes("z"));
    clearEmuTmp(fs);
    CHECK(fs.filesUnder("sd:/newlauncher/tmp/").empty());
    CHECK(fs.file("sd:/newlauncher/config.ini") != nullptr);
    clearEmuTmp(fs);  // zweimal ok
}

TEST_CASE("installEmulator: ohne SD auf das <data>-Gerät (Wurzel usb:)") {
    Harness h;
    h.fs.addDir("usb:/newlauncher");
    const EmuInstallResult r = installEmulator(h.fs, h.io, "http://h/hbb", appFor(h.zip), "usb:");
    CHECK(r.error == EmuInstallError::None);
    CHECK(text(h.fs.file("usb:/apps/fceugx/boot.dol")) == "NEUES-DOL");
    CHECK(text(h.fs.file("usb:/fceugx/fceugx.cfg")) == "standard-cfg");
    CHECK(h.fs.file("sd:/apps/fceugx/boot.dol") == nullptr);
    CHECK(h.fs.file(installedEmusPath("usb:")) != nullptr);
    CHECK(h.fs.filesUnder("usb:/newlauncher/tmp/").empty());
    CHECK(emuTmpDir("usb:") == "usb:/newlauncher/tmp");
    CHECK(installedEmusPath("sd:") == kInstalledEmusPath);
    CHECK(onRoot("sd:/apps/vbagx/boot.dol", "usb2:") == "usb2:/apps/vbagx/boot.dol");
}

TEST_CASE("recoverEmuInstall und clearEmuTmp mit Wurzel") {
    MemFileSystem fs;
    fs.addFile("usb:/apps/fceugx/boot.dol.old", bytes("ALT"));
    fs.addFile("usb:/newlauncher/tmp/a.zip", bytes("x"));
    recoverEmuInstall(fs, "usb:");
    clearEmuTmp(fs, "usb:");
    CHECK(text(fs.file("usb:/apps/fceugx/boot.dol")) == "ALT");
    CHECK(fs.filesUnder("usb:/newlauncher/tmp/").empty());
}

TEST_CASE("installZipApp: freie URL, ohne Eintrag in emulators_installed.ini (Selbst-Update)") {
    Harness h;
    h.zip = test::buildZip({
        test::dirEntry("apps/newlauncher/"),
        test::storedEntry("apps/newlauncher/boot.dol", "NL-NEU"),
        test::storedEntry("apps/newlauncher/meta.xml", "<app>nl</app>"),
    });
    h.fs.addFile("sd:/apps/newlauncher/boot.dol", bytes("NL-ALT"));
    OscApp a = appFor(h.zip);
    a.name = "newlauncher";
    const auto r = installZipApp(h.fs, h.io, "https://x/rel/newlauncher-0.2.0.zip", a);
    CHECK(r.error == EmuInstallError::None);
    CHECK(h.lastUrl == "https://x/rel/newlauncher-0.2.0.zip");
    CHECK(text(h.fs.file("sd:/apps/newlauncher/boot.dol")) == "NL-NEU");
    CHECK(h.fs.file(std::string(kInstalledEmusPath)) == nullptr);
    CHECK(h.tmpEmpty());
    CHECK(h.logHas("NL_EMU_INSTALL newlauncher ok files=2"));
}

TEST_CASE("installZipApp: ZIP ohne apps/newlauncher/boot.dol wird abgelehnt, alte DOL bleibt") {
    Harness h;
    h.zip = test::buildZip({test::storedEntry("apps/andere/boot.dol", "X")});
    h.fs.addFile("sd:/apps/newlauncher/boot.dol", bytes("NL-ALT"));
    OscApp a = appFor(h.zip);
    a.name = "newlauncher";
    CHECK(installZipApp(h.fs, h.io, "https://x/a.zip", a).error == EmuInstallError::BadZip);
    CHECK(text(h.fs.file("sd:/apps/newlauncher/boot.dol")) == "NL-ALT");
}
