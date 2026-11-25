#include "core/return_stub.hpp"

#include "launch/return_stub/stub_check.h"

#include <algorithm>

namespace nl {

namespace {

void put32(std::vector<u8>& b, std::size_t at, u32 v) {
    b[at] = static_cast<u8>(v >> 24);
    b[at + 1] = static_cast<u8>(v >> 16);
    b[at + 2] = static_cast<u8>(v >> 8);
    b[at + 3] = static_cast<u8>(v);
}

}  // namespace

Result<std::vector<u8>, ReturnStubError> buildReturnParams(u32 dolSize, std::span<const Extent> extents,
                                                           std::span<const u8> argvBlock) {
    using R = Result<std::vector<u8>, ReturnStubError>;
    if (dolSize == 0 || dolSize > NLRS_MAX_DOL) return R::err(ReturnStubError::DolSize);
    if (extents.empty()) return R::err(ReturnStubError::NoExtents);
    if (argvBlock.size() > NLRS_ARGV_MAX) return R::err(ReturnStubError::ArgvTooLarge);
    // libogc legt argv[] (ein Zeiger je Argument, NULL, Reserve) in den Freiraum hinter der Kommandozeile.
    if (argvBlock.size() > 24) {
        const auto cmd = argvBlock.subspan(24);
        const auto argc = static_cast<u32>(std::count(cmd.begin(), cmd.end(), u8{0}));
        if ((argc + 2u) * 4u > NLRS_ARGV_SLACK) return R::err(ReturnStubError::ArgvTooLarge);
    }
    // fileExtents liefert ganze Cluster: auf ceil(dolSize / 512) Sektoren kürzen, überzählige Extents fallen weg.
    const u32 needed = (dolSize + 511u) / 512u;
    std::vector<Extent> used;
    u32 next = 0;
    for (const Extent& e : extents) {
        if (e.fileSector != next || e.count == 0) return R::err(ReturnStubError::NotContiguous);
        if (next < needed) used.push_back({e.fileSector, e.lba, std::min(e.count, needed - next)});
        next += e.count;
    }
    if (next < needed) return R::err(ReturnStubError::SectorMismatch);

    const u32 argvSize = static_cast<u32>(argvBlock.size());
    const std::size_t size = NLRS_HEAD_BYTES + nlrs_argv_padded(argvSize) + NLRS_ARGV_SLACK + used.size() * 8u;
    if (size > NLRS_PARAMS_MAX) return R::err(ReturnStubError::TooManyExtents);

    std::vector<u8> b(size, 0);
    put32(b, 0x00, NLRS_MAGIC);
    put32(b, 0x04, NLRS_VERSION);
    put32(b, 0x08, dolSize);
    put32(b, 0x0C, static_cast<u32>(used.size()));
    put32(b, 0x10, argvSize);
    for (std::size_t i = 0; i < argvBlock.size(); ++i) b[NLRS_HEAD_BYTES + i] = argvBlock[i];
    std::size_t at = NLRS_HEAD_BYTES + nlrs_argv_padded(argvSize) + NLRS_ARGV_SLACK;
    for (const Extent& e : used) {
        put32(b, at, e.lba);
        put32(b, at + 4, e.count);
        at += 8;
    }
    put32(b, 0x14, nlrs_params_checksum(b.data()));
    return R::ok(std::move(b));
}

bool gameReturnStubWanted(std::string_view selfDol, std::size_t gctBytes) {
    return gctBytes == 0 && selfDol.starts_with("sd:/");
}

}  // namespace nl
