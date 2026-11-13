#include "core/cheat_source.hpp"

#include "core/gecko_txt.hpp"

namespace nl {

std::string cheatUrl(std::string_view tmpl, std::string_view id6) {
    constexpr std::string_view kTag = "{id6}";
    std::string out;
    out.reserve(tmpl.size() + 8);
    size_t pos = 0;
    while (true) {
        const size_t at = tmpl.find(kTag, pos);
        if (at == std::string_view::npos) break;
        out.append(tmpl.substr(pos, at - pos));
        out.append(id6);
        pos = at + kTag.size();
    }
    out.append(tmpl.substr(pos));
    return out;
}

CheatFetchResult classifyCheatBody(std::string_view body, std::string_view id6) {
    const auto parsed = parseGeckoTxt(body);
    if (!parsed.isOk() || parsed.value().id6 != id6 || parsed.value().codes.empty()) return CheatFetchResult::NoCodes;
    return CheatFetchResult::Ok;
}

std::string_view cheatFetchTag(CheatFetchResult r) {
    switch (r) {
        case CheatFetchResult::Ok: return "ok";
        case CheatFetchResult::NotFound: return "notfound";
        case CheatFetchResult::NoCodes: return "nocodes";
        case CheatFetchResult::Exists: return "exists";
        case CheatFetchResult::NetError: return "neterror";
    }
    return "neterror";
}

namespace {
std::string codesPath(std::string_view dataDir, std::string_view id6, std::string_view ext) {
    std::string p(dataDir);
    p += "/codes/";
    p += id6;
    p += ext;
    return p;
}
}  // namespace

std::string cheatTxtPath(std::string_view dataDir, std::string_view id6) { return codesPath(dataDir, id6, ".txt"); }
std::string cheatSelPath(std::string_view dataDir, std::string_view id6) { return codesPath(dataDir, id6, ".sel"); }

}  // namespace nl
