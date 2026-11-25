#pragma once
// „Fehlende Cover laden“ (Spec 3.4, 6.4): Regionen, Aufträge, URL, Zusammenfassung.

#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "core/console.hpp"
#include "core/fs.hpp"
#include "core/game.hpp"
#include "core/http.hpp"
#include "core/net_error.hpp"
#include "core/types.hpp"

namespace nl {

// GameTDB: HTTPS mit TLS-Build, sonst HTTP (M11 Task 8: 200 image/png über HTTPS geprüft, 2026-11-11).
inline constexpr std::string_view kGametdbCoverBaseHttps = "https://art.gametdb.com/wii/cover";
inline constexpr std::string_view kGametdbCoverBaseHttp = "http://art.gametdb.com/wii/cover";
constexpr std::string_view defaultCoverBase(bool tls) { return tls ? kGametdbCoverBaseHttps : kGametdbCoverBaseHttp; }
inline constexpr u32 kMaxCoverDownloadBytes = 2u * 1024 * 1024;

// Feste Region zuerst, sonst die Systemregion; danach EN, US, JA ohne Doppelte.
// Unbekannte Konfigurationswerte gelten als "auto".
std::vector<std::string_view> coverRegions(std::string_view configRegion, std::string_view systemRegion);

std::string gametdbCoverUrl(std::string_view base, std::string_view region, std::string_view id6);

// M22: Retro-Boxarts von libretro-thumbnails (GitHub; thumbnails.libretro.com liefert seit 2026-11 nur 404).
// Nur HTTPS: ohne TLS-Build gibt es keine Standardquelle (kein Rückfall auf HTTP).
inline constexpr std::string_view kLibretroCoverBase = "https://raw.githubusercontent.com/libretro-thumbnails";
constexpr std::string_view defaultRetroCoverBase(bool tls) { return tls ? kLibretroCoverBase : std::string_view(); }

// <base>/<System_mit_Unterstrichen>/master/Named_Boxarts/<name>.png; im Namen &*/:`<>?\|" → _ (libretro-Regel),
// dann prozentkodiert. Leer für Wii/GC/Kanäle oder leere Basis.
std::string libretroCoverUrl(std::string_view base, Console console, std::string_view fileStem);

// M24: Namensvarianten für libretro-thumbnails, exakter Name zuerst. Deckt ab: andere Revision bzw. keine,
// fehlendes oder zusätzliches „(En)“, weggelassene Zusatz-Tags, Doppeltitel „A ~ B“ → „A“, Einzelregion aus
// Mehrfachregion und Ersatzregionen (USA, World, „USA, Europe“, Europe, „Japan, USA“; Japan nur für
// japanische Ausgaben). Ohne Doppelte, höchstens kMaxLibretroCandidates.
inline constexpr std::size_t kMaxLibretroCandidates = 16;
std::vector<std::string> libretroNameCandidates(std::string_view stem);

// M24: libretro-thumbnails nutzt Git-Symlinks; raw.githubusercontent liefert dafür nur den Zielnamen als Text.
// Liefert den Zielnamen (einzeilig, < 512 Bytes, endet auf .png, ohne Pfadtrenner), sonst leer.
std::string libretroSymlinkTarget(std::span<const u8> body);
// URL mit dem letzten Pfadteil ersetzt durch den (prozentkodierten) Zielnamen.
std::string libretroLinkUrl(std::string_view url, std::string_view target);
inline constexpr int kMaxLibretroLinkHops = 2;

struct CoverJob {
    std::string id6;
    std::string stem;
    std::string root = {};  // Cover-Wurzel des Geräts (coverRoot), Ziel von coverSrcPath
    std::vector<std::string> urls = {};  // M22/M24: libretro-Kandidaten der Reihe nach; leer = GameTDB über Regionen
    Console console = Console::Wii;  // M24: bestimmt die Reihenfolge (orderCoverJobs)
};

// M24: Rang der Download-Reihenfolge, Konsolen von neu nach alt wie die Konsolenleiste:
// Wii 0, GC 1, GBA 2, N64 3, GBC 4, SNES 5, GB 6, NES 7, Kanäle danach.
int coverJobRank(Console c);
// Sortiert stabil nach coverJobRank; innerhalb einer Konsole bleibt die Reihenfolge (Titel der Bibliothek).
void orderCoverJobs(std::vector<CoverJob>& jobs);

// Wii/GC (und mit retroBase auch Retro-Konsolen), bei denen weder covers_src-PNG noch .tex existiert. Stämme in
// skip (Fehlliste, M22) werden übersprungen.
std::vector<CoverJob> coverDownloadJobs(std::span<const Game* const> games,
                                        const std::function<bool(const std::string&)>& exists,
                                        std::string_view libRoot, std::string_view retroBase = {},
                                        const std::unordered_set<std::string>* skip = nullptr);

// Vorhandene Cover (covers_src/*.png und cache/covers/*.tex) als volle Pfade, mit je einem list() pro Ordner statt
// einer Abfrage pro Spiel; als exists für coverDownloadJobs. Ohne retro nur die Ordner wii und gc.
std::unordered_set<std::string> existingCoverFiles(FileSystem& fs, std::string_view libRoot, bool retro = false);

// M22: Fehlliste <wurzel>/newlauncher/cache/covers/missing.txt, ein Stamm je Zeile.
std::string missingListPath(std::string_view libRoot);
// M24: erste Zeile ist die Sucher-Version; Listen ohne diese Kopfzeile (älterer Sucher) gelten als leer.
inline constexpr std::string_view kMissingListHeader = "#nl-missing 2";
std::vector<std::string> parseMissingList(std::string_view text);  // ohne Leerzeilen und Doppelte
// (old − jobStems) ∪ missed, sortiert, ohne Doppelte: was dieser Lauf versucht hat, zählt neu.
std::vector<std::string> updateMissingList(const std::vector<std::string>& old, const std::vector<std::string>& jobStems,
                                           const std::vector<std::string>& missed);
std::string missingListText(const std::vector<std::string>& stems);

// M23 „Cover-Cache löschen“: alle Dateien in covers_src/<konsole> und cache/covers/<konsole> (alle Konsolen außer
// Kanälen) und die Fehlliste. Andere Dateien bleiben. failed zählt Dateien, die sich nicht löschen ließen.
struct CoverClearResult {
    u32 removed = 0, failed = 0;
    bool cancelled = false;  // M24: vor dem Ende abgebrochen
};
// M24: onRemoved nach jeder gelöschten Datei (Fortschritt; läuft im Lösch-Thread); cancelled wird vor jeder Datei
// geprüft (Start eines Spiels, Neustart, Ausschalten warten so nicht auf das ganze Löschen).
CoverClearResult clearCoverCache(FileSystem& fs, std::string_view libRoot, const std::function<void()>& onRemoved = {},
                                 const std::function<bool()>& cancelled = {});

bool isPng(std::span<const u8> data);

struct DownloadSummary {
    u32 ok = 0, missing = 0, failed = 0;
    NetError lastError = NetError::None;  // Ursache des letzten Fehlschlags (setzt der Worker; Spec v2 4.4)
};

// Nach so vielen Fehlern in Folge gelten Netz oder Server als tot: die übrigen Aufträge werden nicht versucht.
inline constexpr u32 kMaxConsecutiveDownloadFailures = 3;

struct CoverDownloadIo {
    std::function<HttpOutcome(const Url&, std::vector<u8>& body)> get;
    std::function<bool(const std::string& stem, std::span<const u8> png)> save;  // nach covers_src
    std::function<bool()> cancelled;                                            // vor jeder Anfrage geprüft
    std::function<void(std::string line)> log;
    std::function<void(const std::string& stem)> finished;  // Cover neu gespeichert
    std::function<void()> jobDone;                          // Fortschritt
    std::function<void(const std::string& stem)> missed;    // M22: nicht gefunden (für die Fehlliste); optional
};

// Ablauf von „Fehlende Cover laden“: je Auftrag die Regionen (bzw. die libretro-URLs) der Reihe nach; 200 ohne
// PNG-Signatur und 404 führen zur nächsten. Ein Symlink-Text wird bis zu kMaxLibretroLinkHops-mal verfolgt. Abbruch und Fehlerserie werten die übrigen Aufträge als failed (mit Marker).
DownloadSummary runCoverDownloads(std::span<const CoverJob> jobs, std::span<const std::string> regions,
                                  std::string_view baseUrl, const CoverDownloadIo& io);

// M21: Wann der automatische Lauf von „Fehlende Cover laden“ startet (einmal pro Sitzung).
struct CoverAutoInputs {
    bool enabled;      // [covers] auto
    bool ready;        // Netz bereit
    bool scanRunning;  // Bibliotheks-Scan läuft noch
    bool netBusy;      // anderer Netz-Auftrag läuft
    bool alreadyRan;   // in dieser Sitzung schon gestartet
    bool clearing = false;  // M24: Cover-Cache wird gerade gelöscht
};
bool shouldAutoFetchCovers(const CoverAutoInputs& in);

}  // namespace nl
