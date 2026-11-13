#pragma once
// INI-Dateien nach Spec 5.4: Schlüssel vor dem ersten „=", beides getrimmt, Kommentare nur als
// ganze Zeile mit ; oder #. Zeilen ohne „=" bleiben als Listeneinträge erhalten (Favoriten).

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace nl {

class IniDoc {
public:
    static IniDoc parse(std::string_view text);

    std::string_view get(std::string_view section, std::string_view key, std::string_view fallback = {}) const;
    void set(std::string_view section, std::string_view key, std::string_view value);
    std::span<const std::string> lines(std::string_view section) const;
    // Alle Inhaltszeilen (getrimmt, ohne Kommentare) in Dateireihenfolge – auch solche mit „=".
    std::span<const std::string> rawLines(std::string_view section) const;
    // Alle Schlüssel-Wert-Paare in Dateireihenfolge, gleiche Schlüssel mehrfach.
    std::span<const std::pair<std::string, std::string>> entries(std::string_view section) const;
    void addLine(std::string_view section, std::string_view line);
    // Entfernt den Abschnitt (exakter Name) samt Inhalt; ein fehlender Abschnitt ist kein Fehler.
    void removeSection(std::string_view section);
    // Namen aller Abschnitte in Dateireihenfolge (gleichnamige zusammengefasst); gilt, solange das Dokument lebt.
    std::vector<std::string_view> sectionNames() const;
    std::string serialize() const;

private:
    struct Section {
        std::string name;
        std::vector<std::pair<std::string, std::string>> entries;
        std::vector<std::string> lines;
        std::vector<std::string> raw;
    };
    const Section* find(std::string_view name) const;
    Section& findOrAdd(std::string_view name);

    std::vector<Section> sections_;
};

std::string_view trim(std::string_view s);

// Textbearbeitung für Dateien, die Menschen pflegen (M14): Alles außerhalb der berührten Zeilen bleibt Byte für
// Byte, auch Kommentare und Zeilenenden. Abschnitte passen ohne Groß-/Kleinschreibung und mit beliebigem
// Leerraum zwischen Wörtern („[game  rmgp01]“ = „game RMGP01“), Schlüssel ohne Groß-/Kleinschreibung.
// iniTextSet: der letzte Eintrag des Schlüssels wird ersetzt, frühere entfallen; fehlt er, folgt er der letzten
// Inhaltszeile des letzten passenden Abschnitts, ohne Abschnitt kommt einer ans Ende. nullopt löscht.
std::string iniTextSet(std::string_view text, std::string_view section, std::string_view key,
                       std::optional<std::string_view> value);
// Entfernt jeden passenden Abschnitt samt Kopfzeile bis zum nächsten Abschnitt.
std::string iniTextRemoveSection(std::string_view text, std::string_view section);

}  // namespace nl
