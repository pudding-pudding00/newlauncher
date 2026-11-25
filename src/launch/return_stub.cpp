#include "launch/return_stub.hpp"

#include "core/fat_chain.hpp"
#include "core/launch_args.hpp"
#include "platform/log.hpp"
#include "core/return_stub.hpp"
#include "launch/return_stub/stub_check.h"
#include "platform/storage.hpp"
#include "platform/wii_disc.hpp"
#include "return_stub_bin.h"

#include <gccore.h>
#include <sys/stat.h>

#include <cstring>
#include <string>
#include <vector>

namespace nl::launch {

namespace {

inline u8* at(u32 addr) { return reinterpret_cast<u8*>(addr); }

const char* errorName(ReturnStubError e) {
    switch (e) {
        case ReturnStubError::NoExtents: return "no_extents";
        case ReturnStubError::NotContiguous: return "not_contiguous";
        case ReturnStubError::SectorMismatch: return "sector_mismatch";
        case ReturnStubError::DolSize: return "dol_size";
        case ReturnStubError::ArgvTooLarge: return "argv";
        case ReturnStubError::TooManyExtents: return "too_many_extents";
    }
    return "?";
}

}  // namespace

bool installReturnStub(std::string_view selfDol) {
    static_assert(NLRS_STUB_ADDR + NLRS_STUB_MAX == NLRS_PARAMS_ADDR);
    if (selfDol.substr(0, 4) != "sd:/") {
        log::line("NL_RETURN_STUB skip self=%.*s", static_cast<int>(selfDol.size()), selfDol.data());
        return false;
    }
    const platform::Device* sd = platform::deviceTable().byName("sd");
    if (sd == nullptr || !sd->mounted) {
        log::line("NL_RETURN_STUB skip no_sd");
        return false;
    }
    const std::string path(selfDol);
    struct stat st {};
    if (stat(path.c_str(), &st) != 0 || st.st_size <= 0) {
        log::line("NL_RETURN_STUB skip stat");
        return false;
    }
    platform::RawDevice dev(false);
    auto vol = findFatVolumeAt(dev, static_cast<u32>(sd->slot.lba));
    if (!vol.isOk()) {
        log::line("NL_RETURN_STUB skip fat=%u", static_cast<unsigned>(vol.error()));
        return false;
    }
    auto ext = fileExtents(dev, vol.value(), static_cast<u32>(st.st_ino), static_cast<u64>(st.st_size));
    if (!ext.isOk()) {
        log::line("NL_RETURN_STUB skip chain=%u", static_cast<unsigned>(ext.error()));
        return false;
    }
    const std::vector<std::string> args{path};
    auto argv = packArgvBlock(args, kReturnArgvAddr);
    if (!argv.isOk()) {
        log::line("NL_RETURN_STUB skip argv");
        return false;
    }
    auto params = buildReturnParams(static_cast<u32>(st.st_size), ext.value(), argv.value());
    if (!params.isOk()) {
        log::line("NL_RETURN_STUB skip %s", errorName(params.error()));
        return false;
    }
    if (return_stub_bin_size > NLRS_STUB_MAX) {
        log::line("NL_RETURN_STUB skip stub_size");
        return false;
    }

    std::memset(at(NLRS_STUB_ADDR), 0, 0x1800);  // Stub, Parameterblock, Fortschrittscode
    std::memcpy(at(NLRS_STUB_ADDR), return_stub_bin, return_stub_bin_size);
    std::memcpy(at(NLRS_PARAMS_ADDR), params.value().data(), params.value().size());
    DCFlushRange(at(NLRS_STUB_ADDR), 0x1800);
    ICInvalidateRange(at(NLRS_STUB_ADDR), return_stub_bin_size);
    log::line("NL_RETURN_STUB installed extents=%u size=%u", static_cast<unsigned>(ext.value().size()),
              static_cast<unsigned>(st.st_size));
    return true;
}

bool returnStubInstalled() {
    if (return_stub_bin_size > NLRS_STUB_MAX) return false;
    DCInvalidateRange(at(NLRS_STUB_ADDR), 0x1800);
    return std::memcmp(at(NLRS_STUB_ADDR), return_stub_bin, return_stub_bin_size) == 0 &&
           nlrs_params_ok(at(NLRS_PARAMS_ADDR)) == 1;
}

u32 takeReturnStubStage() {
    auto* stage = reinterpret_cast<volatile u32*>(NLRS_STAGE_ADDR | 0x40000000u);
    const u32 v = *stage;
    *stage = 0;
    return v;
}

}  // namespace nl::launch
