#include "core/save_lock.hpp"

#include <doctest/doctest.h>

using namespace nl;

// SaveWorker und CloudWorker schreiben dieselben Ordner und dasselbe Journal (saves/restore.ini): nie gleichzeitig.
TEST_CASE("save_lock: Sichern/Zurückspielen nur ohne Cloud-Auftrag und ohne offenes Tor") {
    CHECK(mayStartLocalSaveJob({}));
    CHECK_FALSE(mayStartLocalSaveJob({.local = true}));
    CHECK_FALSE(mayStartLocalSaveJob({.cloud = true}));
    CHECK_FALSE(mayStartLocalSaveJob({.gate = true}));
}

TEST_CASE("save_lock: Hintergrund-Upload wartet auf den lokalen Auftrag") {
    CHECK(mayStartCloudUpload({}));
    CHECK_FALSE(mayStartCloudUpload({.local = true}));
    CHECK_FALSE(mayStartCloudUpload({.cloud = true}));
    CHECK_FALSE(mayStartCloudUpload({.gate = true}));
}

TEST_CASE("save_lock: Start-Tor schickt seinen Auftrag erst, wenn beide Worker frei sind") {
    CHECK(maySubmitCloudGate({.gate = true}));
    CHECK_FALSE(maySubmitCloudGate({.local = true, .gate = true}));
    CHECK_FALSE(maySubmitCloudGate({.cloud = true, .gate = true}));
}
