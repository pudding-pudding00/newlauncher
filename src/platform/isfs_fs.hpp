#pragma once
// FileSystem für den echten NAND über ISFS (M26). Pfade sehen aus wie "nand:/title/00010000/524d4750/data".
// Lesen geht überall, wo IOS es erlaubt; geschrieben und gelöscht wird nur unter /title/<hi>/<lo>/data
// (core/nand_path), und nur nachdem applyIsfsPermissionPatch das erlaubt hat.

#include "core/fs.hpp"

namespace nl::platform {

class IsfsFileSystem final : public FileSystem {
public:
    // writable = nandWritable(applyIsfsPermissionPatch()); ohne Patch scheitert jedes Schreiben mit Unsupported.
    explicit IsfsFileSystem(bool writable) : writable_(writable) {}

    Result<std::vector<DirEntry>> list(std::string_view dir) override;
    Result<std::size_t> read(std::string_view file, u64 offset, std::span<u8> out) override;
    Result<std::vector<u8>> readAll(std::string_view file, std::size_t maxBytes) override;
    // Über /tmp/<Dateiname> und ISFS_Rename; Eigentümer, Gruppe und Rechte kommen von der alten Datei oder dem Ordner,
    // sonst darf das Spiel seinen Spielstand danach nicht mehr selbst schreiben.
    Result<void> writeAtomic(std::string_view file, std::span<const u8> data) override;
    Result<void> makeDirs(std::string_view dir) override;  // nur unterhalb eines vorhandenen data-Ordners
    Result<void> removeFile(std::string_view file) override;
    Result<void> renameFile(std::string_view from, std::string_view to) override;  // Unsupported

    void setWritable(bool w) { writable_ = w; }

private:
    bool writable_;
};

}  // namespace nl::platform
