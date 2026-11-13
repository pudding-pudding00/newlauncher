#include "core/options_model.hpp"

#include <algorithm>

#include "core/ini.hpp"

namespace nl {

namespace {

constexpr std::string_view kDefault = "default";

std::string lowerTrim(std::string_view s) {
    std::string out(trim(s));
    for (char& c : out)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}

bool isChannelHex(std::string_view v) {
    if (v.size() != 16 || v.substr(0, 8) != "00010001") return false;
    return std::all_of(v.begin(), v.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

}  // namespace

std::optional<std::string> optionsFileText(const Result<std::vector<u8>>& read) {
    if (read.isOk()) {
        const std::vector<u8>& b = read.value();
        return std::string(reinterpret_cast<const char*>(b.data()), b.size());
    }
    if (read.error().code == ErrorCode::NotFound) return std::string();
    return std::nullopt;
}

OptionsModel::OptionsModel(Console c, std::string id6, std::string configText, std::string gamesText,
                           std::optional<NinVersion> nin)
    : console_(c),
      id6_(std::move(id6)),
      config_(std::move(configText)),
      games_(std::move(gamesText)),
      config0_(config_),
      games0_(games_),
      nin_(nin) {
    refresh();
}

void OptionsModel::refresh() { rows_ = listOptions(console_, IniDoc::parse(config_), IniDoc::parse(games_), id6_); }

std::string OptionsModel::section() const {
    if (perGame()) return "game " + id6_;
    return console_ == Console::Gc ? "gc" : "wii";
}

const std::string& OptionsModel::shownValue(std::size_t r) const { return rows_[r].value; }

std::vector<std::string> OptionsModel::choices(std::size_t r) const {
    std::vector<std::string> out;
    if (perGame()) out.emplace_back(kDefault);
    for (std::string_view v : optionValues(console_, rows_[r].key)) out.emplace_back(v);
    if (rows_[r].key == "ios")
        for (unsigned slot = 200; slot <= 255; ++slot) out.push_back(std::to_string(slot));
    if (!rows_[r].inherited && std::find(out.begin(), out.end(), rows_[r].value) == out.end())
        out.push_back(rows_[r].value);  // z. B. return_to mit eigener Title-ID
    return out;
}

std::size_t OptionsModel::choice(std::size_t r) const {
    if (perGame() && rows_[r].inherited) return 0;
    const std::vector<std::string> c = choices(r);
    const auto it = std::find(c.begin(), c.end(), rows_[r].value);
    return it == c.end() ? 0 : static_cast<std::size_t>(it - c.begin());
}

std::optional<NinVersion> OptionsModel::requiredVersion(std::size_t r) const {
    if (console_ != Console::Gc || !nin_) return std::nullopt;
    for (const NinOptionGate& g : ninOptionGates())
        if (rows_[r].key == g.key && *nin_ < g.minVersion) return g.minVersion;
    return std::nullopt;
}

void OptionsModel::write(std::string_view key, std::optional<std::string_view> value) {
    if (perGame()) games_ = iniTextSet(games_, section(), key, value);
    else config_ = iniTextSet(config_, section(), key, value);
    refresh();
}

void OptionsModel::step(std::size_t r, int dir) {
    if (r >= rows_.size() || !enabled(r)) return;
    const std::vector<std::string> c = choices(r);
    const std::size_t n = c.size();
    const std::size_t next = (choice(r) + (dir < 0 ? n - 1 : 1)) % n;
    const std::string_view key = rows_[r].key;  // zeigt in die statische Tabelle, überlebt refresh()
    if (perGame() && next == 0) write(key, std::nullopt);
    else write(key, c[next]);
}

bool OptionsModel::set(std::string_view key, std::string_view value) {
    const std::string k = lowerTrim(key), v = lowerTrim(value);
    for (std::size_t r = 0; r < rows_.size(); ++r) {
        if (rows_[r].key != k) continue;
        const std::string_view canonical = rows_[r].key;
        if (perGame() && v == kDefault) {
            write(canonical, std::nullopt);
            return true;
        }
        const std::vector<std::string> c = choices(r);
        const bool ok = std::find(c.begin(), c.end(), v) != c.end() || (canonical == "return_to" && isChannelHex(v));
        if (!ok || v == kDefault) return false;
        write(canonical, v);
        return true;
    }
    return false;
}

void OptionsModel::reset() {
    if (perGame()) games_ = iniTextRemoveSection(games_, section());
    else config_ = iniTextRemoveSection(config_, section());
    refresh();
}

}  // namespace nl
