#pragma once
// opening.bnr eines Wii-Spiels (M18 Task 9, Spike Weg B): Partitionstabelle bei 0x40000 → Spielpartition (Typ 0)
// → Ticket (verschlüsselter Titelschlüssel 0x1BF, Titel-ID 0x1DC, Schlüsselindex 0x1F1) → Daten ab 0x2B8 (×4)
// in Clustern zu 0x8000 Byte (0x400 Hash-Bereich, 0x7C00 Daten, IV = Hash-Bereich 0x3D0..0x3E0) →
// AES-128-CBC mit dem Titelschlüssel → Disc-Kopf, FST (0x424/0x428 ×4) → opening.bnr (Offset ×4).
// Den Titelschlüssel entschlüsselt der Aufrufer (Wii: ES_Decrypt mit dem Schlüssel im IOS); dieser Code kennt
// keinen festen Schlüssel. Formatbeschreibung: WiiBrew „Wii Disc“, „Ticket“.
// Die Disc gilt als nicht vertrauenswürdig: jede Größe und jeder Offset ist grenzgeprüft.

#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "core/aes128.hpp"
#include "core/fs.hpp"
#include "core/result.hpp"
#include "core/types.hpp"

namespace nl {

// Liest rohe Disc-Bytes (Disc-Offset, nicht Dateioffset). Nicht belegte WBFS-Blöcke lesen sich als Nullen.
class DiscSource {
public:
    virtual ~DiscSource() = default;
    virtual Result<void> read(u64 offset, std::span<u8> out) = 0;
};

// .iso direkt; .wbfs-Datei über den WBFS-Kopf (Slot 0). Geteilte .wbf1-Teile → Unsupported.
Result<std::unique_ptr<DiscSource>> openWiiDiscFile(FileSystem& fs, std::string path);

struct WiiTicketKey {
    AesBlock encrypted;  // Ticket 0x1BF
    AesBlock iv;         // Titel-ID (8 Byte) + 8 Nullbytes
    u8 index = 0;        // 0 = gewöhnlicher Schlüssel, 1 = koreanischer
};
// Entschlüsselt den Titelschlüssel; nullopt = fehlgeschlagen (Fehler „es“).
using TitleKeyDecrypt = std::function<std::optional<AesBlock>(const WiiTicketKey&)>;

inline constexpr u32 kWiiBannerMax = 4u * 1024 * 1024;
inline constexpr u32 kWiiFstMax = 1024 * 1024;

// Keine Spielpartition / kein opening.bnr → NotFound; Schlüsselindex > 1 oder kaputte Strukturen → InvalidData;
// Entschlüsselung des Titelschlüssels fehlgeschlagen → IoError mit Kontext „es“.
Result<std::vector<u8>> readWiiOpeningBnr(DiscSource& disc, const TitleKeyDecrypt& decrypt, u32 maxBytes = kWiiBannerMax);

}  // namespace nl
