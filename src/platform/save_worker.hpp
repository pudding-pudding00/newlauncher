#pragma once
// M26 Spielstände sichern und wiederherstellen im Hintergrund: ein LWP-Thread, ein Auftrag zugleich. Kein Netz,
// deshalb nicht im NetService. Der Haupt-Thread holt das Ergebnis mit poll() und loggt die Marker.

#include <optional>
#include <string>

#include <ogc/lwp.h>
#include <ogc/mutex.h>

#include "core/fs.hpp"
#include "core/save_backup.hpp"
#include "core/save_locator.hpp"

namespace nl::platform {

struct SaveJobResult {
    enum class Kind : u8 { Backup, Restore, Scan } kind = Kind::Backup;
    bool ok = false;
    std::string detail;  // Fehlertext bei !ok, sonst leer
    BackupInfo backup;   // nur Backup
    std::string unitId;  // nur Scan: Einheit, die vermessen wurde
    u64 bytes = 0;       // nur Scan
    u32 files = 0;       // nur Scan
};

class SaveWorker {
public:
    // fs: RoutingFileSystem (nand: und sd:/usb:); dataDir z. B. "sd:/newlauncher"; device = Gerätename im Paket.
    SaveWorker(FileSystem& fs, std::string dataDir, std::string device);
    ~SaveWorker();
    SaveWorker(const SaveWorker&) = delete;
    SaveWorker& operator=(const SaveWorker&) = delete;

    bool startBackup(SaveUnit u, std::string title);  // false = läuft schon / Thread startet nicht
    bool startRestore(SaveUnit u, std::string zipPath, std::string title);
    bool startScan(SaveUnit u);  // Größe und Dateizahl im Hintergrund (liest die ganze Einheit, darf den Haupt-Thread nicht blockieren)
    bool running() const { return running_; }
    std::optional<SaveJobResult> poll();  // genau einmal nach dem Ende
    void join();                          // vor dem Aushängen der Laufwerke

private:
    bool launch(SaveJobResult::Kind kind);
    static void* entry(void* self);

    FileSystem& fs_;
    std::string dataDir_;
    std::string device_;
    lwp_t thread_ = LWP_THREAD_NULL;
    mutex_t mutex_ = LWP_MUTEX_NULL;
    SaveJobResult::Kind kind_ = SaveJobResult::Kind::Backup;
    SaveUnit unit_;
    std::string zipPath_;
    std::string title_;
    volatile bool running_ = false;
    std::optional<SaveJobResult> result_;
};

}  // namespace nl::platform
