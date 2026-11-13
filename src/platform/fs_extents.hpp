#pragma once
// Extents einer Datei auf NTFS (M16, Spec v2 9.2): fragt libntfs nach den Läufen und rechnet sie über
// core/extent_units in 512-Byte-Sektoren um, absolut auf dem Gerät. FAT bleibt bei core/fat_chain.

#include <string_view>

#include "core/extent_units.hpp"
#include "core/frag_list.hpp"
#include "core/result.hpp"
#include "platform/storage.hpp"

namespace nl::platform {

// path mit Gerät ("usb2:/wii/a.iso"). Ohne NL_FS_NTFS oder auf anderem Dateisystem → Unsupported.
Result<FilePart, ExtentError> fileExtentsFor(const Device& dev, std::string_view path);

// Gerätesektorgröße der Partition: 512 auf der SD (auch im Smoke mit sd_as_disk), sonst die der USB-Platte.
u32 deviceSectorBytes(const Device& dev);

}  // namespace nl::platform
