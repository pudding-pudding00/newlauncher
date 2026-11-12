#include "core/library_sources.hpp"

#include <algorithm>
#include <string_view>

namespace nl {
namespace {

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}

// „USB2:“ → „usb2“.
std::string deviceKey(std::string_view s) {
    s = trim(s);
    if (!s.empty() && s.back() == ':') s.remove_suffix(1);
    return lower(s);
}

bool knownDevice(std::string_view d) {
    if (d == "sd" || d == "usb") return true;
    return d.size() == 4 && d.substr(0, 3) == "usb" && d[3] >= '2' && d[3] <= '8';
}

template <typename F>
void splitOn(std::string_view s, char sep, F&& f) {
    while (true) {
        const std::size_t p = s.find(sep);
        f(trim(s.substr(0, p)));
        if (p == std::string_view::npos) return;
        s.remove_prefix(p + 1);
    }
}

bool contains(const std::vector<std::string>& v, std::string_view s) { return std::find(v.begin(), v.end(), s) != v.end(); }

// Ordner relativ zur Gerätewurzel: kein „..“, keine absolute Angabe, kein Gerätepräfix. Leer = ungültig.
std::string cleanFolder(std::string_view in) {
    std::string s(in);
    std::replace(s.begin(), s.end(), '\\', '/');
    if (s.empty() || s.front() == '/' || s.find(':') != std::string::npos) return {};
    while (!s.empty() && s.back() == '/') s.pop_back();
    std::string out;
    bool ok = true;
    splitOn(s, '/', [&](std::string_view part) {
        if (part.empty() || part == ".") return;
        if (part == "..") ok = false;
        if (!out.empty()) out += '/';
        out += part;
    });
    return ok ? out : std::string{};
}

std::string cleanExtension(std::string_view in) {
    std::string e = lower(in);
    if (e.size() < 2 || e.front() != '.') return {};
    for (char c : e)
        if (c == '/' || c == '\\' || c == ':' || c == ' ') return {};
    return e;
}

// Liest eine „|“-Liste; ungültige Teile fallen weg und machen `key` zu einem schlechten Schlüssel.
template <typename Clean>
std::vector<std::string> readList(std::string_view value, std::size_t max, Clean clean, bool& bad) {
    std::vector<std::string> out;
    splitOn(value, '|', [&](std::string_view part) {
        if (part.empty()) return;
        std::string v = clean(part);
        if (v.empty()) {
            bad = true;
            return;
        }
        if (out.size() < max && !contains(out, v)) out.push_back(std::move(v));
    });
    return out;
}

SourceSpec readSources(const IniDoc& ini, std::vector<std::string>& bad) {
    SourceSpec s;
    const std::string_view text = ini.get("library", "sources");
    if (text.empty()) {
        const std::string_view legacy = ini.get("general", "library");
        if (legacy == "sd" || legacy == "usb") s = SourceSpec{false, {std::string(legacy)}};
        return s;
    }
    if (lower(trim(text)) == "auto") return s;
    bool invalid = false;
    std::vector<std::string> devices;
    splitOn(text, ',', [&](std::string_view part) {
        const std::string d = deviceKey(part);
        if (!knownDevice(d)) {
            invalid = true;
            return;
        }
        if (!contains(devices, d)) devices.push_back(d);
    });
    if (invalid) bad.push_back("library.sources");
    if (!devices.empty()) s = SourceSpec{false, std::move(devices)};
    return s;
}

}  // namespace

ConsolePaths defaultConsolePaths(Console c) {
    const ConsoleInfo& info = consoleInfo(c);
    ConsolePaths p;
    for (const char* f : info.folders)
        if (f != nullptr) p.folders.emplace_back(f);
    for (const char* e : info.extensions)
        if (e != nullptr) p.extensions.emplace_back(e);
    return p;
}

LibraryRules libraryRulesFromIni(const IniDoc& ini) {
    LibraryRules r;
    r.sources = readSources(ini, r.bad);
    for (int i = 0; i < kConsoleCount; ++i) {
        const auto c = static_cast<Console>(i);
        const char* key = consoleInfo(c).key;
        ConsolePaths& p = r.paths[static_cast<std::size_t>(i)];
        p = defaultConsolePaths(c);
        if (const std::string_view v = ini.get("paths", key); !v.empty()) {
            bool bad = false;
            std::vector<std::string> folders = readList(v, kMaxConsoleFolders, cleanFolder, bad);
            if (bad || folders.empty()) r.bad.push_back(std::string("paths.") + key);
            if (!folders.empty()) p.folders = std::move(folders);
        }
        if (const std::string_view v = ini.get("extensions", key); !v.empty()) {
            bool bad = false;
            std::vector<std::string> exts = readList(v, kMaxConsoleExtensions, cleanExtension, bad);
            if (bad || exts.empty()) r.bad.push_back(std::string("extensions.") + key);
            if (!exts.empty()) p.extensions = std::move(exts);
        }
    }
    return r;
}

std::string sourcesToText(const SourceSpec& s) {
    if (s.autoMode || s.devices.empty()) return "auto";
    std::string out;
    for (const std::string& d : s.devices) {
        if (!out.empty()) out += ',';
        out += d;
    }
    return out;
}

std::vector<std::string> orderedSources(const SourceSpec& s, std::span<const std::string> mounted) {
    std::vector<std::string> names;
    for (const std::string& m : mounted) names.push_back(deviceKey(m));
    if (s.autoMode) return names;
    std::vector<std::string> out;
    for (const std::string& d : s.devices)
        if (contains(names, d) && !contains(out, d)) out.push_back(d);
    return out;
}

}  // namespace nl
