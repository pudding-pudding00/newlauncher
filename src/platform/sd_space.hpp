#pragma once
// Freier Platz auf der SD-Karte (Spec 6.9, Installationsschritt 1). Kann beim ersten Aufruf auf FAT32 einige
// Sekunden dauern (libfat zählt freie Cluster) – nur im Worker-Thread aufrufen.

#include "core/types.hpp"

namespace nl::platform {

u64 sdFreeBytes();  // 0, wenn statvfs scheitert

}  // namespace nl::platform
