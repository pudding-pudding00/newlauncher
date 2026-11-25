#include "core/self_update.hpp"
#include <doctest/doctest.h>

using namespace nl;

namespace {
constexpr std::string_view kManifestUrl = "https://github.com/o/r/releases/latest/download/update.ini";
}

TEST_CASE("versionNewer vergleicht numerisch je Teil") {
    CHECK(versionNewer("0.1.0", "0.0.1"));
    CHECK(versionNewer("0.10.0", "0.9.9"));
    CHECK(versionNewer("1.0", "0.99.99"));
    CHECK(versionNewer("1.0.1", "1.0"));
    CHECK_FALSE(versionNewer("1.0", "1.0.0"));
    CHECK_FALSE(versionNewer("0.0.1", "0.0.1"));
    CHECK_FALSE(versionNewer("0.0.1", "0.1.0"));
    // Kaputte Angaben sind nie neuer
    CHECK_FALSE(versionNewer("abc", "0.0.1"));
    CHECK_FALSE(versionNewer("1..2", "0.0.1"));
    CHECK_FALSE(versionNewer("", "0.0.1"));
    CHECK_FALSE(versionNewer("1.2.x", "0.0.1"));
}

TEST_CASE("parseUpdateManifest: Name relativ zur Manifest-URL") {
    auto m = parseUpdateManifest(
        "[update]\r\nversion = 0.2.0\r\nzip = newlauncher-0.2.0.zip\r\nzip_size = 123456\r\nunzipped_size = 400000\r\n",
        kManifestUrl);
    REQUIRE(m);
    CHECK(m->version == "0.2.0");
    CHECK(m->zipUrl == "https://github.com/o/r/releases/latest/download/newlauncher-0.2.0.zip");
    CHECK(m->zipSize == 123456u);
    CHECK(m->unzippedSize == 400000u);
}

TEST_CASE("parseUpdateManifest: absolute HTTPS-URL bleibt") {
    auto m = parseUpdateManifest("[update]\nversion=1.0\nzip=https://cdn.example/x.zip\nzip_size=10\nunzipped_size=20\n",
                                 kManifestUrl);
    REQUIRE(m);
    CHECK(m->zipUrl == "https://cdn.example/x.zip");
}

TEST_CASE("parseUpdateManifest lehnt Kaputtes ab") {
    const std::string ok = "version=1.0\nzip=a.zip\nzip_size=10\nunzipped_size=20\n";
    CHECK(parseUpdateManifest("[update]\n" + ok, kManifestUrl));
    CHECK_FALSE(parseUpdateManifest(ok, kManifestUrl));  // kein Abschnitt
    CHECK_FALSE(parseUpdateManifest("[update]\nversion=x\nzip=a.zip\nzip_size=10\nunzipped_size=20\n", kManifestUrl));
    CHECK_FALSE(parseUpdateManifest("[update]\nversion=1.0\nzip_size=10\nunzipped_size=20\n", kManifestUrl));
    CHECK_FALSE(parseUpdateManifest("[update]\nversion=1.0\nzip=a.zip\nzip_size=0\nunzipped_size=20\n", kManifestUrl));
    CHECK_FALSE(parseUpdateManifest("[update]\nversion=1.0\nzip=a.zip\nzip_size=abc\nunzipped_size=20\n", kManifestUrl));
    CHECK_FALSE(
        parseUpdateManifest("[update]\nversion=1.0\nzip=a.zip\nzip_size=999999999\nunzipped_size=20\n", kManifestUrl));
    // Kein Rückfall auf HTTP, keine Pfade im Namen
    CHECK_FALSE(
        parseUpdateManifest("[update]\nversion=1.0\nzip=http://x/a.zip\nzip_size=10\nunzipped_size=20\n", kManifestUrl));
    CHECK_FALSE(parseUpdateManifest("[update]\nversion=1.0\nzip=../a.zip\nzip_size=10\nunzipped_size=20\n", kManifestUrl));
    CHECK_FALSE(parseUpdateManifest("[update]\nversion=1.0\nzip=a/b.zip\nzip_size=10\nunzipped_size=20\n", kManifestUrl));
    CHECK_FALSE(parseUpdateManifest("[update]\n" + ok, "http://x/update.ini"));
}

TEST_CASE("selfUpdateApp beschreibt newlauncher für den Installer") {
    UpdateManifest m{"0.3.0", "https://x/a.zip", 10, 20};
    OscApp a = selfUpdateApp(m);
    CHECK(a.name == "newlauncher");
    CHECK(a.version == "0.3.0");
    CHECK(a.zipSize == 10u);
    CHECK(a.unzippedSize == 20u);
}

TEST_CASE("Standard-URL zeigt auf das neueste GitHub-Release") {
    CHECK(kDefaultUpdateUrl == "https://github.com/pudding-pudding00/newlauncher/releases/latest/download/update.ini");
}
