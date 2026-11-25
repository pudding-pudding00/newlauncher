#pragma once
// Rückkehr-Stub (M20, Spec 4): vor dem Start einer Homebrew-DOL bei 0x80001800 ablegen, damit deren exit()
// newlauncher von der SD neu lädt statt in den Homebrew Channel zurückzukehren.

#include "core/types.hpp"

#include <span>
#include <string>
#include <string_view>

namespace nl::launch {

// Nur, wenn selfDol auf "sd:/" liegt und dessen Extents (FAT32) ermittelbar sind; sonst bleibt der
// vorhandene Stub (HBC) unangetastet. Muss vor unmountAll laufen. Loggt NL_RETURN_STUB.
bool installReturnStub(std::string_view selfDol, std::span<const std::string> extraArgs = {});

// Liegt unser Stub (nicht der des HBC) bei 0x80001800? Dann würde exit() newlauncher neu laden.
bool returnStubInstalled();

// Fortschrittscode, den ein gescheiterter Stub hinterlassen hat (0 = keiner); löscht ihn.
u32 takeReturnStubStage();

}  // namespace nl::launch
