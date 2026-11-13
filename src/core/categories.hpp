#pragma once
// Eigene Kategorien (M17, Spec 10.3): <data>/categories.ini, [<Kategorie>] mit einem Spiel-Schlüssel pro Zeile.
// Die Datei pflegen auch Menschen: Änderungen berühren nur die betroffenen Zeilen, Kommentare bleiben.

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/types.hpp"

namespace nl {

inline constexpr std::size_t kMaxCategories = 64;

class Categories {
public:
    static Categories fromText(std::string_view text);  // weitere Kategorien über kMaxCategories werden ignoriert
    std::span<const std::string> names() const { return names_; }  // Dateireihenfolge, gleichnamige zusammengefasst
    bool contains(std::string_view cat, std::string_view key) const;
    // Fügt hinzu (ans Ende der Kategorie, neue Kategorie ans Dateiende) oder entfernt jede Zeile des Schlüssels.
    // false, wenn es nicht geht: 64 Kategorien voll, Name oder Schlüssel nicht als Zeile darstellbar.
    bool set(std::string_view cat, std::string_view key, bool on);
    const std::string& toText() const { return text_; }

private:
    void reparse();

    std::string text_;
    std::vector<std::string> names_;
    std::vector<std::vector<std::string>> keys_;  // parallel zu names_
};

}  // namespace nl
