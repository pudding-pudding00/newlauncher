#include "core/playlist.hpp"
#include "support/mem_fs.hpp"
#include <algorithm>
#include <doctest/doctest.h>
#include <string>
#include <vector>

namespace {
std::vector<std::string> files(int n) {
    std::vector<std::string> v;
    for (int i = 0; i < n; ++i) v.push_back("sd:/m/" + std::to_string(i) + ".ogg");
    return v;
}
}  // namespace

TEST_CASE("Playlist: jede Runde enthält jedes Stück einmal") {
    const std::vector<std::string> in = files(5);
    nl::Playlist p(in, 42);
    CHECK(p.size() == 5);
    CHECK_FALSE(p.empty());
    for (int round = 0; round < 3; ++round) {
        std::vector<std::string> got;
        for (int i = 0; i < 5; ++i) got.push_back(p.next());
        std::sort(got.begin(), got.end());
        CHECK(got == in);
    }
}

TEST_CASE("Playlist: kein Stück zweimal hintereinander") {
    nl::Playlist p(files(2), 7);
    std::string last = p.next();
    for (int i = 0; i < 200; ++i) {
        const std::string cur = p.next();
        CHECK(cur != last);
        last = cur;
    }
}

TEST_CASE("Playlist: ein Stück wiederholt sich") {
    nl::Playlist p(files(1), 0);
    const std::string a = p.next();
    CHECK(p.next() == a);
    CHECK(p.next() == a);
}

TEST_CASE("Playlist: gleicher seed, gleiche Folge") {
    nl::Playlist a(files(6), 1234), b(files(6), 1234);
    for (int i = 0; i < 30; ++i) CHECK(a.next() == b.next());
}

TEST_CASE("musicFiles filtert .ogg") {
    nl::test::MemFileSystem fs;
    fs.addFile("sd:/newlauncher/music/a.OGG", {1});
    fs.addFile("sd:/newlauncher/music/b.ogg", {1});
    fs.addFile("sd:/newlauncher/music/c.mp3", {1});
    fs.addDir("sd:/newlauncher/music/x.ogg");
    CHECK(nl::musicFiles(fs, "sd:/newlauncher/music") ==
          std::vector<std::string>{"sd:/newlauncher/music/a.OGG", "sd:/newlauncher/music/b.ogg"});
    CHECK(nl::musicFiles(fs, "sd:/fehlt").empty());
}

TEST_CASE("asndVolume") {
    CHECK(nl::asndVolume(60) == 153);
    CHECK(nl::asndVolume(0) == 0);
    CHECK(nl::asndVolume(100) == 255);
    CHECK(nl::asndVolume(150) == 255);
    CHECK(nl::asndVolume(-5) == 0);
}

TEST_CASE("Playlist: kaputte Stücke werden danach übersprungen") {
    nl::Playlist p(files(4), 7);
    const std::string bad = "sd:/m/2.ogg";
    bool marked = false;
    for (int i = 0; i < 40; ++i) {
        const std::string& f = p.next();
        if (marked) CHECK(f != bad);
        if (f == bad) {
            p.markBroken();
            marked = true;
        }
    }
    CHECK(marked);
    CHECK_FALSE(p.allBroken());
}

TEST_CASE("Playlist: ein spielbares Stück reicht, auch über Rundengrenzen") {
    // Review M6/I2: früher zählten Fehler über die Rundengrenze weiter (X,Y | X → drei in Folge → Musik aus).
    for (nl::u32 seed = 1; seed <= 50; ++seed) {
        nl::Playlist p({"g", "x", "y"}, seed);
        int good = 0;
        for (int i = 0; i < 30; ++i) {
            if (p.next() == "g") {
                ++good;
            } else {
                p.markBroken();
            }
            REQUIRE_FALSE(p.allBroken());
        }
        CHECK(good >= 20);  // nach dem ersten Fehlerdurchgang nur noch „g“
    }
}

TEST_CASE("Playlist: allBroken erst, wenn jedes Stück gescheitert ist") {
    nl::Playlist p(files(3), 3);
    CHECK_FALSE(p.allBroken());
    p.next();
    p.markBroken();
    p.next();
    p.markBroken();
    CHECK_FALSE(p.allBroken());
    p.next();
    p.markBroken();
    CHECK(p.allBroken());
    CHECK(nl::Playlist({}, 1).allBroken());  // leer: nichts zu spielen
}
