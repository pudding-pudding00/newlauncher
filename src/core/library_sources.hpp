#pragma once
// Bibliotheksquellen, Ordner und Endungen je Konsole aus config.ini (Spec v2 5.2–5.4, M12).

#include "core/console.hpp"
#include "core/ini.hpp"

#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace nl {

inline constexpr std::size_t kMaxConsoleFolders = 8;
inline constexpr std::size_t kMaxConsoleExtensions = 8;

// [library] sources = auto | usb,usb2,sd. Gerätenamen klein und ohne „:“.
struct SourceSpec {
    bool autoMode = true;
    std::vector<std::string> devices;
};

struct ConsolePaths {
    std::vector<std::string> folders;     // relativ zur Gerätewurzel, „/“ als Trenner, ohne „/“ am Ende
    std::vector<std::string> extensions;  // klein, mit Punkt
};

struct LibraryRules {
    SourceSpec sources;
    std::array<ConsolePaths, kConsoleCount> paths;  // Index = Console
    std::vector<std::string> bad;  // Schlüssel mit verworfenen Werten („paths.nes“) für NL_CFG_BAD
};

// Liest [library] sources (sonst Alt-Schlüssel [general] library = usb|sd, sonst auto), [paths] und [extensions].
// Ungültige Teile fallen weg; bleibt nichts übrig, gilt der Standard. Beides landet in `bad`.
LibraryRules libraryRulesFromIni(const IniDoc& ini);

// Standardordner und -endungen aus der Konsolentabelle.
ConsolePaths defaultConsolePaths(Console c);

std::string sourcesToText(const SourceSpec& s);  // "auto" bzw. "usb,usb2,sd"

// Die zu scannenden Geräte in Scan-Reihenfolge: auto = alle eingehängten in Gerätereihenfolge, sonst die Liste,
// soweit eingehängt. `mounted` darf „sd:“ oder „USB“ enthalten; zurück kommen die Namen aus der Quellenliste.
std::vector<std::string> orderedSources(const SourceSpec& s, std::span<const std::string> mounted);

}  // namespace nl
