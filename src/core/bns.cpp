#include "core/bns.hpp"

#include <cstring>

#include "core/dsp_adpcm.hpp"

namespace nl {

namespace {
u32 be32(std::span<const u8> b, std::size_t at) {
    return static_cast<u32>(b[at]) << 24 | static_cast<u32>(b[at + 1]) << 16 | static_cast<u32>(b[at + 2]) << 8 | b[at + 3];
}
u16 be16(std::span<const u8> b, std::size_t at) { return static_cast<u16>(b[at] << 8 | b[at + 1]); }
Result<BnsSound> invalid(const char* what) { return Result<BnsSound>::err(Error{ErrorCode::InvalidData, what}); }

// Abschnitt (Magic + Länge) bei off/size innerhalb der Datei; liefert den Körper ohne die 8 Byte Kopf.
bool section(std::span<const u8> file, u32 off, u32 size, const char* magic, std::span<const u8>& body) {
    if (size < 8 || off > file.size() || size > file.size() - off) return false;
    const auto s = file.subspan(off, size);
    if (std::memcmp(s.data(), magic, 4) != 0 || be32(s, 4) > size || be32(s, 4) < 8) return false;
    body = s.subspan(8, be32(s, 4) - 8);
    return true;
}
}  // namespace

Result<BnsSound> parseBns(std::span<const u8> file, u32 maxSamples) {
    if (file.size() < 0x20 || std::memcmp(file.data(), "BNS ", 4) != 0) return invalid("bns: kein BNS");
    std::span<const u8> info, data;
    if (!section(file, be32(file, 16), be32(file, 20), "INFO", info)) return invalid("bns: INFO");
    if (!section(file, be32(file, 24), be32(file, 28), "DATA", data)) return invalid("bns: DATA");
    if (info.size() < 0x18) return invalid("bns: INFO zu kurz");
    if (info[0] != 0) return invalid("bns: Codec");
    const u8 channels = info[2];
    if (channels < 1 || channels > 2) return invalid("bns: Kanäle");
    const u32 rate = be16(info, 4);
    if (rate == 0) return invalid("bns: Rate");
    const u32 n = be32(info, 12);
    if (n == 0 || n > maxSamples) return invalid("bns: Samplezahl");
    const u64 frameBytes = (static_cast<u64>(n) + kDspFrameSamples - 1) / kDspFrameSamples * kDspFrameBytes;
    const u32 table = be32(info, 16);
    if (table > info.size() || info.size() - table < 4u * channels) return invalid("bns: Kanaltabelle");
    // Vor dem Allozieren: die Samplezahl muss von den vorhandenen Daten gedeckt sein (sonst bis 24 GiB → Abbruch).
    if (frameBytes > data.size()) return invalid("bns: Daten abgeschnitten");

    BnsSound out;
    out.sampleRate = rate;
    out.channels = 2;
    out.loopStart = be32(info, 8);
    out.loop = info[1] != 0 && out.loopStart < n;
    if (!out.loop) out.loopStart = 0;
    out.pcm.assign(static_cast<std::size_t>(n) * 2, 0);
    std::vector<s16> mono(n);
    for (u32 c = 0; c < channels; ++c) {
        const u32 ci = be32(info, table + 4 * c);
        if (ci > info.size() || info.size() - ci < 0x0C) return invalid("bns: Kanalinfo");
        const u32 dataOff = be32(info, ci), adpcm = be32(info, ci + 4);
        if (adpcm > info.size() || info.size() - adpcm < 0x30) return invalid("bns: ADPCM-Info");
        if (dataOff > data.size() || data.size() - dataOff < frameBytes) return invalid("bns: Daten abgeschnitten");
        DspChannel ch;
        for (std::size_t k = 0; k < 16; ++k) ch.coef[k] = static_cast<s16>(be16(info, adpcm + 2 * k));
        ch.hist1 = static_cast<s16>(be16(info, adpcm + 0x24));
        ch.hist2 = static_cast<s16>(be16(info, adpcm + 0x26));
        decodeDspAdpcm(data.subspan(dataOff, static_cast<std::size_t>(frameBytes)), ch, mono);
        for (u32 i = 0; i < n; ++i) {
            if (channels == 1) out.pcm[2 * i + 1] = mono[i];
            out.pcm[2 * i + c] = mono[i];
        }
    }
    return Result<BnsSound>::ok(std::move(out));
}

}  // namespace nl
