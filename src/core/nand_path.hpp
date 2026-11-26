#pragma once
// Pfadregeln für Schreibzugriffe auf den echten NAND (M26): geschrieben wird nur unter
// /title/<hi>/<lo>/data. Host-testbar, die ISFS-Aufrufe liegen in platform/isfs_fs.

#include <optional>
#include <string>
#include <string_view>

namespace nl {

constexpr std::size_t kIsfsMaxName = 12;  // NAND-Dateinamen sind auf 12 Zeichen begrenzt
constexpr std::size_t kIsfsMaxPath = 64;  // ISFS-Pfade inklusive führendem '/'

// "nand:/a/b" → "/a/b"; alles andere (anderes Gerät, relativ, "..") → nullopt.
std::optional<std::string> isfsPath(std::string_view path);

// Datei unter /title/<8 hex>/<8 hex>/data/…, ohne leere Teile, ohne "." und "..", in den NAND-Grenzen.
bool isSaveDataPath(std::string_view isfs);
// Der Ordner data selbst oder ein Unterordner davon.
bool isSaveDataDir(std::string_view isfs);

// Zwischendatei für atomares Schreiben: /tmp/nl_<crc32 des Ziels, 8 hex>.
std::string nandTempPath(std::string_view isfs);

}  // namespace nl
