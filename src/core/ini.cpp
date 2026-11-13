#include "core/ini.hpp"

#include <algorithm>

namespace nl {

std::string_view trim(std::string_view s) {
    std::size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r')) --e;
    return s.substr(b, e - b);
}

IniDoc IniDoc::parse(std::string_view text) {
    IniDoc doc;
    if (text.substr(0, 3) == "\xEF\xBB\xBF") text.remove_prefix(3);
    Section* current = nullptr;
    while (!text.empty()) {
        const std::size_t nl = text.find('\n');
        const std::string_view raw = text.substr(0, nl);
        text = nl == std::string_view::npos ? std::string_view{} : text.substr(nl + 1);

        const std::string_view line = trim(raw);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        const std::size_t eq = line.find('=');
        if (line.front() == '[' && line.back() == ']' && eq == std::string_view::npos) {
            const std::string_view name = trim(line.substr(1, line.size() - 2));
            current = name.empty() ? nullptr : &doc.findOrAdd(name);
            continue;
        }
        if (current == nullptr) continue;  // vor dem ersten gültigen Abschnitt
        current->raw.emplace_back(line);
        if (eq == std::string_view::npos) {
            current->lines.emplace_back(line);
            continue;
        }
        const std::string_view key = trim(line.substr(0, eq));
        if (key.empty()) continue;
        current->entries.emplace_back(std::string(key), std::string(trim(line.substr(eq + 1))));
    }
    return doc;
}

const IniDoc::Section* IniDoc::find(std::string_view name) const {
    for (const Section& s : sections_)
        if (s.name == name) return &s;
    return nullptr;
}

IniDoc::Section& IniDoc::findOrAdd(std::string_view name) {
    for (Section& s : sections_)
        if (s.name == name) return s;
    sections_.push_back(Section{std::string(name), {}, {}, {}});
    return sections_.back();
}

std::string_view IniDoc::get(std::string_view section, std::string_view key, std::string_view fallback) const {
    const Section* s = find(section);
    if (s == nullptr) return fallback;
    for (const auto& [k, v] : s->entries)
        if (k == key) return v;
    return fallback;
}

void IniDoc::set(std::string_view section, std::string_view key, std::string_view value) {
    Section& s = findOrAdd(section);
    for (auto& [k, v] : s.entries) {
        if (k == key) {
            v = std::string(value);
            return;
        }
    }
    s.entries.emplace_back(std::string(key), std::string(value));
}

std::span<const std::string> IniDoc::lines(std::string_view section) const {
    const Section* s = find(section);
    return s != nullptr ? std::span<const std::string>(s->lines) : std::span<const std::string>{};
}

std::vector<std::string_view> IniDoc::sectionNames() const {
    std::vector<std::string_view> out;
    out.reserve(sections_.size());
    for (const Section& s : sections_) out.emplace_back(s.name);
    return out;
}

void IniDoc::addLine(std::string_view section, std::string_view line) {
    Section& s = findOrAdd(section);
    s.lines.emplace_back(line);
    s.raw.emplace_back(line);
}

void IniDoc::removeSection(std::string_view section) {
    std::erase_if(sections_, [section](const Section& s) { return s.name == section; });
}

std::span<const std::string> IniDoc::rawLines(std::string_view section) const {
    const Section* s = find(section);
    return s != nullptr ? std::span<const std::string>(s->raw) : std::span<const std::string>{};
}

std::span<const std::pair<std::string, std::string>> IniDoc::entries(std::string_view section) const {
    const Section* s = find(section);
    return s != nullptr ? std::span<const std::pair<std::string, std::string>>(s->entries)
                        : std::span<const std::pair<std::string, std::string>>{};
}

std::string IniDoc::serialize() const {
    std::string out;
    for (std::size_t i = 0; i < sections_.size(); ++i) {
        const Section& s = sections_[i];
        if (i > 0) out += '\n';
        out += '[';
        out += s.name;
        out += "]\n";
        for (const auto& [k, v] : s.entries) {
            out += k;
            out += " = ";
            out += v;
            out += '\n';
        }
        for (const std::string& l : s.lines) {
            out += l;
            out += '\n';
        }
    }
    return out;
}

namespace {

char lowerAscii(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

bool equalsNoCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (lowerAscii(a[i]) != lowerAscii(b[i])) return false;
    return true;
}

// Klein, getrimmt, Leerraum zwischen Wörtern zu einem Leerzeichen.
std::string normalSection(std::string_view s) {
    std::string out;
    bool space = false;
    for (char c : trim(s)) {
        if (c == ' ' || c == '\t') {
            space = true;
            continue;
        }
        if (space) out += ' ';
        space = false;
        out += lowerAscii(c);
    }
    return out;
}

// Zeilen samt Zeilenende; die letzte darf ohne sein.
std::vector<std::string_view> splitLines(std::string_view text) {
    std::vector<std::string_view> out;
    while (!text.empty()) {
        const std::size_t nl = text.find('\n');
        const std::size_t len = nl == std::string_view::npos ? text.size() : nl + 1;
        out.push_back(text.substr(0, len));
        text.remove_prefix(len);
    }
    return out;
}

std::string_view body(std::string_view line) {
    if (!line.empty() && line.back() == '\n') line.remove_suffix(1);
    return trim(line);
}

bool headerName(std::string_view line, std::string_view& name) {
    const std::string_view t = body(line);
    if (t.size() < 2 || t.front() != '[' || t.back() != ']' || t.find('=') != std::string_view::npos) return false;
    name = t.substr(1, t.size() - 2);
    return true;
}

bool isContent(std::string_view line) {
    const std::string_view t = body(line);
    return !t.empty() && t.front() != ';' && t.front() != '#';
}

bool hasKey(std::string_view line, std::string_view key) {
    const std::string_view t = body(line);
    const std::size_t eq = t.find('=');
    return isContent(line) && eq != std::string_view::npos && equalsNoCase(trim(t.substr(0, eq)), key);
}

std::string_view eolOf(std::string_view text) { return text.find("\r\n") != std::string_view::npos ? "\r\n" : "\n"; }

std::string_view lineEol(std::string_view line, std::string_view fallback) {
    if (line.size() >= 2 && line.substr(line.size() - 2) == "\r\n") return "\r\n";
    if (!line.empty() && line.back() == '\n') return "\n";
    return fallback;
}

}  // namespace

std::string iniTextSet(std::string_view text, std::string_view section, std::string_view key,
                       std::optional<std::string_view> value) {
    const std::vector<std::string_view> lines = splitLines(text);
    const std::string want = normalSection(section);
    const std::string_view eol = eolOf(text);
    std::vector<std::size_t> hits;
    std::size_t anchor = lines.size();  // letzte Inhaltszeile (oder Kopf) des letzten passenden Abschnitts
    bool in = false;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        std::string_view name;
        if (headerName(lines[i], name)) {
            in = normalSection(name) == want;
            if (in) anchor = i;
            continue;
        }
        if (!in) continue;
        if (isContent(lines[i])) anchor = i;
        if (hasKey(lines[i], key)) hits.push_back(i);
    }
    const auto entry = [&](std::string_view end) {
        std::string s(key);
        s += " = ";
        s += *value;
        s += end;
        return s;
    };

    std::string out;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const bool hit = std::find(hits.begin(), hits.end(), i) != hits.end();
        if (hit && value && i == hits.back()) out += entry(lineEol(lines[i], ""));
        else if (!hit) out += lines[i];
        if (value && hits.empty() && i == anchor) {
            if (lineEol(lines[i], "").empty()) out += eol;
            out += entry(eol);
        }
    }
    if (value && hits.empty() && anchor == lines.size()) {
        if (!out.empty() && out.back() != '\n') out += eol;
        const bool blankBefore = out.empty() || out.ends_with("\n\n") || out.ends_with("\r\n\r\n");
        if (!blankBefore) out += eol;
        out += "[";
        out += section;
        out += "]";
        out += eol;
        out += entry(eol);
    }
    return out;
}

std::string iniTextRemoveSection(std::string_view text, std::string_view section) {
    const std::string want = normalSection(section);
    std::string out;
    bool in = false;
    for (std::string_view line : splitLines(text)) {
        std::string_view name;
        if (headerName(line, name)) in = normalSection(name) == want;
        if (!in) out += line;
    }
    return out;
}

}  // namespace nl
