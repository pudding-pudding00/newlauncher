#include "core/extent_units.hpp"

namespace nl {

namespace {
constexpr u64 kSector = 512;
constexpr u64 kSectorLimit = u64{1} << 32;  // Extent und FragList zählen in u32
}  // namespace

Result<std::vector<Extent>, ExtentError> runsToExtents(std::span<const FsRun> runs, u32 unitBytes, u64 partLba,
                                                       u32 deviceSectorBytes, u64 fileSize) {
    using R = Result<std::vector<Extent>, ExtentError>;
    std::vector<Extent> out;
    if (unitBytes == 0 || unitBytes % kSector != 0) return R::err(ExtentError::Unsupported);
    if (deviceSectorBytes == 0 || deviceSectorBytes % kSector != 0) return R::err(ExtentError::Unsupported);
    if (fileSize == 0) return R::ok(std::move(out));

    const u64 perUnit = unitBytes / kSector;
    const u64 part512 = partLba * (deviceSectorBytes / kSector);
    const u64 fileSectors = (fileSize + kSector - 1) / kSector;
    const u64 needUnits = (fileSize + unitBytes - 1) / unitBytes;
    if (fileSectors > kSectorLimit) return R::err(ExtentError::Overflow);

    u64 nextUnit = 0;
    for (const FsRun& run : runs) {
        if (nextUnit >= needUnits) break;  // Läufe hinter dem Dateiende zählen nicht
        if (run.units == 0) continue;
        if (run.fileUnit != nextUnit) return R::err(ExtentError::Gap);
        if (run.hole) return R::err(ExtentError::Unsupported);
        const u64 units = run.units < needUnits - nextUnit ? run.units : needUnits - nextUnit;
        const u64 fileSector = run.fileUnit * perUnit;
        // Die letzte Einheit endet am Dateiende, nicht an der Einheitengrenze.
        const u64 count = (fileSector + units * perUnit > fileSectors ? fileSectors - fileSector : units * perUnit);
        if (run.devUnit > (kSectorLimit - part512) / perUnit) return R::err(ExtentError::Overflow);
        const u64 lba = part512 + run.devUnit * perUnit;
        if (lba + count > kSectorLimit) return R::err(ExtentError::Overflow);
        if (!out.empty() && u64{out.back().lba} + out.back().count == lba &&
            u64{out.back().fileSector} + out.back().count == fileSector) {
            out.back().count += static_cast<u32>(count);
        } else {
            out.push_back({static_cast<u32>(fileSector), static_cast<u32>(lba), static_cast<u32>(count)});
        }
        nextUnit += units;
    }
    if (nextUnit < needUnits) return R::err(ExtentError::Gap);
    return R::ok(std::move(out));
}

}  // namespace nl
