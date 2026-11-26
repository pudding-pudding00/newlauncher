#pragma once
// Cloud-Anbieter im Speicher für Host-Tests (M27). Revisionen zählen je Einheit hoch; Schalter simulieren offline,
// abgelaufene Anmeldung und ein zweites Gerät, das zwischen stat und upload hochlädt.

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "core/cloud_provider.hpp"

namespace nl::test {

class FakeProvider final : public CloudProvider {
public:
    struct Stored {
        CloudEntry entry;
        std::vector<u8> zip;
    };

    static constexpr const char* kToken = "FAKE-TOKEN-ya29-geheim";

    CloudStatus account(std::string& id, const volatile bool&) override {
        if (const CloudStatus s = gate(); s != CloudStatus::Ok) return s;
        id = "4711";
        return CloudStatus::Ok;
    }

    CloudStatus stat(std::string_view unit, CloudEntry& out, const volatile bool&) override {
        ++stats;
        if (const CloudStatus s = gate(); s != CloudStatus::Ok) return s;
        const auto it = files.find(std::string(unit));
        if (it == files.end()) return CloudStatus::NotFound;
        out = it->second.entry;
        return CloudStatus::Ok;
    }

    CloudStatus download(const CloudEntry& e, std::vector<u8>& zip, const volatile bool&) override {
        ++downloads;
        if (const CloudStatus s = gate(); s != CloudStatus::Ok) return s;
        for (const auto& [unit, f] : files) {
            if (f.entry.id == e.id) {
                zip = f.zip;
                return CloudStatus::Ok;
            }
        }
        return CloudStatus::NotFound;
    }

    CloudStatus upload(std::string_view unit, std::span<const u8> zip, const SaveMeta& meta,
                       std::string_view expectRev, CloudEntry& out, const volatile bool&) override {
        ++uploads;
        if (const CloudStatus s = gate(); s != CloudStatus::Ok) return s;
        if (beforeUpload) {
            auto hook = std::move(beforeUpload);  // nur einmal
            beforeUpload = nullptr;
            hook(*this);
        }
        const std::string key(unit);
        const auto it = files.find(key);
        const std::string current = it == files.end() ? std::string() : it->second.entry.rev;
        if (current != expectRev) return CloudStatus::RevMismatch;
        put(key, std::vector<u8>(zip.begin(), zip.end()), meta.hash, meta.device, meta.saved);
        out = files[key].entry;
        return CloudStatus::Ok;
    }

    // Legt ein Paket ab, als hätte ein anderes Gerät es hochgeladen; hash = appProperties.hash.
    void put(const std::string& unit, std::vector<u8> zip, std::string hash, std::string device, u64 saved) {
        Stored& f = files[unit];
        f.entry.id = "id-" + unit;
        f.entry.rev = std::to_string(++revCounter);
        f.entry.hash = std::move(hash);
        f.entry.device = std::move(device);
        f.entry.saved = saved;
        f.entry.size = zip.size();
        f.zip = std::move(zip);
    }

    std::map<std::string, Stored> files;
    std::function<void(FakeProvider&)> beforeUpload;
    CloudStatus fail = CloudStatus::Ok;  // ≠ Ok → jede Anfrage scheitert so
    int stats = 0, downloads = 0, uploads = 0;

private:
    CloudStatus gate() const { return fail; }
    u64 revCounter = 0;
};

}  // namespace nl::test
