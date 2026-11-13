#pragma once
// Modell der Options-Szene (Spec 7): eine Zeile je Option einer Konsole, pro Spiel ([game <ID6>] in games.ini)
// oder global ([wii]/[gc] in config.ini). Es arbeitet auf dem Dateitext statt auf IniDoc, damit Kommentare und
// fremde Abschnitte (N64-Methode, Emulator-Profile) beim Speichern Byte für Byte bleiben. Texte für die Anzeige
// setzt die Szene zusammen; das Modell liefert normalisierte Werte.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/console.hpp"
#include "core/game_options.hpp"
#include "core/nin_cfg.hpp"

namespace nl {

class OptionsModel {
public:
    // id6 leer = globale Standards. nin = erkannte Nintendont-Version; nullopt sperrt keine GC-Option.
    OptionsModel(Console c, std::string id6, std::string configText, std::string gamesText,
                 std::optional<NinVersion> nin = std::nullopt);

    Console console() const { return console_; }
    const std::string& id6() const { return id6_; }
    bool perGame() const { return !id6_.empty(); }

    std::size_t rows() const { return rows_.size(); }
    std::string_view key(std::size_t r) const { return rows_[r].key; }
    bool inherited(std::size_t r) const { return rows_[r].inherited; }
    // Wirksamer Wert; geerbt: der globale Wert bzw. Standard.
    const std::string& shownValue(std::size_t r) const;
    // Wählbare Werte in Anzeigereihenfolge; pro Spiel steht „default“ an Position 0. ios: auto, 200–255.
    std::vector<std::string> choices(std::size_t r) const;
    std::size_t choice(std::size_t r) const;  // Position des aktuellen Werts in choices()

    bool enabled(std::size_t r) const { return !requiredVersion(r).has_value(); }
    // Nintendont-Version, ab der die Option wirkt, wenn die erkannte älter ist; sonst nullopt.
    std::optional<NinVersion> requiredVersion(std::size_t r) const;

    void step(std::size_t r, int dir);  // zyklisch; gesperrte Zeilen bleiben
    bool set(std::string_view key, std::string_view value);  // „default“ erbt wieder; false bei Unbekanntem
    void reset();  // pro Spiel: Abschnitt [game <ID6>] weg; global: [wii] bzw. [gc] weg

    bool dirty() const { return config_ != config0_ || games_ != games0_; }
    const std::string& configIniText() const { return config_; }
    const std::string& gamesIniText() const { return games_; }

private:
    std::string section() const;
    void write(std::string_view key, std::optional<std::string_view> value);
    void refresh();

    Console console_;
    std::string id6_;
    std::string config_, games_, config0_, games0_;
    std::optional<NinVersion> nin_;
    std::vector<OptionValue> rows_;
};

}  // namespace nl
