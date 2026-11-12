#pragma once
// Datenordner <data> (Spec v2 5.1): folgt dem Gerät, von dem der Launcher gestartet wurde.

#include <span>
#include <string>
#include <string_view>

namespace nl {

struct DataCandidate {
    std::string device;  // "sd", "usb", "usb2" …
    bool writable;
    bool hasDataDir;     // <gerät>:/newlauncher existiert
};

struct DataDirChoice {
    std::string path;  // z. B. "usb2:/newlauncher"; leer, wenn kein Gerät taugt
    bool create;       // Ordner muss angelegt werden
    bool readOnly;     // kein beschreibbares Gerät: Einstellungen und Verlauf werden nicht gespeichert
};

// "usb:/apps/x/boot.dol" → "usb" (klein geschrieben); ohne Gerät "".
std::string deviceFromArgv0(std::string_view argv0);

// 1. Startgerät mit Ordner, 2. sd mit Ordner, 3. erstes beschreibbares USB-Gerät mit Ordner,
// 4. anlegen auf Startgerät, sd, erstem beschreibbaren USB-Gerät, 5. nur lesend auf dem ersten Gerät mit Ordner.
// Stufen 1–4 verlangen ein beschreibbares Gerät.
DataDirChoice chooseDataDir(std::string_view argv0, std::span<const DataCandidate> devs);

}  // namespace nl
