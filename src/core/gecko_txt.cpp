#include "core/gecko_txt.hpp"

#include <utility>

#include "core/utf8.hpp"

namespace nl {
namespace {

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r'; }

std::string_view trim(std::string_view s) {
    while (!s.empty() && isSpace(s.front())) s.remove_prefix(1);
    while (!s.empty() && isSpace(s.back())) s.remove_suffix(1);
    return s;
}

bool isAlnum(char c) { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }

int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

// Ein Codewort: genau 8 alphanumerische Zeichen. Echte Listen markieren einzutragende Werte mit beliebigen
// Buchstaben (XXXX, KK, YYYYYY, ZZZZzzzz); jedes Nicht-Hex-Zeichen wird zu 0 und setzt placeholder.
bool parseWord(std::string_view tok, u32& out, bool& placeholder) {
    if (tok.size() != 8) return false;
    u32 v = 0;
    for (char c : tok) {
        if (!isAlnum(c)) return false;
        int h = hexValue(c);
        if (h < 0) {
            h = 0;
            placeholder = true;
        }
        v = (v << 4) | static_cast<u32>(h);
    }
    out = v;
    return true;
}

bool hasDigit(std::string_view s) {
    for (char c : s) {
        if (c >= '0' && c <= '9') return true;
    }
    return false;
}

// Bis zu drei Tokens; n ist die Gesamtzahl (höchstens 3 gezählt).
size_t tokens(std::string_view line, std::string_view (&toks)[3]) {
    size_t n = 0;
    size_t i = 0;
    while (i < line.size() && n < 3) {
        while (i < line.size() && isSpace(line[i])) ++i;
        if (i >= line.size()) break;
        size_t j = i;
        while (j < line.size() && !isSpace(line[j])) ++j;
        toks[n++] = line.substr(i, j - i);
        i = j;
    }
    return n;
}

// Beginnt die Zeile wie eine Codezeile? Erstes Token: Codewort mit mindestens einer Ziffer, damit Text wie
// „Infinite Energies“ nicht als Code gilt.
bool looksLikeCode(std::string_view line) {
    std::string_view toks[3];
    if (tokens(line, toks) == 0) return false;
    u32 v = 0;
    bool ph = false;
    return hasDigit(toks[0]) && parseWord(toks[0], v, ph);
}

// Zwei Codewörter, optional gefolgt von einem Kommentar, der nicht mit Buchstabe/Ziffer beginnt
// („[Autor]“, „* …“, „(…)“). Alles andere ist mehrdeutig.
bool parseCodeLine(std::string_view line, GeckoCode& code) {
    std::string_view toks[3];
    const size_t n = tokens(line, toks);
    if (n < 2) return false;
    if (n == 3 && isAlnum(toks[2].front())) return false;
    u32 a = 0;
    u32 b = 0;
    bool ph = false;
    if (!parseWord(toks[0], a, ph) || !parseWord(toks[1], b, ph)) return false;
    code.words.push_back(a);
    code.words.push_back(b);
    code.hasPlaceholder = code.hasPlaceholder || ph;
    return true;
}

// Ungültiges UTF-8 (z. B. „é“ als 0xE9) heißt: die Datei ist Latin-1. decodeUtf8 meldet Fehler als '?'.
bool isValidUtf8(std::string_view s) {
    size_t pos = 0;
    while (pos < s.size()) {
        const size_t start = pos;
        if (decodeUtf8(s, pos) == '?' && s[start] != '?') return false;
    }
    return true;
}

std::string latin1ToUtf8(std::string_view s) {
    std::string out;
    out.reserve(s.size() + s.size() / 8);
    for (char c : s) {
        const auto b = static_cast<unsigned char>(c);
        appendUtf8(out, (b >= 0x80 && b < 0xA0) ? u32{'?'} : u32{b});  // C1-Steuerzeichen nicht übernehmen
    }
    return out;
}

void splitNameAuthor(std::string_view line, GeckoCode& code) {
    if (line.size() >= 2 && line.back() == ']') {
        const size_t open = line.rfind('[');
        if (open != std::string_view::npos) {
            code.author = std::string(trim(line.substr(open + 1, line.size() - open - 2)));
            code.name = std::string(trim(line.substr(0, open)));
            if (!code.name.empty()) return;
            code.author.clear();
        }
    }
    code.name = std::string(line);
}

class LineReader {
public:
    explicit LineReader(std::string_view text) : rest_(text) {}
    bool next(std::string_view& line) {
        if (done_) return false;
        const size_t nl = rest_.find('\n');
        if (nl == std::string_view::npos) {
            line = trim(rest_);
            done_ = true;
            return true;
        }
        line = trim(rest_.substr(0, nl));
        rest_.remove_prefix(nl + 1);
        return true;
    }

private:
    std::string_view rest_;
    bool done_ = false;
};

}  // namespace

Result<GeckoFile> parseGeckoTxt(std::string_view text) {
    if (text.substr(0, 3) == "\xEF\xBB\xBF") text.remove_prefix(3);
    std::string converted;
    if (!isValidUtf8(text)) {
        converted = latin1ToUtf8(text);
        text = converted;
    }
    LineReader reader(text);
    std::string_view line;
    GeckoFile file;

    if (!reader.next(line) || line.size() != 6) {
        return Result<GeckoFile>::err(Error{ErrorCode::InvalidData, "gecko: keine ID6 in Zeile 1"});
    }
    for (char c : line) {
        if (!isAlnum(c)) return Result<GeckoFile>::err(Error{ErrorCode::InvalidData, "gecko: keine ID6 in Zeile 1"});
    }
    file.id6 = std::string(line);
    if (reader.next(line)) file.title = std::string(line);

    GeckoCode cur;
    bool inBlock = false;
    bool broken = false;
    auto finish = [&]() {
        if (!inBlock) return;
        if (broken || cur.words.empty()) {
            ++file.skipped;
        } else {
            file.codes.push_back(std::move(cur));
        }
        cur = GeckoCode{};
        inBlock = false;
        broken = false;
    };

    while (reader.next(line)) {
        if (line.empty()) {
            finish();
            continue;
        }
        if (!inBlock) {
            inBlock = true;
            if (looksLikeCode(line)) {
                broken = true;  // Block ohne Namen
            } else {
                splitNameAuthor(line, cur);
            }
            continue;
        }
        if (broken) continue;
        if (looksLikeCode(line)) {
            if (parseCodeLine(line, cur)) continue;
            cur.hasPlaceholder = true;  // mehrdeutig (Wertetabelle oder halber Code): zeigen, aber nicht wählbar
        }
        if (!cur.comment.empty()) cur.comment += '\n';
        cur.comment.append(line);
    }
    finish();
    return Result<GeckoFile>::ok(std::move(file));
}

}  // namespace nl
