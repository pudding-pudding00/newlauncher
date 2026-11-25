#include "core/return_stub.hpp"

#include "launch/return_stub/stub_check.h"

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
    u32 next = 0;
    for (const Extent& e : extents) {
        if (e.fileSector != next || e.count == 0) return R::err(ReturnStubError::NotContiguous);
        next += e.count;
    }
    if (next != (dolSize + 511u) / 512u) return R::err(ReturnStubError::SectorMismatch);

    const u32 argvSize = static_cast<u32>(argvBlock.size());
    const std::size_t size = NLRS_HEAD_BYTES + nlrs_argv_padded(argvSize) + extents.size() * 8u;
    if (size > NLRS_PARAMS_MAX) return R::err(ReturnStubError::TooManyExtents);

    std::vector<u8> b(size, 0);
    put32(b, 0x00, NLRS_MAGIC);
    put32(b, 0x04, NLRS_VERSION);
    put32(b, 0x08, dolSize);
    put32(b, 0x0C, static_cast<u32>(extents.size()));
    put32(b, 0x10, argvSize);
    for (std::size_t i = 0; i < argvBlock.size(); ++i) b[NLRS_HEAD_BYTES + i] = argvBlock[i];
    std::size_t at = NLRS_HEAD_BYTES + nlrs_argv_padded(argvSize);
    for (const Extent& e : extents) {
        put32(b, at, e.lba);
        put32(b, at + 4, e.count);
        at += 8;
    }
    put32(b, 0x14, nlrs_checksum(b.data() + NLRS_HEAD_BYTES, static_cast<u32>(size - NLRS_HEAD_BYTES)));
    return R::ok(std::move(b));
}

}  // namespace nl
