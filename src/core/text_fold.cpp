#include "core/text_fold.hpp"

#include "core/utf8.hpp"

namespace nl {

namespace {

// Faltung eines Codepoints: Ergebnis 0–2 Zeichen in out, Rückgabe = Anzahl; 0 = Trenner.
int foldCodepoint(u32 cp, char out[2]) {
    if (cp >= 'A' && cp <= 'Z') cp += 'a' - 'A';
    if ((cp >= 'a' && cp <= 'z') || (cp >= '0' && cp <= '9')) {
        out[0] = static_cast<char>(cp);
        return 1;
    }
    if (cp >= 0xC0 && cp <= 0xFF) {
        // Latin-1-Buchstaben → ASCII; Index cp - 0xC0. '*' = Sonderfall, ' ' = kein Buchstabe.
        static const char kMap[] = "aaaaaa*ceeeeiiiidnooooo ouuuuyt*aaaaaa*ceeeeiiiidnooooo ouuuuyty";
        const char m = kMap[cp - 0xC0];
        if (m == ' ') return 0;
        if (m == '*') {
            if (cp == 0xC6 || cp == 0xE6) { out[0] = 'a'; out[1] = 'e'; return 2; }  // Æ æ
            out[0] = 's'; out[1] = 's'; return 2;                                     // ß
        }
        if ((cp == 0xDE || cp == 0xFE)) { out[0] = 't'; out[1] = 'h'; return 2; }   // Þ þ
        out[0] = m;
        return 1;
    }
    return 0;
}

}  // namespace

std::string cleanTitle(std::string_view stem) {
    std::string out;
    out.reserve(stem.size());
    for (std::size_t i = 0; i < stem.size(); ++i) {
        const char c = stem[i];
        if (c == '(' || c == '[') {
            const std::size_t close = stem.find(c == '(' ? ')' : ']', i + 1);
            if (close != std::string_view::npos) {
                i = close;
                out += ' ';
                continue;
            }
        }
        out += c;
    }
    std::string collapsed;
    for (char c : out) {
        if (c == ' ' && (collapsed.empty() || collapsed.back() == ' ')) continue;
        collapsed += c;
    }
    while (!collapsed.empty() && collapsed.back() == ' ') collapsed.pop_back();
    if (!collapsed.empty()) return collapsed;
    std::size_t b = 0, e = stem.size();
    while (b < e && stem[b] == ' ') ++b;
    while (e > b && stem[e - 1] == ' ') --e;
    return std::string(stem.substr(b, e - b));
}

std::string foldForSearch(std::string_view utf8) {
    std::string out;
    out.reserve(utf8.size());
    bool pendingSpace = false;
    for (std::size_t pos = 0; pos < utf8.size();) {
        const std::size_t before = pos;
        u32 cp = decodeUtf8(utf8, pos);
        if (cp == '?' && utf8[before] != '?') cp = 0;  // ungültiges UTF-8 entfällt
        char f[2];
        const int n = cp == 0 ? 0 : foldCodepoint(cp, f);
        if (n == 0) {
            pendingSpace = !out.empty();
            continue;
        }
        if (pendingSpace) out += ' ';
        pendingSpace = false;
        out.append(f, static_cast<std::size_t>(n));
    }
    return out;
}

bool matchesQuery(std::string_view title, std::string_view query) {
    std::size_t pos = 0;
    while (pos < query.size()) {
        while (pos < query.size() && query[pos] == ' ') ++pos;
        const std::size_t end = query.find(' ', pos);
        const std::string_view word = query.substr(pos, end == std::string_view::npos ? query.size() - pos : end - pos);
        if (!word.empty() && title.find(word) == std::string_view::npos) return false;
        if (end == std::string_view::npos) break;
        pos = end;
    }
    return true;
}

char letterBucket(std::string_view title) {
    const std::string f = foldForSearch(title);
    if (!f.empty() && f[0] >= 'a' && f[0] <= 'z') return static_cast<char>(f[0] - 'a' + 'A');
    return '#';
}

int firstIndexForBucket(std::span<const Game* const> games, char bucket) {
    if (games.empty()) return -1;
    auto rank = [](char b) { return b == '#' ? 0 : b - 'A' + 1; };
    int best = -1, bestRank = 1000;
    for (std::size_t i = 0; i < games.size(); ++i) {
        const int r = rank(letterBucket(games[i]->title));
        if (r == rank(bucket)) return static_cast<int>(i);
        if (r > rank(bucket) && r < bestRank) {
            bestRank = r;
            best = static_cast<int>(i);
        }
    }
    return best >= 0 ? best : static_cast<int>(games.size()) - 1;
}

int compareTitles(std::string_view a, std::string_view b) {
    const int c = foldForSearch(a).compare(foldForSearch(b));
    if (c != 0) return c;
    return a.compare(b);
}

}  // namespace nl
