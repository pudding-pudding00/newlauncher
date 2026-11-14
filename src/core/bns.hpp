#pragma once
// BNS-Banner-Ton (M18): Kopf 0x20 ("BNS ", FEFF 0100, Größe, 0x20, 2 Abschnitte, INFO-/DATA-Offset und -Länge),
// INFO (Codec 0 = DSP-ADPCM, Schleife, Kanäle, Rate, Schleifenstart, Samplezahl, Kanaltabelle) und DATA
// (je Kanal zusammenhängende 8-Byte-Frames). Das Layout folgt der öffentlichen Formatbeschreibung und ist
// nur gegen eigene Fixtures geprüft.

#include <span>
#include <vector>

#include "core/result.hpp"
#include "core/types.hpp"

namespace nl {

struct BnsSound {
    u32 sampleRate = 0;
    u8 channels = 2;  // Ausgabe ist immer stereo; Mono wird auf beide Seiten gelegt
    bool loop = false;
    u32 loopStart = 0;      // in Samples je Kanal
    std::vector<s16> pcm;  // verschränkt L, R
};

inline constexpr u32 kBnsMaxSamples = 1u << 19;  // je Kanal: 16 s bei 32 kHz, 2 MiB PCM stereo

// Fehler bei falschem Magic, Codec ≠ 0, 0 oder mehr als 2 Kanälen, Rate 0, mehr als maxSamples Samples
// oder abgeschnittenen Daten. Ein Schleifenstart hinter dem Ende schaltet die Schleife ab.
Result<BnsSound> parseBns(std::span<const u8> file, u32 maxSamples);

}  // namespace nl
