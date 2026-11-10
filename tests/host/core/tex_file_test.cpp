#include "core/tex_file.hpp"
#include <doctest/doctest.h>
#include <vector>
using namespace nl;

TEST_CASE(".tex Round-Trip") {
    std::vector<u8> data(200 * 280 / 2);
    for (std::size_t i = 0; i < data.size(); ++i) data[i] = u8(i * 7);
    const auto file = writeTex(200, 280, data);
    REQUIRE(file.size() == kTexHeaderBytes + data.size());
    CHECK(file[0] == 'N'); CHECK(file[3] == 'X');
    CHECK(file[11] == kGxFormatCmpr);  // u32 BE an Offset 8
    const auto view = parseTex(file);
    REQUIRE(view.isOk());
    CHECK(view.value().width == 200);
    CHECK(view.value().height == 280);
    CHECK(view.value().data.data() == file.data() + 32);
    CHECK(std::vector<u8>(view.value().data.begin(), view.value().data.end()) == data);
}

TEST_CASE(".tex verwirft Kaputtes") {
    const std::vector<u8> data(200 * 280 / 2, 0x11);
    const auto good = writeTex(200, 280, data);
    auto badMagic = good; badMagic[0] = 'X';
    auto badFormat = good; badFormat[11] = 6;
    auto badLen = good; badLen[15] = u8(badLen[15] + 1);
    auto shortFile = good; shortFile.resize(good.size() - 1);
    CHECK_FALSE(parseTex(badMagic).isOk());
    CHECK_FALSE(parseTex(badFormat).isOk());
    CHECK_FALSE(parseTex(badLen).isOk());
    CHECK_FALSE(parseTex(shortFile).isOk());
    CHECK_FALSE(parseTex({}).isOk());
    CHECK(parseTex(badLen).error().code == ErrorCode::InvalidData);
}
