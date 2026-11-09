#include "core/ini.hpp"

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

void IniDoc::addLine(std::string_view section, std::string_view line) {
    Section& s = findOrAdd(section);
    s.lines.emplace_back(line);
    s.raw.emplace_back(line);
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

}  // namespace nl
