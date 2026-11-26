#pragma once
// Spielstand-Paket (M26, Spec Ziel 2): ein ZIP je Einheit, erster Eintrag newlauncher-save.ini mit Format,
// Einheit, Titel, Gerät, Zeit, Inhalts-Hash und Dateizahl. Der Hash hängt nur vom Inhalt ab, nie von den ZIP-Bytes.

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/result.hpp"
#include "core/types.hpp"

namespace nl {

inline constexpr std::size_t kMaxSaveUnitBytes = 16u * 1024 * 1024;  // roh je Einheit (Nintendont-Multikarte)
inline constexpr std::string_view kSaveMetaName = "newlauncher-save.ini";
inline constexpr u32 kSaveFormat = 1;

struct SaveFile {
    std::string path;  // relativ zum Einheiten-Stamm, Trenner '/'
    std::vector<u8> data;
};

struct SaveMeta {
    u32 format = kSaveFormat;
    std::string unit, title, device;
    u64 saved = 0;  // Unix-Zeit
    std::string hash;
    u32 files = 0;
};

// SHA-256 (hex) über die nach Pfad sortierten Zeilen "<pfad>\n<größe>\n<sha256 hex>\n".
std::string contentHash(std::span<const SaveFile> files);

// Setzt hash und files. Summe der Dateien über kMaxSaveUnitBytes oder ein unsicherer Pfad → InvalidData.
Result<std::vector<u8>> packSave(SaveMeta meta, std::span<const SaveFile> files);

struct SavePackage {
    SaveMeta meta;
    std::vector<SaveFile> files;
};

// Meta muss der erste Eintrag sein; format ≠ 1 → VersionMismatch. Die Summe der Größen aus dem Verzeichnis wird
// vor dem Entpacken gegen maxRaw geprüft; Hash oder Dateizahl falsch, doppelter Pfad → InvalidData.
Result<SavePackage> unpackSave(std::span<const u8> zip, std::size_t maxRaw = kMaxSaveUnitBytes);

// Nur die Meta (Liste der Sicherungen), ohne die Dateien zu entpacken.
Result<SaveMeta> readSaveMeta(std::span<const u8> zip);

}  // namespace nl
