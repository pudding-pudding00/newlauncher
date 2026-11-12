#include "core/setup_wizard.hpp"

#include "core/emulators.hpp"

namespace nl {

namespace {

constexpr std::array<const char*, 8> kEmuSections{"nes", "snes", "gb", "gbc", "gba", "n64_not64", "n64_wii64",
                                                  "nintendont"};

std::string devicesLabel(const SourceSpec& s, std::span<const DeviceGames> games, bool de) {
    if (s.autoMode) return de ? "Alle Geräte (automatisch)" : "All devices (automatic)";
    std::string l;
    for (const std::string& d : s.devices) {
        if (!l.empty()) l += ", ";
        l += d;
        for (const DeviceGames& g : games)
            if (g.device == d) l += " (" + std::to_string(g.total()) + (de ? " Spiele)" : " games)");
    }
    return l;
}

}  // namespace

u32 DeviceGames::total() const {
    u32 n = 0;
    for (u32 g : games) n += g;
    return n;
}

const char* wizardStepKey(WizardStep s) {
    switch (s) {
        case WizardStep::Language: return "language";
        case WizardStep::Sources: return "sources";
        case WizardStep::Cios: return "cios";
        case WizardStep::Emulators: return "emulators";
        case WizardStep::Network: return "network";
        default: return "done";
    }
}

SetupWizard::SetupWizard(const SystemInfo& sys, const Config& current, std::span<const DeviceGames> gamesPerDevice)
    : current_(current), games_(gamesPerDevice.begin(), gamesPerDevice.end()) {
    // Sprache: Vorschlag ist die bisherige Wahl (beim ersten Start Systemsprache), Standard Systemsprache.
    langs_ = {Config::Language::Auto, Config::Language::De, Config::Language::En};
    sug_[0] = static_cast<std::size_t>(current.language);

    // Quellen: Vorschlag = die Geräte mit Spielen in Gerätereihenfolge; ohne solche Geräte auto.
    SourceSpec withGames{false, {}};
    for (const DeviceGames& g : gamesPerDevice)
        if (g.total() > 0) withGames.devices.push_back(g.device);
    if (!withGames.devices.empty()) sources_.push_back(withGames);
    sources_.push_back(SourceSpec{});
    def_[1] = sources_.size() - 1;
    if (withGames.devices.size() > 1)
        for (const std::string& d : withGames.devices) sources_.push_back(SourceSpec{false, {d}});
    if (!current.libraryRules.sources.autoMode) {
        bool known = false;
        for (const SourceSpec& s : sources_) known = known || s == current.libraryRules.sources;
        if (!known) sources_.push_back(current.libraryRules.sources);
    }
    sug_[1] = 0;

    // cIOS: Automatik plus jeder brauchbare Slot (keine Stubs); Vorschlag = bisherige Wahl, sonst Automatik.
    ios_.push_back(std::nullopt);
    for (const CiosEntry& c : sys.cios) {
        if (c.kind == CiosKind::Stub) continue;
        ios_.push_back(c.slot);
        iosInfo_.push_back(c);
        if (current.wiiIos == c.slot) sug_[2] = ios_.size() - 1;
    }

    // Emulatoren: erkannte verwenden; festschreiben nur bei erkannten; installieren nur bei fehlenden.
    for (const ResolvedEmuSummary& e : sys.emulators)
        if (e.how == "detected") detected_.push_back(e);
        else if (e.how == "ini") ownIni_.push_back(e);  // festschreiben behält eigene Abschnitte
    for (const char* sec : kEmuSections) {
        bool found = false;
        for (const ResolvedEmuSummary& e : sys.emulators) found = found || (e.section == sec && e.how != "default");
        if (!found) ++missing_;
    }
    emus_.push_back(EmuChoice::Keep);
    if (!detected_.empty()) emus_.push_back(EmuChoice::Freeze);
    if (missing_ > 0) emus_.push_back(EmuChoice::Install);

    sel_ = sug_;
}

std::size_t SetupWizard::count(std::size_t i) const {
    switch (static_cast<WizardStep>(i)) {
        case WizardStep::Language: return langs_.size();
        case WizardStep::Sources: return sources_.size();
        case WizardStep::Cios: return ios_.size();
        case WizardStep::Emulators: return emus_.size();
        default: return 1;
    }
}

void SetupWizard::accept() {
    if (step_ == WizardStep::Done) return;
    step_ = static_cast<WizardStep>(index() + 1);
}

void SetupWizard::skip() {
    if (step_ == WizardStep::Done) return;
    sel_[index()] = def_[index()];
    accept();
}

void SetupWizard::back() {
    if (step_ != WizardStep::Language) step_ = static_cast<WizardStep>(index() - 1);
}

void SetupWizard::select(int delta) {
    const auto n = static_cast<int>(count(index()));
    int v = (static_cast<int>(sel_[index()]) + delta) % n;
    if (v < 0) v += n;
    sel_[index()] = static_cast<std::size_t>(v);
}

bool SetupWizard::installSelected() const {
    return step_ == WizardStep::Emulators && emus_[sel_[index()]] == EmuChoice::Install;
}

std::vector<std::string> SetupWizard::options(bool de) const {
    std::vector<std::string> out;
    switch (step_) {
        case WizardStep::Language:
            out = {de ? "Systemsprache" : "System language", "Deutsch", "English"};
            break;
        case WizardStep::Sources:
            for (const SourceSpec& s : sources_) out.push_back(devicesLabel(s, games_, de));
            break;
        case WizardStep::Cios:
            out.push_back(de ? "Automatisch" : "Automatic");
            for (const CiosEntry& c : iosInfo_) {
                std::string l = "IOS" + std::to_string(c.slot) + " " + ciosKindName(c.kind);
                if (c.kind == CiosKind::D2x) l += " base " + std::to_string(c.base);
                out.push_back(l);
            }
            break;
        case WizardStep::Emulators:
            for (EmuChoice e : emus_) {
                if (e == EmuChoice::Keep)
                    out.push_back((de ? "Erkannte verwenden (" : "Use detected (") + std::to_string(detected_.size()) + ")");
                else if (e == EmuChoice::Freeze)
                    out.push_back(de ? "In emulators.ini festschreiben" : "Write to emulators.ini");
                else
                    out.push_back((de ? "Fehlende installieren (" : "Install missing (") + std::to_string(missing_) + ")");
            }
            break;
        case WizardStep::Network:
            out.push_back(de ? "Weiter" : "Continue");
            break;
        default:
            out.push_back(de ? "Speichern" : "Save");
            break;
    }
    return out;
}

Config SetupWizard::result() const {
    Config c = current_;
    c.language = langs_[sel_[0]];
    c.libraryRules.sources = sources_[sel_[1]];
    c.wiiIos = ios_[sel_[2]];
    return c;
}

std::optional<std::string> SetupWizard::emulatorsIni() const {
    if (emus_[sel_[3]] != EmuChoice::Freeze) return std::nullopt;
    std::string t = defaultEmulatorsText();
    auto add = [&t](const ResolvedEmuSummary& e) {
        if (!t.empty() && t.back() != '\n') t += '\n';
        t += "\n[" + e.section + "]\ndol = " + e.path + "\nargs = " + e.args + "\n";
    };
    for (const ResolvedEmuSummary& e : ownIni_) add(e);  // eigene Abschnitte bleiben unverändert
    for (const ResolvedEmuSummary& e : detected_) add(e);
    return t;
}

std::string backupName(std::string_view path) { return std::string(path) + ".bak"; }

}  // namespace nl
