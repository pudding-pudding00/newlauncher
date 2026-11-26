#pragma once
// Schnittstelle der Cloud-Anbieter (M27, Spec Ziel 5/6). Ein Anbieter speichert je Einheit genau ein Paket
// "<einheit>.zip" mit den Eigenschaften hash/device/saved (Spec: aus den Datei-Eigenschaften, nicht aus dem ZIP).
// Alle Aufrufe blockieren und laufen nur im Netz-Thread.

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/save_archive.hpp"
#include "core/types.hpp"

namespace nl {

enum class CloudStatus : u8 { Ok, NotFound, RevMismatch, Unauthorized, Offline, Failed };

const char* cloudStatusName(CloudStatus s);  // "ok", "notfound", … für Log-Marker

struct CloudEntry {
    std::string id, rev, hash, device;
    u64 saved = 0, size = 0;
};

class CloudProvider {
public:
    virtual ~CloudProvider() = default;
    // Stabile Konto-ID ohne persönliche Daten (Drive: permissionId, keine E-Mail).
    virtual CloudStatus account(std::string& id, const volatile bool& cancel) = 0;
    virtual CloudStatus stat(std::string_view unit, CloudEntry& out, const volatile bool& cancel) = 0;
    virtual CloudStatus download(const CloudEntry& entry, std::vector<u8>& zip, const volatile bool& cancel) = 0;
    // expectRev leer = die Datei darf noch nicht existieren; sonst muss ihre Revision gleich sein (RevMismatch).
    virtual CloudStatus upload(std::string_view unit, std::span<const u8> zip, const SaveMeta& meta,
                               std::string_view expectRev, CloudEntry& out, const volatile bool& cancel) = 0;
    // OAuth-Anbieter: Tokens nach einem Refresh neu versiegeln (tokensChanged), NL_CLOUD_REFRESH (refreshed).
    virtual bool tokensChanged() const { return false; }
    virtual bool refreshed() const { return false; }
};

// Paketgrenze in der Cloud (gepackt): größere Downloads werden abgelehnt.
inline constexpr std::size_t kMaxCloudPackageBytes = kMaxSaveUnitBytes * 2;

// Einheiten-IDs, die als Dateiname und in Abfragen sicher sind: [A-Za-z0-9._-], 1..64 Zeichen.
bool cloudSafeUnitId(std::string_view unit);

}  // namespace nl
