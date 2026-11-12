#include "core/wii_title.hpp"
#include <doctest/doctest.h>
#include <string>
#include <vector>
using namespace nl;

namespace {

void put16(std::vector<u8>& b, std::size_t at, u16 v) {
    b[at] = static_cast<u8>(v >> 8);
    b[at + 1] = static_cast<u8>(v);
}
void put32(std::vector<u8>& b, std::size_t at, u32 v) {
    put16(b, at, static_cast<u16>(v >> 16));
    put16(b, at + 2, static_cast<u16>(v));
}
void put64(std::vector<u8>& b, std::size_t at, u64 v) {
    put32(b, at, static_cast<u32>(v >> 32));
    put32(b, at + 4, static_cast<u32>(v));
}

struct TestContent {
    u32 id;
    u16 index;
    u64 size;
};

std::vector<u8> tmd(u64 titleId, u16 bootIndex, const std::vector<TestContent>& cs, std::size_t room) {
    std::vector<u8> b(0x1E4 + 36 * room, 0);
    put32(b, 0, 0x00010001);
    put64(b, 0x18C, titleId);
    put16(b, 0x1DC, 0xFF00);  // Titelversion (Stub-Kennung bei cIOS-Slots)
    put16(b, 0x1DE, static_cast<u16>(cs.size()));
    put16(b, 0x1E0, bootIndex);
    for (std::size_t i = 0; i < cs.size() && i < room; ++i) {
        const std::size_t at = 0x1E4 + 36 * i;
        put32(b, at, cs[i].id);
        put16(b, at + 4, cs[i].index);
        put16(b, at + 6, 1);
        put64(b, at + 8, cs[i].size);
    }
    return b;
}

// names[lang] als UTF-16-Einheiten.
std::vector<u8> imet(const std::vector<std::vector<u16>>& names) {
    std::vector<u8> b(kImetReadBytes, 0);
    b[0x40] = 'I'; b[0x41] = 'M'; b[0x42] = 'E'; b[0x43] = 'T';
    for (std::size_t l = 0; l < names.size(); ++l)
        for (std::size_t i = 0; i < names[l].size(); ++i) put16(b, 0x5C + l * 84 + i * 2, names[l][i]);
    return b;
}

std::vector<u16> u16s(const char* ascii) {
    std::vector<u16> out;
    for (const char* p = ascii; *p; ++p) out.push_back(static_cast<u16>(*p));
    return out;
}

}  // namespace

TEST_CASE("parseTmd liest Title-ID, Boot-Index und Contents") {
    const auto b = tmd(0x000100014E4C5443ULL, 1, {{0, 0, 0x600}, {1, 1, 0x1234}}, 2);
    auto r = parseTmd(b);
    REQUIRE(r.isOk());
    const TmdInfo& t = r.value();
    CHECK(t.titleId == 0x000100014E4C5443ULL);
    CHECK(t.bootIndex == 1);
    CHECK(t.titleVersion == 0xFF00);
    REQUIRE(t.contents.size() == 2);
    CHECK(t.contents[1].size == 0x1234);
    CHECK(t.contents[1].type == 1);
    CHECK(contentIdForIndex(t, 0) == 0u);
    CHECK(contentIdForIndex(t, 1) == 1u);
    CHECK_FALSE(contentIdForIndex(t, 7).has_value());
}

TEST_CASE("parseTmd lehnt zu kurze Daten ab") {
    const std::vector<u8> shortB(0x1E3, 0);
    auto a = parseTmd(shortB);
    REQUIRE_FALSE(a.isOk());
    CHECK(a.error().code == ErrorCode::InvalidData);
    const auto b = tmd(1, 0, {{0, 0, 1}, {1, 1, 1}}, 1);
    auto r = parseTmd(b);
    REQUIRE_FALSE(r.isOk());
    CHECK(r.error().code == ErrorCode::InvalidData);
}

TEST_CASE("isChannelTitle filtert nach Titeltyp 00010001") {
    CHECK(isChannelTitle(0x000100014E4C5443ULL));
    CHECK_FALSE(isChannelTitle(0x0000000100000002ULL));
    CHECK_FALSE(isChannelTitle(0x0001000048414141ULL));
    CHECK_FALSE(isChannelTitle(0x0001000248414141ULL));
    CHECK_FALSE(isChannelTitle(0x0001000848414141ULL));
}

TEST_CASE("imetChannelName bevorzugt die Sprache, dann Englisch, dann den ersten Namen") {
    // ニンテンドー
    const std::vector<u16> jp{0x30CB, 0x30F3, 0x30C6, 0x30F3, 0x30C9, 0x30FC};
    const auto b = imet({jp, u16s("NL Testkanal"), {}});
    CHECK(imetChannelName(b, BannerLang::German) == "NL Testkanal");
    CHECK(imetChannelName(b, BannerLang::Japanese) == "\xE3\x83\x8B\xE3\x83\xB3\xE3\x83\x86\xE3\x83\xB3\xE3\x83\x89\xE3\x83\xBC");
    const auto es = imet({{}, {}, {}, {}, u16s("Canal")});
    CHECK(imetChannelName(es, BannerLang::English) == "Canal");
}

TEST_CASE("imetChannelName ohne IMET, abgeschnitten oder leer liefert \"\"") {
    const std::vector<u8> zeros(0x40, 0);
    CHECK(imetChannelName(zeros, BannerLang::English).empty());
    auto full = imet({{}, u16s("Name")});
    const std::vector<u8> cut(full.begin(), full.begin() + 0x80);
    CHECK(imetChannelName(cut, BannerLang::English).empty());
    CHECK(imetChannelName(imet({}), BannerLang::English).empty());
}

TEST_CASE("imetChannelName dekodiert Surrogatpaare und ersetzt unpaarige") {
    CHECK(imetChannelName(imet({{}, {0xD83C, 0xDFAE}}), BannerLang::English) == "\xF0\x9F\x8E\xAE");
    CHECK(imetChannelName(imet({{}, {0xD83C, 0x0041}}), BannerLang::English) == "\xEF\xBF\xBD" "A");
}
