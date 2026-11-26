#pragma once
// ISFS-Rechte-Patch im Speicher des laufenden IOS (M26). Nur mit AHBPROT aus (<ahb_access/> in der meta.xml):
// danach darf der Launcher unter /title/<hi>/<lo>/data schreiben. Der Patch lebt bis zum nächsten IOS-Neuladen.

namespace nl::platform {

enum class IosPatchResult { Applied, Already, NotFound, NoAhbprot, Emulated };

// Dolphin → Emulated (dort braucht es keinen Patch); AHBPROT an → NoAhbprot; Muster fehlt → NotFound.
IosPatchResult applyIsfsPermissionPatch();

// Für den Marker NL_IOSPATCH: "applied", "already", "notfound", "noahbprot", "emulated".
const char* iosPatchName(IosPatchResult r);

// true für Applied, Already und Emulated: dann darf isfs_fs schreiben.
inline bool nandWritable(IosPatchResult r) {
    return r == IosPatchResult::Applied || r == IosPatchResult::Already || r == IosPatchResult::Emulated;
}

}  // namespace nl::platform
