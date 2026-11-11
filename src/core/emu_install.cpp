#include "core/emu_install.hpp"

#include <algorithm>
#include <map>
#include <vector>

#include "core/zip_reader.hpp"

namespace nl {

namespace {

constexpr u64 kSpareBytes = 1024 * 1024;
constexpr std::size_t kMaxInstalledIni = 64 * 1024;

void clearDir(FileSystem& fs, const std::string& dir, int depth) {
    if (depth > 16) return;
    auto entries = fs.list(dir);
    if (!entries.isOk()) return;
    for (const DirEntry& e : entries.value()) {
        const std::string path = dir + "/" + e.name;
        if (e.isDir)
            clearDir(fs, path, depth + 1);
        else
            (void)fs.removeFile(path);
    }
}

std::string parentDir(const std::string& path) {
    const std::size_t slash = path.rfind('/');
    return slash == std::string::npos ? std::string() : path.substr(0, slash);
}

bool isBootDol(std::string_view rel) {
    const std::size_t slash = rel.rfind('/');
    return (slash == std::string_view::npos ? rel : rel.substr(slash + 1)) == "boot.dol";
}

char lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

bool equalsNoCase(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) { return lower(x) == lower(y); });
}

// FAT unterscheidet keine Groß-/Kleinschreibung: „Apps/x“ landet in apps/ und wird wie apps/ behandelt.
bool underApps(std::string_view rel) { return rel.size() > 5 && equalsNoCase(rel.substr(0, 5), "apps/"); }

// Die DOL, die der Launcher startet, relativ zu sd:/ (Katalog; sonst apps/<name>/boot.dol).
std::string catalogDol(const OscApp& app) {
    for (const EmuInfo& e : emulatorCatalog())
        if (e.oscName == app.name && e.dol.rfind("sd:/", 0) == 0) return std::string(e.dol.substr(4));
    return "apps/" + app.name + "/boot.dol";
}

bool exists(FileSystem& fs, const std::string& path) {
    const std::string dir = parentDir(path);
    auto entries = fs.list(dir);
    if (!entries.isOk()) return false;
    const std::string name = path.substr(dir.size() + 1);
    for (const DirEntry& e : entries.value())
        if (!e.isDir && e.name == name) return true;
    return false;
}

// Ersetzt dest durch staged: dest → dest.old, staged → dest, .old löschen; schlägt der zweite Schritt fehl,
// kommt .old zurück.
Result<void> replaceFile(FileSystem& fs, const std::string& staged, const std::string& dest) {
    if (!exists(fs, dest)) return fs.renameFile(staged, dest);
    const std::string old = dest + ".old";
    (void)fs.removeFile(old);
    if (auto r = fs.renameFile(dest, old); !r.isOk()) return r;
    if (auto r = fs.renameFile(staged, dest); !r.isOk()) {
        (void)fs.renameFile(old, dest);
        return r;
    }
    (void)fs.removeFile(old);
    return Result<void>::ok();
}

struct Session {
    FileSystem& fs;
    EmuInstallIo& io;
    const OscApp& app;

    EmuInstallResult fail(EmuInstallError e, std::string detail) {
        clearEmuTmp(fs);
        if (io.log)
            io.log("NL_EMU_INSTALL " + app.name + " fail " + std::string(emuInstallErrorName(e)) + " " + detail);
        return EmuInstallResult{e, std::move(detail)};
    }
    bool cancelled() const { return io.cancelled && io.cancelled(); }
    void progress(EmuStep s, u32 done, u32 total) {
        if (io.progress) io.progress(s, done, total);
    }
};

}  // namespace

std::string_view emuInstallErrorName(EmuInstallError e) {
    switch (e) {
        case EmuInstallError::None: return "none";
        case EmuInstallError::NoSpace: return "nospace";
        case EmuInstallError::Download: return "download";
        case EmuInstallError::BadSize: return "badsize";
        case EmuInstallError::BadZip: return "badzip";
        case EmuInstallError::Write: return "write";
        case EmuInstallError::Cancelled: return "cancelled";
    }
    return "?";
}

void clearEmuTmp(FileSystem& fs) { clearDir(fs, std::string(kEmuTmpDir), 0); }

void recoverEmuInstall(FileSystem& fs) {
    for (const EmuInfo& e : emulatorCatalog()) {
        const std::string dol(e.dol), old = dol + ".old";
        if (!exists(fs, old)) continue;
        if (exists(fs, dol))
            (void)fs.removeFile(old);
        else
            (void)fs.renameFile(old, dol);
    }
}

EmuInstallResult installEmulator(FileSystem& fs, EmuInstallIo& io, std::string_view repo, const OscApp& app) {
    Session s{fs, io, app};
    clearEmuTmp(fs);

    // 1. Platz
    const u64 need = u64{app.zipSize} + u64{app.unzippedSize} + kSpareBytes;
    if (io.freeBytes && io.freeBytes() < need)
        return s.fail(EmuInstallError::NoSpace, std::to_string(need));

    // 2. Download in die Zwischenablage
    const std::string tmp(kEmuTmpDir);
    if (!fs.makeDirs(tmp).isOk()) return s.fail(EmuInstallError::Write, tmp);
    const std::string zip = tmp + "/" + app.name + ".zip";
    s.progress(EmuStep::Download, 0, app.zipSize);
    const bool downloaded = io.download && io.download(oscZipUrl(repo, app.name), zip, app.zipSize) == HttpOutcome::Ok;
    if (s.cancelled()) return s.fail(EmuInstallError::Cancelled, "download");  // vor Download: B zeigt „Abgebrochen“
    if (!downloaded) return s.fail(EmuInstallError::Download, oscZipUrl(repo, app.name));

    // 3. Prüfen
    s.progress(EmuStep::Verify, 0, 1);
    const std::optional<u64> got = [&]() -> std::optional<u64> {
        auto entries = fs.list(tmp);
        if (!entries.isOk()) return std::nullopt;
        for (const DirEntry& e : entries.value())
            if (!e.isDir && e.name == app.name + ".zip") return e.size;
        return std::nullopt;
    }();
    if (!got || *got != app.zipSize)
        return s.fail(EmuInstallError::BadSize, std::to_string(got.value_or(0)) + "!=" + std::to_string(app.zipSize));
    auto dir = readZipDirectory(fs, zip, *got);
    if (!dir.isOk()) return s.fail(EmuInstallError::BadZip, dir.error().context);
    std::vector<const ZipEntry*> files;
    bool hasDol = false;
    const std::string dol = catalogDol(app);
    for (const ZipEntry& e : dir.value()) {
        if (e.isDir) continue;
        files.push_back(&e);
        hasDol = hasDol || equalsNoCase(e.name, dol);
    }
    // Ohne die DOL, die der Launcher startet, wäre „installiert“ gelogen (alte DOL liefe weiter).
    if (!hasDol) return s.fail(EmuInstallError::BadZip, "fehlt: " + dol);

    // 4. Entpacken
    const std::string stage = tmp + "/" + app.name;
    u32 done = 0;
    for (const ZipEntry* e : files) {
        if (s.cancelled()) return s.fail(EmuInstallError::Cancelled, "extract");
        s.progress(EmuStep::Extract, done, static_cast<u32>(files.size()));
        auto data = extractZipEntry(fs, zip, *e);
        if (!data.isOk()) return s.fail(EmuInstallError::BadZip, e->name);
        const std::string path = stage + "/" + e->name;
        if (!fs.makeDirs(parentDir(path)).isOk() || !fs.writeAtomic(path, data.value()).isOk())
            return s.fail(EmuInstallError::Write, path);
        ++done;
    }
    (void)fs.removeFile(zip);
    if (s.cancelled()) return s.fail(EmuInstallError::Cancelled, "extract");

    // 5. Verschieben: apps/ ersetzen, sonst nur Fehlendes anlegen; boot.dol zuletzt. Kein Abbruch mehr.
    std::stable_sort(files.begin(), files.end(),
                     [](const ZipEntry* a, const ZipEntry* b) { return !isBootDol(a->name) && isBootDol(b->name); });
    done = 0;
    for (const ZipEntry* e : files) {
        s.progress(EmuStep::Move, done++, static_cast<u32>(files.size()));
        const std::string staged = stage + "/" + e->name;
        const bool apps = underApps(e->name);
        const std::string dest = "sd:/" + (apps ? "apps/" + e->name.substr(5) : e->name);
        if (!fs.makeDirs(parentDir(dest)).isOk()) return s.fail(EmuInstallError::Write, dest);
        if (apps) {
            if (!replaceFile(fs, staged, dest).isOk()) return s.fail(EmuInstallError::Write, dest);
        } else if (!exists(fs, dest)) {
            if (!fs.renameFile(staged, dest).isOk()) return s.fail(EmuInstallError::Write, dest);
        }
    }

    // 6. Stand eintragen
    std::map<std::string, InstalledEmu> installed;
    if (auto old = fs.readAll(kInstalledEmusPath, kMaxInstalledIni); old.isOk()) {
        const auto& d = old.value();
        installed = parseInstalledEmus(std::string_view(reinterpret_cast<const char*>(d.data()), d.size()));
    }
    installed[app.name] = InstalledEmu{app.timestamp, app.version};
    const std::string ini = formatInstalledEmus(installed);
    if (!fs.writeAtomic(kInstalledEmusPath, std::span<const u8>(reinterpret_cast<const u8*>(ini.data()), ini.size())).isOk())
        return s.fail(EmuInstallError::Write, std::string(kInstalledEmusPath));

    clearEmuTmp(fs);
    if (io.log) io.log("NL_EMU_INSTALL " + app.name + " ok files=" + std::to_string(files.size()));
    return EmuInstallResult{};
}

}  // namespace nl
