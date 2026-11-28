#include <doctest/doctest.h>

#include <map>
#include <set>

#include "core/wiiload_zip.hpp"
#include "core/zip_writer.hpp"

using namespace nl;

namespace {

class MemFs : public FileSystem {
public:
    std::map<std::string, std::vector<u8>> files;
    std::set<std::string> dirs;
    std::string failWriteOn;

    Result<std::vector<DirEntry>> list(std::string_view dir) override {
        std::string d(dir);
        if (dirs.find(d) == dirs.end()) {
            bool any = false;
            std::string pref = d + "/";
            for (const auto& [k, _] : files) {
                if (k.starts_with(pref)) { any = true; break; }
            }
            if (!any) return Result<std::vector<DirEntry>>::err({ErrorCode::NotFound, d});
        }
        std::map<std::string, DirEntry> out;
        std::string pref = d + "/";
        for (const auto& [k, v] : files) {
            if (!k.starts_with(pref)) continue;
            std::string_view rest = std::string_view(k).substr(pref.size());
            auto sl = rest.find('/');
            if (sl == std::string_view::npos) {
                out[std::string(rest)] = DirEntry{std::string(rest), false, v.size(), 0};
            } else {
                std::string sub(rest.substr(0, sl));
                out[sub] = DirEntry{sub, true, 0, 0};
            }
        }
        std::vector<DirEntry> res;
        for (auto& [_, e] : out) res.push_back(std::move(e));
        return Result<std::vector<DirEntry>>::ok(std::move(res));
    }

    Result<std::size_t> read(std::string_view, u64, std::span<u8>) override {
        return Result<std::size_t>::err({ErrorCode::Unsupported, ""});
    }

    Result<std::vector<u8>> readAll(std::string_view file, std::size_t) override {
        auto it = files.find(std::string(file));
        if (it == files.end()) return Result<std::vector<u8>>::err({ErrorCode::NotFound, std::string(file)});
        return Result<std::vector<u8>>::ok(it->second);
    }

    Result<void> writeAtomic(std::string_view file, std::span<const u8> data) override {
        if (!failWriteOn.empty() && file.find(failWriteOn) != std::string_view::npos) {
            return Result<void>::err({ErrorCode::IoError, "injected failure"});
        }
        files[std::string(file)] = std::vector<u8>(data.begin(), data.end());
        return Result<void>::ok();
    }

    Result<void> makeDirs(std::string_view dir) override {
        dirs.insert(std::string(dir));
        return Result<void>::ok();
    }

    Result<void> removeFile(std::string_view file) override {
        files.erase(std::string(file));
        dirs.erase(std::string(file));
        return Result<void>::ok();
    }

    Result<void> renameFile(std::string_view from, std::string_view to) override {
        std::string f(from), t(to);
        std::string fp = f + "/";
        std::vector<std::pair<std::string, std::vector<u8>>> moved;
        for (auto it = files.begin(); it != files.end();) {
            if (it->first == f) {
                moved.emplace_back(t, std::move(it->second));
                it = files.erase(it);
            } else if (it->first.starts_with(fp)) {
                moved.emplace_back(t + it->first.substr(f.size()), std::move(it->second));
                it = files.erase(it);
            } else {
                ++it;
            }
        }
        for (auto& [k, v] : moved) files[k] = std::move(v);
        if (dirs.erase(f)) dirs.insert(t);
        return Result<void>::ok();
    }
};

std::vector<u8> makeZip(const std::vector<std::pair<std::string, std::vector<u8>>>& items) {
    ZipWriter w;
    for (const auto& [name, data] : items) {
        REQUIRE(w.add(name, data, true, 0).isOk());
    }
    return w.finish();
}

}  // namespace

TEST_CASE("inspectWiiloadZip and installWiiloadZip handle standard apps/<name>/ bundles") {
    auto zip = makeZip({
        {"apps/demo/boot.dol", {1, 2, 3, 4}},
        {"apps/demo/meta.xml", {'<', 'a', 'p', 'p', '/', '>'}},
    });
    auto plan = inspectWiiloadZip(zip);
    REQUIRE(plan.isOk());
    CHECK(plan.value().isBundle);
    CHECK(plan.value().appName == "demo");
    CHECK(plan.value().entryPath == "apps/demo/boot.dol");
    CHECK(plan.value().entryKind == WiiloadKind::Dol);

    MemFs fs;
    auto installed = installWiiloadZip(fs, zip, plan.value(), "sd:");
    REQUIRE(installed.isOk());
    CHECK(installed.value() == "sd:/apps/demo/boot.dol");
    CHECK(fs.files.count("sd:/apps/demo/boot.dol") == 1);
    CHECK(fs.files.count("sd:/apps/demo/meta.xml") == 1);
    CHECK(fs.dirs.count("sd:/apps/demo.tmp") == 0);
    CHECK(fs.dirs.count("sd:/apps/demo.old") == 0);
}

TEST_CASE("inspectWiiloadZip handles boot.elf bundles and root single-binary ZIPs") {
    auto elfBundle = makeZip({{"apps/myelf/boot.elf", {0x7F, 'E', 'L', 'F'}}});
    auto p1 = inspectWiiloadZip(elfBundle);
    REQUIRE(p1.isOk());
    CHECK(p1.value().isBundle);
    CHECK(p1.value().entryKind == WiiloadKind::Elf);

    auto rootZip = makeZip({{"boot.dol", {9, 8, 7}}});
    auto p2 = inspectWiiloadZip(rootZip);
    REQUIRE(p2.isOk());
    CHECK_FALSE(p2.value().isBundle);
    auto extracted = extractWiiloadZipEntry(rootZip, p2.value().entryPath);
    REQUIRE(extracted.isOk());
    CHECK(extracted.value() == std::vector<u8>({9, 8, 7}));
}

TEST_CASE("installWiiloadZip preserves existing app on mid-extraction write failure") {
    auto zip = makeZip({
        {"apps/demo/boot.dol", {1, 2, 3}},
        {"apps/demo/fail.bin", {4, 5, 6}},
    });
    auto plan = inspectWiiloadZip(zip);
    REQUIRE(plan.isOk());

    MemFs fs;
    fs.files["sd:/apps/demo/boot.dol"] = {9, 9, 9};
    fs.dirs.insert("sd:/apps/demo");
    fs.failWriteOn = "fail.bin";

    auto res = installWiiloadZip(fs, zip, plan.value(), "sd:");
    CHECK_FALSE(res.isOk());
    CHECK(fs.files["sd:/apps/demo/boot.dol"] == std::vector<u8>({9, 9, 9}));
    CHECK(fs.dirs.count("sd:/apps/demo.tmp") == 0);
}

TEST_CASE("inspectWiiloadZip rejects missing executable and mixed layouts") {
    auto noExec = makeZip({{"apps/demo/readme.txt", {'h', 'i'}}});
    CHECK_FALSE(inspectWiiloadZip(noExec).isOk());

    auto mixed = makeZip({{"apps/demo/boot.dol", {1}}, {"extra.txt", {2}}});
    CHECK_FALSE(inspectWiiloadZip(mixed).isOk());
}
