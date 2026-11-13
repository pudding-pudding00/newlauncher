#pragma once
// Modell der Cheat-Szene (M15 Task 4): Codes eines Spiels aus <ID6>.txt mit Haken aus <ID6>.sel. Wählbar ist ein
// Code ohne Platzhalter und nur beim ersten Vorkommen seines Namens, weil die .sel Namen speichert. Unbekannte
// Namen der .sel fallen still weg.

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "core/console.hpp"
#include "core/gecko_txt.hpp"
#include "core/types.hpp"

namespace nl {

enum class CheatStatus : u8 { NoFile, Ready, WiiUnavailable };

class CheatModel {
public:
    // txt: Inhalt der Codeliste (leer = keine Datei), sel: Inhalt der Auswahl. wiiCheats: der Build bringt einen
    // Codehandler mit (Launcher-Define NL_WII_CHEATS); sonst zeigt eine Wii-Liste nur „nicht verfügbar“.
    CheatModel(Console console, std::string_view txt, std::string_view sel, bool wiiCheats);

    CheatStatus status() const { return status_; }
    std::size_t rows() const { return rows_.size(); }
    const std::string& title() const { return file_.title; }
    u32 skipped() const { return file_.skipped; }

    const std::string& label(std::size_t r) const;
    const std::string& author(std::size_t r) const;
    bool checked(std::size_t r) const;
    bool selectable(std::size_t r) const;
    bool toggle(std::size_t r);  // true = geändert
    bool dirty() const { return dirty_; }

    std::vector<std::string> selection() const;  // Dateireihenfolge
    std::string selText() const;

private:
    struct Row {
        std::size_t code;
        bool selectable;
        bool checked;
    };
    GeckoFile file_;
    std::vector<Row> rows_;
    CheatStatus status_ = CheatStatus::NoFile;
    bool dirty_ = false;
};

}  // namespace nl
