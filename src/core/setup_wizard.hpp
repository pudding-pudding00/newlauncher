#pragma once
// Einrichtungs-Assistent (M13, Spec v2 6.4): Schritt-Logik ohne Grafik und ohne Netz. Jeder Schritt hat eine
// Liste von Optionen; der Vorschlag ist vorausgewählt. accept() übernimmt die Auswahl, skip() den Standardwert.

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/config.hpp"
#include "core/console.hpp"
#include "core/sysinfo.hpp"

namespace nl {

struct DeviceGames {
    std::string device;                     // "usb", "usb2", "sd"
    std::array<u32, kConsoleCount> games{};  // Index = Console
    u32 total() const;
};

enum class WizardStep : u8 { Language, Sources, Cios, Emulators, Network, Done };
inline constexpr int kWizardStepCount = 6;
const char* wizardStepKey(WizardStep s);  // "language" … "done" (Log NL_WIZARD)

class SetupWizard {
public:
    SetupWizard(const SystemInfo& sys, const Config& current, std::span<const DeviceGames> gamesPerDevice);

    WizardStep step() const { return step_; }
    void accept();  // Auswahl übernehmen, weiter (in Done ohne Wirkung)
    void skip();    // Standardwert, weiter
    void back();    // einen Schritt zurück; im ersten bleibt er

    void select(int delta);  // Auswahl im aktuellen Schritt, mit Umlauf
    std::size_t selected() const { return sel_[index()]; }
    std::size_t suggested() const { return sug_[index()]; }
    std::vector<std::string> options(bool german) const;
    bool installSelected() const;  // Schritt Emulatoren: „Fehlende installieren“ gewählt (M10-Szene statt weiter)
    std::size_t missingEmulators() const { return missing_; }

    Config result() const;  // current mit Sprache, [library] sources und [wii] ios aus den Schritten
    // emulators.ini mit den erkannten Pfaden, nur wenn „festschreiben“ gewählt wurde.
    std::optional<std::string> emulatorsIni() const;

private:
    enum class EmuChoice : u8 { Keep, Freeze, Install };
    std::size_t index() const { return static_cast<std::size_t>(step_); }
    std::size_t count(std::size_t stepIndex) const;

    WizardStep step_ = WizardStep::Language;
    Config current_;
    std::vector<DeviceGames> games_;
    std::vector<ResolvedEmuSummary> detected_;
    std::vector<ResolvedEmuSummary> ownIni_;  // how == "ini": eigene Abschnitte, festschreiben übernimmt sie
    std::size_t missing_ = 0;
    std::vector<Config::Language> langs_;
    std::vector<SourceSpec> sources_;
    std::vector<std::optional<u8>> ios_;
    std::vector<CiosEntry> iosInfo_;  // parallel zu ios_ ab Index 1
    std::vector<EmuChoice> emus_;
    std::array<std::size_t, kWizardStepCount> sel_{}, sug_{}, def_{};
};

std::string backupName(std::string_view path);  // "x/config.ini" → "x/config.ini.bak"

}  // namespace nl
