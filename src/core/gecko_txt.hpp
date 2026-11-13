#pragma once
// Gecko-Codelisten im Textformat (Spec v2 8): Zeile 1 ID6, Zeile 2 Titel, danach Blöcke getrennt durch Leerzeilen:
// Name (optional „[Autor]“), Codezeilen „XXXXXXXX YYYYYYYY“, Kommentarzeilen. Kaputte Blöcke werden übersprungen
// und gezählt; X/x in einer Codezeile macht den Code nicht wählbar.

#include <string>
#include <string_view>
#include <vector>

#include "core/result.hpp"
#include "core/types.hpp"

namespace nl {

struct GeckoCode {
    std::string name;
    std::string author;
    std::vector<u32> words;  // Platzhalterstellen als 0
    bool hasPlaceholder = false;
    std::string comment;  // Kommentarzeilen, durch Zeilenumbruch verbunden
};

struct GeckoFile {
    std::string id6;
    std::string title;
    std::vector<GeckoCode> codes;
    u32 skipped = 0;
};

// InvalidData, wenn Zeile 1 keine ID6 ist (leere Datei, HTML-Fehlerseite).
Result<GeckoFile> parseGeckoTxt(std::string_view text);

}  // namespace nl
