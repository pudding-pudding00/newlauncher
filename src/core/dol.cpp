#include "core/dol.hpp"

#include <string>

#include "core/bin_io.hpp"
#include "core/launch_layout.hpp"

namespace nl {

namespace {

Result<DolHeader> invalid(std::string context) { return Result<DolHeader>::err({ErrorCode::InvalidData, std::move(context)}); }

bool within(u64 begin, u64 end, u64 lo, u64 hi) { return begin >= lo && end <= hi; }

// Ziel [address, address+size) ganz in MEM1 oder ganz in MEM2 unterhalb des reservierten Bereichs.
bool validTarget(u32 address, u32 size) {
    const u64 begin = address;
    const u64 end = begin + size;
    return within(begin, end, kMem1Begin, kMem1End) || within(begin, end, kMem2Begin, kDolBufferAddr);
}

}  // namespace

Result<DolHeader> parseDol(std::span<const u8> head, u64 fileSize) {
    if (head.size() < kDolHeaderBytes || fileSize < kDolHeaderBytes) return invalid("DOL kürzer als Header");

    DolHeader h;
    BeReader r(head.first(kDolHeaderBytes));
    for (DolSection& s : h.sections) r.u32(s.offset);
    for (DolSection& s : h.sections) r.u32(s.address);
    for (DolSection& s : h.sections) r.u32(s.size);
    r.u32(h.bssAddress);
    r.u32(h.bssSize);
    r.u32(h.entry);
    if (!r.ok()) return invalid("DOL-Header unlesbar");

    bool entryInText = false;
    bool anyText = false;
    for (std::size_t i = 0; i < h.sections.size(); ++i) {
        const DolSection& s = h.sections[i];
        if (s.size == 0) continue;
        const std::string name = "Sektion " + std::to_string(i);
        if (u64(s.offset) + s.size > fileSize) return invalid(name + " endet hinter Dateiende");
        if (!validTarget(s.address, s.size)) return invalid(name + " außerhalb des Speichers");
        if (i < kDolTextSections) {
            anyText = true;
            if (h.entry >= s.address && u64(h.entry) < u64(s.address) + s.size) entryInText = true;
        }
    }
    if (h.bssSize != 0 && !validTarget(h.bssAddress, h.bssSize)) return invalid("BSS außerhalb des Speichers");
    if (!anyText) return invalid("DOL ohne Text-Sektion");
    if (!entryInText) return invalid("Einsprung außerhalb aller Text-Sektionen");
    return Result<DolHeader>::ok(h);
}

}  // namespace nl
