#pragma once
// Titelbereinigung (No-Intro-Tags), Suchfaltung und Anfangsbuchstaben (Spec 3.5, 6.2).

#include <string>
#include <string_view>

namespace nl {

// Entfernt geschlossene (…)- und […]-Gruppen, fasst Leerzeichen zusammen, trimmt.
// Bliebe nichts übrig, wird der getrimmte Originaltext geliefert.
std::string cleanTitle(std::string_view fileStem);

// Kleinbuchstaben, Latin-1-Akzente aufgelöst (ä→a, ß→ss, æ→ae …), alles andere außer a–z/0–9
// wird zu einem Leerzeichen; Leerzeichen zusammengefasst und getrimmt. Ungültiges UTF-8 entfällt.
std::string foldForSearch(std::string_view utf8);

// Jedes Wort der (gefalteten) Anfrage muss als Teilstring im (gefalteten) Titel vorkommen.
bool matchesQuery(std::string_view foldedTitle, std::string_view foldedQuery);

// 'A'–'Z' nach Faltung, sonst '#'.
char letterBucket(std::string_view title);

// Vergleich nach foldForSearch, bei Gleichstand bytewise; Artikel zählen mit (Spec 6.2).
int compareTitles(std::string_view a, std::string_view b);

}  // namespace nl
