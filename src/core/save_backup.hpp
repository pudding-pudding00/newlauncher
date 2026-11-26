#pragma once
// Lokale Sicherungen und Wiederherstellung mit Journal (M26, Spec Ziel 3).
// Sicherungen: <data>/saves/<einheit>/<yyyymmdd-hhmmss>.zip, höchstens 5. Journal: <data>/saves/restore.ini.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/fs.hpp"
#include "core/result.hpp"
#include "core/save_archive.hpp"
#include "core/save_locator.hpp"

namespace nl {

inline constexpr std::size_t kKeepBackups = 5;

std::string backupDir(std::string_view dataDir, std::string_view unitId);
std::string backupFileName(u64 unix);  // yyyymmdd-hhmmss.zip (UTC)
std::vector<std::string> listBackups(FileSystem& fs, std::string_view dataDir, std::string_view unitId);  // neueste zuerst
Result<void> pruneBackups(FileSystem& fs, std::string_view dataDir, std::string_view unitId, std::size_t keep = kKeepBackups);

struct BackupInfo {
    std::string zipPath;  // leer bei empty
    std::string hash;
    bool empty = false;
};

// Leere Einheit → empty, kein ZIP. Belegt die Sekunde schon eine Sicherung, rückt der Name weiter. Danach pruneBackups.
Result<BackupInfo> backupUnit(FileSystem& fs, std::string_view dataDir, const SaveUnit& u, std::string_view title,
                              std::string_view device, u64 now);

enum class RestorePhase : u8 { Writing, Verifying };

struct RestoreJournal {
    std::string unit, dir, backup;    // backup leer = Einheit war leer
    std::vector<std::string> added;   // Pfade, die es vor der Wiederherstellung nicht gab
    RestorePhase phase = RestorePhase::Writing;
};

std::string serializeJournal(const RestoreJournal& j);
std::optional<RestoreJournal> parseJournal(std::string_view text);

// Prüft Einheit und alle Pfade vor dem ersten Schreiben; dann Sicherung, Journal, Schreiben, Zurücklesen + Hash,
// Journal löschen. Fehler nach dem Journal → sofort zurückrollen und den Fehler melden.
Result<void> restoreUnit(FileSystem& fs, std::string_view dataDir, const SaveUnit& u, const SavePackage& pkg,
                         std::string_view title, std::string_view device, u64 now);

// Liest ein Spielstand-ZIP (Sicherung oder Cloud-Paket), entpackt es und ruft restoreUnit. Kaputtes ZIP → nichts geschrieben.
Result<void> restoreFromZip(FileSystem& fs, std::string_view dataDir, const SaveUnit& u, std::string_view zipPath,
                            std::string_view title, std::string_view device, u64 now);

enum class RecoverResult : u8 { None, RolledBack, Failed };

// Beim Start: liegt ein Journal, Sicherung zurückspielen (leer = nur added löschen), added löschen, Journal löschen.
RecoverResult recoverRestore(FileSystem& fs, std::string_view dataDir);

}  // namespace nl
