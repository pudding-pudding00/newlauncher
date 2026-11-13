#include "core/gecko_txt.hpp"

#include <utility>

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

// Ein Codewort: genau 8 Zeichen aus Hex oder X/x. X wird zu 0 und setzt placeholder.
bool parseWord(std::string_view tok, u32& out, bool& placeholder) {
    if (tok.size() != 8) return false;
    u32 v = 0;
    for (char c : tok) {
        int h = hexValue(c);
        if (c == 'X' || c == 'x') {
            h = 0;
            placeholder = true;
        }
        if (h < 0) return false;
        v = (v << 4) | static_cast<u32>(h);
    }
    out = v;
    return true;
}

std::string_view firstToken(std::string_view line) {
    size_t i = 0;
    while (i < line.size() && !isSpace(line[i])) ++i;
    return line.substr(0, i);
}

// Beginnt die Zeile wie eine Codezeile (erstes Token 8 Hex/X)?
bool looksLikeCode(std::string_view line) {
    u32 v = 0;
    bool ph = false;
    return parseWord(firstToken(line), v, ph);
}

// Genau zwei Codewörter, sonst false.
bool parseCodeLine(std::string_view line, GeckoCode& code) {
    std::string_view toks[3];
    size_t n = 0;
    size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && isSpace(line[i])) ++i;
        if (i >= line.size()) break;
        size_t j = i;
        while (j < line.size() && !isSpace(line[j])) ++j;
        if (n == 3) return false;
        toks[n++] = line.substr(i, j - i);
        i = j;
    }
    if (n != 2) return false;
    u32 a = 0;
    u32 b = 0;
    bool ph = false;
    if (!parseWord(toks[0], a, ph) || !parseWord(toks[1], b, ph)) return false;
    code.words.push_back(a);
    code.words.push_back(b);
    code.hasPlaceholder = code.hasPlaceholder || ph;
    return true;
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
            if (!parseCodeLine(line, cur)) broken = true;
            continue;
        }
        if (!cur.comment.empty()) cur.comment += '\n';
        cur.comment.append(line);
    }
    finish();
    return Result<GeckoFile>::ok(std::move(file));
}

}  // namespace nl
