#pragma once

namespace nl {

// SaveWorker (Sichern/Zurückspielen) und CloudWorker (Pull/Upload) schreiben dieselben Save-Ordner, Sicherungen und
// dasselbe Journal saves/restore.ini. Sie laufen deshalb nie gleichzeitig; das Start-Tor hat Vorrang vor dem
// Hintergrund-Upload (den es abbricht) und wartet auf einen laufenden lokalen Auftrag.
struct SaveJobs {
    bool local = false;  // SaveWorker läuft
    bool cloud = false;  // CloudWorker läuft
    bool gate = false;   // Start-Tor offen
};

constexpr bool mayStartLocalSaveJob(const SaveJobs& j) { return !j.local && !j.cloud && !j.gate; }
constexpr bool mayStartCloudUpload(const SaveJobs& j) { return !j.local && !j.cloud && !j.gate; }
constexpr bool maySubmitCloudGate(const SaveJobs& j) { return !j.local && !j.cloud; }

}  // namespace nl
