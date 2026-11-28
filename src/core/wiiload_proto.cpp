#include "core/wiiload_proto.hpp"

#include "core/bin_io.hpp"
#include "core/inflate.hpp"

namespace nl {
namespace {

u32 adler32(std::span<const u8> data) {
    u32 a = 1;
    u32 b = 0;
    constexpr u32 kMod = 65521;
    for (u8 byte : data) {
        a = (a + byte) % kMod;
        b = (b + a) % kMod;
    }
    return (b << 16) | a;
}

}  // namespace

Result<WiiloadHeader> parseWiiloadHeader(std::span<const u8> hdr) {
    if (hdr.size() < kWiiloadHeaderBytes) {
        return Result<WiiloadHeader>::err({ErrorCode::InvalidData, "wiiload header truncated"});
    }
    if (hdr[0] != 'H' || hdr[1] != 'A' || hdr[2] != 'X' || hdr[3] != 'X') {
        return Result<WiiloadHeader>::err({ErrorCode::InvalidData, "wiiload bad magic"});
    }
    BeReader r(hdr.subspan(4, kWiiloadHeaderBytes - 4));
    WiiloadHeader out;
    r.u8(out.major);
    r.u8(out.minor);
    r.u16(out.argsLen);
    r.u32(out.compSize);
    r.u32(out.uncompSize);
    if (!r.ok()) {
        return Result<WiiloadHeader>::err({ErrorCode::InvalidData, "wiiload header read failed"});
    }
    if (out.major != 0 || (out.minor != 4 && out.minor != 5)) {
        return Result<WiiloadHeader>::err({ErrorCode::InvalidData, "wiiload unsupported version"});
    }
    if (out.argsLen > kWiiloadMaxArgsBytes) {
        return Result<WiiloadHeader>::err({ErrorCode::InvalidData, "wiiload args too large"});
    }
    if (out.compSize == 0 || out.compSize > kWiiloadMaxCompBytes) {
        return Result<WiiloadHeader>::err({ErrorCode::InvalidData, "wiiload compressed size out of range"});
    }
    if (out.uncompSize > kWiiloadMaxUncompBytes) {
        return Result<WiiloadHeader>::err({ErrorCode::InvalidData, "wiiload uncompressed size out of range"});
    }
    return Result<WiiloadHeader>::ok(out);
}

Result<std::vector<u8>> decodeWiiloadPayload(const WiiloadHeader& hdr, std::span<const u8> raw) {
    if (raw.size() != hdr.compSize) {
        return Result<std::vector<u8>>::err({ErrorCode::InvalidData, "wiiload payload length mismatch"});
    }
    if (hdr.uncompSize == 0) {
        return Result<std::vector<u8>>::ok(std::vector<u8>(raw.begin(), raw.end()));
    }
    // RFC 1950 zlib wrapper: 2-byte header + deflate stream + 4-byte Adler-32 trailer.
    if (raw.size() < 6) {
        return Result<std::vector<u8>>::err({ErrorCode::InvalidData, "wiiload zlib stream too short"});
    }
    const u8 cmf = raw[0];
    const u8 flg = raw[1];
    if ((cmf & 0x0Fu) != 8u || (cmf >> 4) > 7u || (((u32{cmf} << 8) | flg) % 31u) != 0u) {
        return Result<std::vector<u8>>::err({ErrorCode::InvalidData, "wiiload invalid zlib header"});
    }
    if ((flg & 0x20u) != 0u) {
        return Result<std::vector<u8>>::err({ErrorCode::InvalidData, "wiiload zlib preset dictionary unsupported"});
    }
    auto inflated = inflateRaw(raw.subspan(2, raw.size() - 6), hdr.uncompSize);
    if (!inflated.isOk()) {
        return Result<std::vector<u8>>::err(inflated.error());
    }
    if (inflated.value().size() != hdr.uncompSize) {
        return Result<std::vector<u8>>::err({ErrorCode::InvalidData, "wiiload inflated size mismatch"});
    }
    BeReader trailer(raw.subspan(raw.size() - 4, 4));
    u32 wantAdler = 0;
    trailer.u32(wantAdler);
    if (adler32(inflated.value()) != wantAdler) {
        return Result<std::vector<u8>>::err({ErrorCode::InvalidData, "wiiload zlib adler32 mismatch"});
    }
    return Result<std::vector<u8>>::ok(std::move(inflated.value()));
}

std::vector<std::string> parseWiiloadArgs(std::span<const u8> rawArgs) {
    std::vector<std::string> args;
    std::size_t i = 0;
    while (i < rawArgs.size() && args.size() < kWiiloadMaxArgs) {
        std::size_t start = i;
        while (i < rawArgs.size() && rawArgs[i] != 0) ++i;
        if (i > start) {
            args.emplace_back(reinterpret_cast<const char*>(rawArgs.data() + start), i - start);
        }
        if (i < rawArgs.size() && rawArgs[i] == 0) ++i;
    }
    return args;
}

WiiloadKind detectWiiloadKind(std::span<const u8> payload, std::string_view /*argv0*/) {
    if (payload.size() >= 4) {
        if (payload[0] == 'P' && payload[1] == 'K' && payload[2] == 0x03 && payload[3] == 0x04) {
            return WiiloadKind::Zip;
        }
        if (payload[0] == 0x7F && payload[1] == 'E' && payload[2] == 'L' && payload[3] == 'F') {
            return WiiloadKind::Elf;
        }
    }
    return WiiloadKind::Dol;
}

}  // namespace nl
