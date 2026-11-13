#include "core/categories.hpp"

#include <algorithm>

#include "core/ini.hpp"

namespace nl {

namespace {
struct Line {
    std::size_t begin, end, next;  // Inhalt [begin, end), nächste Zeile ab next
};

std::vector<Line> splitLines(std::string_view text) {
    std::vector<Line> out;
    std::size_t pos = 0;
    while (pos < text.size()) {
        const std::size_t nl = text.find('\n', pos);
        const std::size_t next = nl == std::string_view::npos ? text.size() : nl + 1;
        std::size_t end = nl == std::string_view::npos ? text.size() : nl;
        if (end > pos && text[end - 1] == '\r') --end;
        out.push_back(Line{pos, end, next});
        pos = next;
    }
    return out;
}

bool headerOf(std::string_view line, std::string_view& name) {
    const std::string_view t = trim(line);
    if (t.size() < 2 || t.front() != '[' || t.back() != ']') return false;
    name = trim(t.substr(1, t.size() - 2));
    return true;
}

bool contentOf(std::string_view line, std::string_view& key) {
    key = trim(line);
    return !key.empty() && key.front() != ';' && key.front() != '#';
}

bool printable(std::string_view s) {
    return !s.empty() && s.find_first_of("\r\n") == std::string_view::npos && trim(s) == s;
}
}  // namespace

Categories Categories::fromText(std::string_view text) {
    Categories c;
    c.text_.assign(text);
    c.reparse();
    return c;
}

void Categories::reparse() {
    names_.clear();
    keys_.clear();
    const std::string_view text(text_);
    int current = -1;  // Index in names_, -1 = außerhalb/ignoriert
    for (const Line& l : splitLines(text)) {
        const std::string_view line = text.substr(l.begin, l.end - l.begin);
        std::string_view name;
        if (headerOf(line, name)) {
            auto it = std::find(names_.begin(), names_.end(), name);
            if (it != names_.end()) current = static_cast<int>(it - names_.begin());
            else if (names_.size() < kMaxCategories) {
                names_.emplace_back(name);
                keys_.emplace_back();
                current = static_cast<int>(names_.size() - 1);
            } else current = -1;
            continue;
        }
        std::string_view key;
        if (current >= 0 && contentOf(line, key)) keys_[static_cast<std::size_t>(current)].emplace_back(key);
    }
}

bool Categories::contains(std::string_view cat, std::string_view key) const {
    auto it = std::find(names_.begin(), names_.end(), cat);
    if (it == names_.end()) return false;
    const auto& keys = keys_[static_cast<std::size_t>(it - names_.begin())];
    return std::find(keys.begin(), keys.end(), key) != keys.end();
}

bool Categories::set(std::string_view cat, std::string_view key, bool on) {
    const bool known = std::find(names_.begin(), names_.end(), cat) != names_.end();
    if (on == contains(cat, key)) return known || !on;
    const std::string_view text(text_);
    const std::vector<Line> lines = splitLines(text);
    const std::string eol = text.find("\r\n") != std::string_view::npos ? "\r\n" : "\n";

    if (!on) {
        std::string out;
        bool inCat = false;
        for (const Line& l : lines) {
            const std::string_view line = text.substr(l.begin, l.end - l.begin);
            std::string_view name, k;
            if (headerOf(line, name)) inCat = name == cat;
            else if (inCat && contentOf(line, k) && k == key) continue;
            out.append(text.substr(l.begin, l.next - l.begin));
        }
        text_ = std::move(out);
        reparse();
        return true;
    }

    if (!printable(key) || key.front() == ';' || key.front() == '#' || key.front() == '[') return false;
    if (!known) {
        if (names_.size() >= kMaxCategories || !printable(cat) || cat.find_first_of("[]") != std::string_view::npos)
            return false;
        std::string out(text);
        if (!out.empty() && out.back() != '\n') out += eol;
        if (!out.empty()) out += eol;
        out.append("[").append(cat).append("]").append(eol).append(key).append(eol);
        text_ = std::move(out);
        reparse();
        return true;
    }

    // Nach der letzten Inhaltszeile des letzten Abschnitts dieser Kategorie, sonst direkt nach dessen Kopf.
    std::size_t insertAfter = 0;
    bool inCat = false;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::string_view line = text.substr(lines[i].begin, lines[i].end - lines[i].begin);
        std::string_view name, k;
        if (headerOf(line, name)) {
            inCat = name == cat;
            if (inCat) insertAfter = i;
        } else if (inCat && contentOf(line, k)) {
            insertAfter = i;
        }
    }
    const Line& l = lines[insertAfter];
    std::string out(text.substr(0, l.next));
    if (l.next == l.end) out += eol;  // letzte Zeile ohne Zeilenende
    out.append(key).append(eol);
    out.append(text.substr(l.next));
    text_ = std::move(out);
    reparse();
    return true;
}

}  // namespace nl
