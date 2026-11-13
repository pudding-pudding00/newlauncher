#include <doctest/doctest.h>

#include <optional>
#include <vector>

#include "core/wii_partition.hpp"
#include "support/mem_fs.hpp"
#include "support/wii_disc_image.hpp"

using namespace nl;
using namespace nl::test;

namespace {

// Groß genug, um über eine Clustergrenze (0x7C00 Nutzdaten) zu reichen.
std::vector<u8> bnrBytes(std::size_t n) {
    std::vector<u8> b(n);
    for (std::size_t i = 0; i < n; ++i) b[i] = static_cast<u8>(i * 5 + (i >> 8));
    return b;
}

class VecSource final : public DiscSource {
public:
    explicit VecSource(std::vector<u8> d) : d_(std::move(d)) {}
    Result<void> read(u64 offset, std::span<u8> out) override {
        if (offset + out.size() > d_.size()) return Result<void>::err(Error{ErrorCode::IoError, "Ende"});
        std::memcpy(out.data(), d_.data() + offset, out.size());
        return Result<void>::ok();
    }

private:
    std::vector<u8> d_;
};

std::optional<AesBlock> testDecrypt(const WiiTicketKey& k) {
    AesBlock out{};
    if (!aesCbcDecrypt(Aes128(kTestCommonKey), k.iv, k.encrypted, out)) return std::nullopt;
    return out;
}

WiiDiscSpec specWithBnr(const std::vector<u8>& bnr) {
    WiiDiscSpec s;
    s.root = {FstSpec{"main.dol", bnrBytes(0x300), false, {}}, FstSpec{"opening.bnr", bnr, false, {}}, FstSpec{"zz", bnrBytes(0x40), false, {}}};
    return s;
}

}  // namespace

TEST_CASE("Wii-Partition: opening.bnr wird über Clustergrenzen entschlüsselt") {
    const std::vector<u8> bnr = bnrBytes(0x9123);
    VecSource disc(wiiDiscImage(specWithBnr(bnr)));
    WiiTicketKey seen{};
    auto r = readWiiOpeningBnr(disc, [&](const WiiTicketKey& k) {
        seen = k;
        return testDecrypt(k);
    });
    REQUIRE(r.isOk());
    CHECK(r.value() == bnr);
    CHECK(seen.index == 0);
    CHECK(seen.iv[4] == 'R');
    CHECK(seen.iv[8] == 0);
    CHECK(seen.iv[15] == 0);
}

TEST_CASE("Wii-Partition: ohne Update-Partition, koreanischer Schlüsselindex wird weitergereicht") {
    WiiDiscSpec s = specWithBnr(bnrBytes(0x700));
    s.updatePartition = false;
    s.keyIndex = 1;
    VecSource disc(wiiDiscImage(s));
    u8 index = 9;
    auto r = readWiiOpeningBnr(disc, [&](const WiiTicketKey& k) {
        index = k.index;
        return testDecrypt(k);
    });
    REQUIRE(r.isOk());
    CHECK(r.value() == bnrBytes(0x700));
    CHECK(index == 1);
}

TEST_CASE("Wii-Partition: Fehlerfälle") {
    SUBCASE("keine Spielpartition") {
        WiiDiscSpec s = specWithBnr(bnrBytes(0x100));
        s.gamePartition = false;
        VecSource disc(wiiDiscImage(s));
        auto r = readWiiOpeningBnr(disc, testDecrypt);
        REQUIRE_FALSE(r.isOk());
        CHECK(r.error().code == ErrorCode::NotFound);
    }
    SUBCASE("unbekannter Schlüsselindex") {
        WiiDiscSpec s = specWithBnr(bnrBytes(0x100));
        s.keyIndex = 2;
        VecSource disc(wiiDiscImage(s));
        bool called = false;
        auto r = readWiiOpeningBnr(disc, [&](const WiiTicketKey& k) {
            called = true;
            return testDecrypt(k);
        });
        REQUIRE_FALSE(r.isOk());
        CHECK(r.error().code == ErrorCode::InvalidData);
        CHECK_FALSE(called);
    }
    SUBCASE("Entschlüsselung des Titelschlüssels scheitert") {
        VecSource disc(wiiDiscImage(specWithBnr(bnrBytes(0x100))));
        auto r = readWiiOpeningBnr(disc, [](const WiiTicketKey&) { return std::optional<AesBlock>{}; });
        REQUIRE_FALSE(r.isOk());
        CHECK(r.error().code == ErrorCode::IoError);
        CHECK(r.error().context == "es");
    }
    SUBCASE("falscher Titelschlüssel → kaputtes FST, kein Absturz") {
        VecSource disc(wiiDiscImage(specWithBnr(bnrBytes(0x100))));
        auto r = readWiiOpeningBnr(disc, [](const WiiTicketKey&) { return std::optional<AesBlock>{AesBlock{}}; });
        REQUIRE_FALSE(r.isOk());
    }
    SUBCASE("kein opening.bnr") {
        WiiDiscSpec s;
        s.root = {FstSpec{"main.dol", bnrBytes(0x300), false, {}}};
        VecSource disc(wiiDiscImage(s));
        auto r = readWiiOpeningBnr(disc, testDecrypt);
        REQUIRE_FALSE(r.isOk());
        CHECK(r.error().code == ErrorCode::NotFound);
    }
    SUBCASE("opening.bnr über der Grenze") {
        VecSource disc(wiiDiscImage(specWithBnr(bnrBytes(0x2000))));
        auto r = readWiiOpeningBnr(disc, testDecrypt, 0x1000);
        REQUIRE_FALSE(r.isOk());
        CHECK(r.error().code == ErrorCode::InvalidData);
    }
    SUBCASE("abgeschnittenes Abbild") {
        std::vector<u8> img = wiiDiscImage(specWithBnr(bnrBytes(0x9000)));
        img.resize(img.size() - 0x8000);
        VecSource disc(std::move(img));
        auto r = readWiiOpeningBnr(disc, testDecrypt);
        REQUIRE_FALSE(r.isOk());
    }
}

TEST_CASE("Wii-Disc-Datei: ISO und WBFS lesen dieselben Disc-Bytes") {
    const std::vector<u8> bnr = bnrBytes(0x8800);
    const std::vector<u8> disc = wiiDiscImage(specWithBnr(bnr));
    MemFileSystem fs;
    fs.addFile("sd:/wbfs/Spiel [RTSE01]/RTSE01.iso", disc);
    // WBFS-Block 256 KiB; Disc-Block 0 → Dateiblock 3, 1 → 1, 2 → 2, Rest frei.
    fs.addFile("sd:/wbfs/Spiel [RTSE01]/RTSE01.wbfs", wbfsFileFromDisc(disc, 18, {3, 1, 2}));

    for (const char* path : {"sd:/wbfs/Spiel [RTSE01]/RTSE01.iso", "sd:/wbfs/Spiel [RTSE01]/RTSE01.wbfs"}) {
        CAPTURE(path);
        auto src = openWiiDiscFile(fs, path);
        REQUIRE(src.isOk());
        std::vector<u8> head(0x20);
        REQUIRE(src.value()->read(0, head).isOk());
        CHECK(std::equal(head.begin(), head.end(), disc.begin()));
        auto r = readWiiOpeningBnr(*src.value(), testDecrypt);
        REQUIRE(r.isOk());
        CHECK(r.value() == bnr);
    }
}

TEST_CASE("Wii-Disc-Datei: WBFS-Sonderfälle") {
    const std::vector<u8> disc = wiiDiscImage(specWithBnr(bnrBytes(0x100)));
    MemFileSystem fs;
    fs.addFile("sd:/a.wbfs", wbfsFileFromDisc(disc, 18, {1, 2, 3}));
    auto src = openWiiDiscFile(fs, "sd:/a.wbfs");
    REQUIRE(src.isOk());
    SUBCASE("nicht belegter Block liest sich als Nullen") {
        std::vector<u8> out(0x10, 0xEE);
        REQUIRE(src.value()->read(u64{10} << 18, out).isOk());
        CHECK(out == std::vector<u8>(0x10, 0));
    }
    SUBCASE("Lesen über eine Blockgrenze") {
        std::vector<u8> out(0x40);
        REQUIRE(src.value()->read((u64{1} << 18) - 0x20, out).isOk());
        CHECK(std::equal(out.begin(), out.end(), disc.begin() + (1 << 18) - 0x20));
    }
    SUBCASE("geteilte Datei wird nicht unterstützt") {
        fs.addFile("sd:/a.wbf1", std::vector<u8>(16, 0));
        auto split = openWiiDiscFile(fs, "sd:/a.wbfs");
        REQUIRE_FALSE(split.isOk());
        CHECK(split.error().code == ErrorCode::Unsupported);
    }
    SUBCASE("kaputter Kopf") {
        std::vector<u8> bad = wbfsFileFromDisc(disc, 18, {1, 2, 3});
        bad[9] = 3;
        fs.addFile("sd:/b.wbfs", bad);
        auto r = openWiiDiscFile(fs, "sd:/b.wbfs");
        REQUIRE_FALSE(r.isOk());
        CHECK(r.error().code == ErrorCode::InvalidData);
    }
    SUBCASE("andere Endung") {
        fs.addFile("sd:/c.wad", std::vector<u8>(16, 0));
        auto r = openWiiDiscFile(fs, "sd:/c.wad");
        REQUIRE_FALSE(r.isOk());
        CHECK(r.error().code == ErrorCode::Unsupported);
    }
}
