#include "core/remote_proto.hpp"

#include <array>
#include <charconv>

namespace nl {

namespace {

using R = Result<RemoteRequest>;

R bad(std::string why) { return R::err({ErrorCode::InvalidData, std::move(why)}); }

char lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

bool equalsNoCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (lower(a[i]) != lower(b[i])) return false;
    return true;
}

// Nächstes durch Leerzeichen getrenntes Wort; rest zeigt danach hinter die Leerzeichen.
std::string_view nextWord(std::string_view& rest) {
    while (!rest.empty() && rest.front() == ' ') rest.remove_prefix(1);
    const std::size_t sp = rest.find(' ');
    const std::string_view w = rest.substr(0, sp);
    rest = sp == std::string_view::npos ? std::string_view{} : rest.substr(sp + 1);
    while (!rest.empty() && rest.front() == ' ') rest.remove_prefix(1);
    return w;
}

template <typename T>
bool parseNum(std::string_view s, T& out, int base = 10) {
    if (s.empty()) return false;
    const auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), out, base);
    return ec == std::errc{} && p == s.data() + s.size();
}

struct ButtonName {
    std::string_view name;
    u64 bit;
};

constexpr std::array<ButtonName, 19> kButtons{{
    {"up", RawButtons::DUp}, {"down", RawButtons::DDown}, {"left", RawButtons::DLeft}, {"right", RawButtons::DRight},
    {"a", RawButtons::A}, {"b", RawButtons::B}, {"minus", RawButtons::Minus}, {"plus", RawButtons::Plus},
    {"one", RawButtons::One}, {"two", RawButtons::Two}, {"home", RawButtons::HomeBtn},
    {"gc_a", RawButtons::GcA}, {"gc_b", RawButtons::GcB}, {"gc_x", RawButtons::GcX}, {"gc_y", RawButtons::GcY},
    {"gc_l", RawButtons::GcL}, {"gc_r", RawButtons::GcR}, {"gc_z", RawButtons::GcZ}, {"gc_start", RawButtons::GcStart},
}};

bool startsWith(std::string_view s, std::string_view prefix) {
    return s.size() >= prefix.size() && s.substr(0, prefix.size()) == prefix;
}

// Datei direkt oder tiefer unter dir/ (mit Dateinamen), ohne Pfadtricks.
bool fileUnder(std::string_view path, std::string_view dir) {
    if (dir.empty() || !startsWith(path, dir)) return false;
    std::string_view rest = path.substr(dir.size());
    if (rest.empty() || rest.front() != '/') return false;
    rest.remove_prefix(1);
    if (rest.empty() || rest.back() == '/') return false;
    while (!rest.empty()) {
        const std::size_t slash = rest.find('/');
        const std::string_view part = rest.substr(0, slash);
        if (part.empty() || part == "." || part == "..") return false;
        rest = slash == std::string_view::npos ? std::string_view{} : rest.substr(slash + 1);
    }
    return true;
}

}  // namespace

std::optional<u64> parseRemoteButtons(std::string_view names) {
    if (names.empty()) return std::nullopt;
    u64 bits = 0;
    while (true) {
        const std::size_t comma = names.find(',');
        const std::string_view name = names.substr(0, comma);
        bool known = false;
        for (const ButtonName& b : kButtons) {
            if (equalsNoCase(name, b.name)) {
                bits |= b.bit;
                known = true;
                break;
            }
        }
        if (!known) return std::nullopt;
        if (comma == std::string_view::npos) break;
        names.remove_prefix(comma + 1);
    }
    return bits;
}

R parseRemoteLine(std::string_view line) {
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' ')) line.remove_suffix(1);
    if (line.size() > kRemoteMaxLine) return bad("Zeile zu lang");
    std::string_view rest = line;
    const std::string_view verb = nextWord(rest);
    RemoteRequest q;
    struct Simple {
        std::string_view name;
        RemoteCmd cmd;
    };
    constexpr Simple kSimple[] = {{"HELLO", RemoteCmd::Hello}, {"LOG", RemoteCmd::Log},         {"SHOT", RemoteCmd::Shot},
                                  {"STATE", RemoteCmd::State}, {"GAMES", RemoteCmd::Games},     {"RESTART", RemoteCmd::Restart},
                                  {"EXIT", RemoteCmd::Exit}};
    for (const Simple& s : kSimple) {
        if (equalsNoCase(verb, s.name)) {
            q.cmd = s.cmd;
            return R::ok(std::move(q));
        }
    }
    if (equalsNoCase(verb, "KEY")) {
        q.cmd = RemoteCmd::Key;
        const auto buttons = parseRemoteButtons(nextWord(rest));
        if (!buttons) return bad("unbekannte Taste");
        q.buttons = *buttons;
        q.frames = kRemoteDefaultKeyFrames;
        if (const std::string_view f = nextWord(rest); !f.empty()) {
            if (!parseNum(f, q.frames) || q.frames == 0 || q.frames > kRemoteMaxKeyFrames) return bad("Frames 1-600");
        }
        return R::ok(std::move(q));
    }
    if (equalsNoCase(verb, "LAUNCH")) {
        if (rest.empty()) return bad("Spielschlüssel fehlt");
        q.cmd = RemoteCmd::Launch;
        q.text = std::string(rest);
        return R::ok(std::move(q));
    }
    if (equalsNoCase(verb, "PUT")) {
        q.cmd = RemoteCmd::Put;
        if (!parseNum(nextWord(rest), q.size) || q.size == 0 || q.size > kRemoteMaxPut) return bad("Größe 1-16 MiB");
        if (!parseNum(nextWord(rest), q.crc, 16)) return bad("CRC32 (hex) fehlt");
        if (rest.empty()) return bad("Pfad fehlt");
        q.text = std::string(rest);
        return R::ok(std::move(q));
    }
    return bad("unbekannter Befehl");
}

bool remotePutAllowed(std::string_view path, std::string_view dataRoot) {
    if (path.find('\\') != std::string_view::npos) return false;
    const std::size_t colon = path.find(":/");
    if (colon == std::string_view::npos || colon == 0) return false;
    const std::string appDir = std::string(path.substr(0, colon)) + ":/apps/newlauncher";
    return fileUnder(path, appDir) || fileUnder(path, dataRoot);
}

std::string remoteReplyHeader(bool ok, std::size_t length, std::string_view info) {
    std::string out = ok ? "OK " : "ERR ";
    out += std::to_string(length);
    if (!info.empty()) {
        out += ' ';
        for (char c : info) out += (c == '\n' || c == '\r') ? ' ' : c;
    }
    out += '\n';
    return out;
}

bool hasRemoteFlag(int argc, const char* const* argv) {
    if (argv == nullptr) return false;
    for (int i = 1; i < argc; ++i)
        if (argv[i] != nullptr && std::string_view(argv[i]) == "--remote") return true;
    return false;
}

}  // namespace nl
