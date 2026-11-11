#pragma once
// „Fehlende Cover laden“ (Spec 3.4, 6.4): Regionen, Aufträge, URL, Zusammenfassung.

#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

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

struct CoverJob {
    std::string id6;
    std::string stem;
};

// Nur Wii/GC, bei denen weder covers_src-PNG noch .tex existiert.
std::vector<CoverJob> coverDownloadJobs(std::span<const Game* const> games,
                                        const std::function<bool(const std::string&)>& exists,
                                        std::string_view libRoot);

// Vorhandene Wii/GC-Cover (covers_src/*.png und cache/covers/*.tex) als volle Pfade, mit je einem list() pro
// Ordner statt einer Abfrage pro Spiel; als exists für coverDownloadJobs.
std::unordered_set<std::string> existingCoverFiles(FileSystem& fs, std::string_view libRoot);

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
};

// Ablauf von „Fehlende Cover laden“: je Auftrag die Regionen der Reihe nach; 200 ohne PNG-Signatur und 404
// führen zur nächsten Region. Abbruch und Fehlerserie werten die übrigen Aufträge als failed (mit Marker).
DownloadSummary runCoverDownloads(std::span<const CoverJob> jobs, std::span<const std::string> regions,
                                  std::string_view baseUrl, const CoverDownloadIo& io);

}  // namespace nl
