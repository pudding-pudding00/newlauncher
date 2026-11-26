#include "core/cloud_state.hpp"

#include <charconv>

namespace nl {

namespace {

std::string section(std::string_view acct, std::string_view unit) {
    std::string s(acct);
    s += '/';
    s += unit;
    return s;
}

}  // namespace

CloudState CloudState::parse(std::string_view text) {
    CloudState s;
    s.ini_ = IniDoc::parse(text);
    return s;
}

std::string CloudState::serialize() const { return ini_.serialize(); }

std::optional<SyncBase> CloudState::base(std::string_view acct, std::string_view unit) const {
    const std::string sec = section(acct, unit);
    SyncBase b{std::string(ini_.get(sec, "hash")), std::string(ini_.get(sec, "rev"))};
    if (b.hash.empty() || b.rev.empty()) return std::nullopt;
    return b;
}

void CloudState::setBase(std::string_view acct, std::string_view unit, const SyncBase& b) {
    const std::string sec = section(acct, unit);
    ini_.set(sec, "hash", b.hash);
    ini_.set(sec, "rev", b.rev);
}

u64 CloudState::played(std::string_view acct, std::string_view unit) const {
    const std::string_view v = ini_.get(section(acct, unit), "played");
    u64 out = 0;
    const auto [p, ec] = std::from_chars(v.data(), v.data() + v.size(), out);
    return ec == std::errc{} && p == v.data() + v.size() ? out : 0;
}

void CloudState::setPlayed(std::string_view acct, std::string_view unit, u64 unixTime) {
    ini_.set(section(acct, unit), "played", std::to_string(unixTime));
}

void CloudState::clearAccount(std::string_view acct) {
    const std::string prefix = std::string(acct) + "/";
    std::vector<std::string> doomed;
    for (const std::string_view name : ini_.sectionNames()) {
        if (name.rfind(prefix, 0) == 0) doomed.emplace_back(name);
    }
    for (const std::string& name : doomed) ini_.removeSection(name);
}

}  // namespace nl
