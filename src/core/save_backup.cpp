#include "core/save_backup.hpp"

#include <algorithm>
#include <cstdio>
#include <set>

#include "core/civil_time.hpp"
#include "core/ini.hpp"
#include "core/nand_path.hpp"
#include "core/zip_reader.hpp"

namespace nl {
namespace {

constexpr int kMaxSameSecond = 60;  // so viele Sicherungen in einer Sekunde sind ein Fehler, keine Absicht

std::string journalPath(std::string_view dataDir) { return std::string(dataDir) + "/saves/restore.ini"; }

std::span<const u8> asBytes(std::string_view s) { return {reinterpret_cast<const u8*>(s.data()), s.size()}; }

std::string parentDir(const std::string& path) {
    const std::size_t s = path.rfind('/');
    return s == std::string::npos ? std::string() : path.substr(0, s);
}

bool isBackupName(std::string_view n) {
    if (n.size() != 19 || n.substr(15) != ".zip" || n[8] != '-') return false;
    for (std::size_t i = 0; i < 15; ++i)
        if (i != 8 && (n[i] < '0' || n[i] > '9')) return false;
    return true;
}

bool exists(FileSystem& fs, const std::string& path) {
    u8 probe = 0;
    return fs.read(path, 0, std::span<u8>(&probe, 0)).isOk();
}

Result<void> writeFile(FileSystem& fs, const std::string& path, std::span<const u8> data) {
    const std::string dir = parentDir(path);
    if (!dir.empty())
        if (auto r = fs.makeDirs(dir); !r.isOk()) return r;
    return fs.writeAtomic(path, data);
}

Result<void> writeJournal(FileSystem& fs, std::string_view dataDir, const RestoreJournal& j) {
    const std::string text = serializeJournal(j);
    return writeFile(fs, journalPath(dataDir), asBytes(text));
}

// Zustand vor der Wiederherstellung herstellen: Sicherung zurückschreiben, neue Dateien löschen, Journal weg.
// Läuft bis zum Ende weiter, auch wenn ein Schritt scheitert, damit so viel wie möglich zurückkommt.
Result<void> rollback(FileSystem& fs, std::string_view dataDir, const RestoreJournal& j) {
    Result<void> first = Result<void>::ok();
    auto note = [&](Result<void> r) {
        if (!r.isOk() && first.isOk()) first = std::move(r);
    };
    if (!j.backup.empty()) {
        auto zip = fs.readAll(j.backup, kMaxSaveUnitBytes * 2);
        if (!zip.isOk()) return Result<void>::err(zip.error());  // ohne Sicherung kein Rückweg: Journal bleibt
        auto pkg = unpackSave(zip.value());
        if (!pkg.isOk()) return Result<void>::err(pkg.error());
        for (const SaveFile& f : pkg.value().files) note(writeFile(fs, j.dir + "/" + f.path, f.data));
    }
    for (const std::string& rel : j.added) note(fs.removeFile(j.dir + "/" + rel));
    if (!first.isOk()) return first;  // Journal bleibt liegen: der nächste Start versucht es noch einmal
    return fs.removeFile(journalPath(dataDir));
}

}  // namespace

std::string backupDir(std::string_view dataDir, std::string_view unitId) {
    return std::string(dataDir) + "/saves/" + std::string(unitId);
}

std::string backupFileName(u64 unix) {
    const CivilTime t = civilFromUnix(unix);
    char buf[32];
    std::snprintf(buf, sizeof buf, "%04d%02u%02u-%02u%02u%02u.zip", t.year, t.month, t.day, t.hour, t.minute, t.second);
    return buf;
}

std::vector<std::string> listBackups(FileSystem& fs, std::string_view dataDir, std::string_view unitId) {
    std::vector<std::string> out;
    auto list = fs.list(backupDir(dataDir, unitId));
    if (!list.isOk()) return out;
    for (const DirEntry& e : list.value())
        if (!e.isDir && isBackupName(e.name)) out.push_back(e.name);
    std::sort(out.begin(), out.end(), std::greater<>());
    return out;
}

Result<void> pruneBackups(FileSystem& fs, std::string_view dataDir, std::string_view unitId, std::size_t keep,
                          std::string_view protect) {
    const std::vector<std::string> list = listBackups(fs, dataDir, unitId);
    const std::string dir = backupDir(dataDir, unitId);
    std::size_t kept = std::count(list.begin(), list.end(), protect) != 0 ? 1 : 0;
    for (const std::string& name : list) {
        if (name == protect) continue;
        if (kept < keep) {
            ++kept;
            continue;
        }
        if (auto r = fs.removeFile(dir + "/" + name); !r.isOk()) return r;
    }
    return Result<void>::ok();
}

Result<BackupInfo> backupUnit(FileSystem& fs, std::string_view dataDir, const SaveUnit& u, std::string_view title,
                              std::string_view device, u64 now) {
    using R = Result<BackupInfo>;
    auto files = readUnit(fs, u);
    if (!files.isOk()) return R::err(files.error());
    BackupInfo info;
    if (files.value().empty()) {
        info.empty = true;
        return R::ok(std::move(info));
    }
    SaveMeta meta;
    meta.unit = u.id;
    meta.title = std::string(title);
    meta.device = std::string(device);
    meta.saved = now;
    auto zip = packSave(meta, files.value());
    if (!zip.isOk()) return R::err(zip.error());
    info.hash = contentHash(files.value());
    for (SaveFile& f : files.value()) info.paths.push_back(std::move(f.path));
    files.value() = {};  // Rohdaten freigeben, bevor das ZIP geschrieben wird (Speicher bei 16 MiB)
    const std::string dir = backupDir(dataDir, u.id);
    u64 t = now;
    std::string path = dir + "/" + backupFileName(t);
    for (int i = 0; i < kMaxSameSecond && exists(fs, path); ++i) path = dir + "/" + backupFileName(++t);
    if (exists(fs, path)) return R::err(Error{ErrorCode::IoError, "Sicherung: kein freier Name in " + dir});
    if (auto r = writeFile(fs, path, zip.value()); !r.isOk()) return R::err(r.error());
    info.zipPath = std::move(path);
    const std::string_view name = std::string_view(info.zipPath).substr(dir.size() + 1);
    if (auto r = pruneBackups(fs, dataDir, u.id, kKeepBackups, name); !r.isOk()) return R::err(r.error());
    return R::ok(std::move(info));
}

std::string serializeJournal(const RestoreJournal& j) {
    std::string out = "[restore]\n";
    out += "unit = " + j.unit + "\n";
    out += "dir = " + j.dir + "\n";
    out += "backup = " + j.backup + "\n";
    out += std::string("phase = ") + (j.phase == RestorePhase::Writing ? "writing" : "verifying") + "\n";
    for (const std::string& a : j.added) out += "added = " + a + "\n";
    return out;
}

std::optional<RestoreJournal> parseJournal(std::string_view text) {
    const IniDoc d = IniDoc::parse(text);
    RestoreJournal j;
    j.unit = std::string(d.get("restore", "unit"));
    j.dir = std::string(d.get("restore", "dir"));
    j.backup = std::string(d.get("restore", "backup"));
    const std::string_view phase = d.get("restore", "phase");
    if (phase == "writing") j.phase = RestorePhase::Writing;
    else if (phase == "verifying") j.phase = RestorePhase::Verifying;
    else return std::nullopt;
    if (j.unit.empty() || j.dir.empty()) return std::nullopt;
    for (const auto& [k, v] : d.entries("restore"))
        if (k == "added") {
            if (!isSafeZipPath(v)) return std::nullopt;  // ein manipuliertes Journal löscht nichts außerhalb
            j.added.push_back(v);
        }
    return j;
}

Result<void> restoreUnit(FileSystem& fs, std::string_view dataDir, const SaveUnit& u, const SavePackage& pkg,
                         std::string_view title, std::string_view device, u64 now) {
    using R = Result<void>;
    if (pkg.meta.unit != u.id)
        return R::err(Error{ErrorCode::InvalidData, "Spielstand gehört zu " + pkg.meta.unit + ", nicht " + u.id});
    for (const SaveFile& f : pkg.files) {
        if (!unitAcceptsPath(u, f.path)) return R::err(Error{ErrorCode::InvalidData, "Pfad nicht erlaubt: " + f.path});
        if (u.store == SaveStore::RealNand) {  // NAND-Grenzen (12 Zeichen je Name, 64 je Pfad) vor dem ersten Schreiben
            const auto isfs = isfsPath(u.dir + "/" + f.path);
            if (!isfs || !isSaveDataPath(*isfs))
                return R::err(Error{ErrorCode::InvalidData, "Pfad passt nicht in den NAND: " + f.path});
        }
    }
    if (contentHash(pkg.files) != pkg.meta.hash) return R::err(Error{ErrorCode::InvalidData, "Inhalts-Hash stimmt nicht"});

    // Die Sicherung liest den alten Stand; ihre Pfadliste sagt, welche Dateien neu dazukommen (nur ein Lesedurchgang).
    auto backup = backupUnit(fs, dataDir, u, title, device, now);
    if (!backup.isOk()) return R::err(backup.error());
    const std::set<std::string> had(backup.value().paths.begin(), backup.value().paths.end());

    RestoreJournal j;
    j.unit = u.id;
    j.dir = u.dir;
    j.backup = backup.value().zipPath;
    for (const SaveFile& f : pkg.files)
        if (had.count(f.path) == 0) j.added.push_back(f.path);
    if (auto r = writeJournal(fs, dataDir, j); !r.isOk()) return r;

    auto fail = [&](Error e) {
        (void)rollback(fs, dataDir, j);
        return R::err(std::move(e));
    };
    for (const SaveFile& f : pkg.files)
        if (auto r = writeFile(fs, u.dir + "/" + f.path, f.data); !r.isOk()) return fail(r.error());

    j.phase = RestorePhase::Verifying;
    if (auto r = writeJournal(fs, dataDir, j); !r.isOk()) return fail(r.error());
    auto back = readUnit(fs, u);
    if (!back.isOk()) return fail(back.error());
    std::set<std::string> wanted;
    for (const SaveFile& f : pkg.files) wanted.insert(f.path);
    std::vector<SaveFile> written;
    for (SaveFile& f : back.value())
        if (wanted.count(f.path) != 0) written.push_back(std::move(f));
    if (contentHash(written) != pkg.meta.hash)
        return fail(Error{ErrorCode::InvalidData, "Zurückgelesener Spielstand weicht ab"});
    return fs.removeFile(journalPath(dataDir));
}

Result<void> restoreFromZip(FileSystem& fs, std::string_view dataDir, const SaveUnit& u, std::string_view zipPath,
                            std::string_view title, std::string_view device, u64 now) {
    using R = Result<void>;
    // Deflate-Pakete sind kleiner als roh; das Doppelte der Rohgrenze lässt auch unkomprimierte ZIPs zu.
    auto zip = fs.readAll(zipPath, kMaxSaveUnitBytes * 2);
    if (!zip.isOk()) return R::err(zip.error());
    auto pkg = unpackSave(zip.value());
    zip.value() = {};  // gepackte Bytes freigeben, das Paket hält die Dateien
    if (!pkg.isOk()) return R::err(pkg.error());
    return restoreUnit(fs, dataDir, u, pkg.value(), title, device, now);
}

RecoverResult recoverRestore(FileSystem& fs, std::string_view dataDir) {
    auto raw = fs.readAll(journalPath(dataDir), 1024 * 1024);
    if (!raw.isOk()) return raw.error().code == ErrorCode::NotFound ? RecoverResult::None : RecoverResult::Failed;
    const auto j = parseJournal(std::string_view(reinterpret_cast<const char*>(raw.value().data()), raw.value().size()));
    if (!j) return RecoverResult::Failed;
    return rollback(fs, dataDir, *j).isOk() ? RecoverResult::RolledBack : RecoverResult::Failed;
}

}  // namespace nl
