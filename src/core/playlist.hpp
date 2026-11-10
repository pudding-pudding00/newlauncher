#pragma once
// Hintergrundmusik (Spec 3.7): Zufallsreihenfolge der Stücke und kleine reine Helfer für den Musik-Thread.

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "core/fs.hpp"
#include "core/types.hpp"

namespace nl {

// Mischt rundenweise: jede Runde spielt jedes Stück genau einmal. Ab zwei Stücken beginnt eine Runde
// nie mit dem letzten Stück der vorigen. Eigener xorshift32, damit Host und Wii gleich mischen.
class Playlist {
public:
    Playlist(std::vector<std::string> files, u32 seed);
    bool empty() const { return files_.empty(); }
    std::size_t size() const { return files_.size(); }
    const std::string& next();  // nicht bei allBroken() aufrufen; überspringt kaputte Stücke
    void markBroken();          // das zuletzt von next() gelieferte Stück ist nicht abspielbar
    bool allBroken() const { return brokenCount_ >= files_.size(); }  // auch bei leerer Liste: dann Musik aus

private:
    u32 random();
    void shuffle();

    std::vector<std::string> files_;
    std::vector<std::size_t> order_;
    std::vector<bool> broken_;
    std::size_t brokenCount_ = 0;
    std::size_t pos_ = 0;
    u32 state_;
    bool hasLast_ = false;
    std::size_t last_ = 0;
};

int asndVolume(int percent);  // 0..100 → 0..255, außerhalb begrenzt

// Volle Pfade aller .ogg-Dateien (Groß/klein egal) in dir, sortiert; Ordner übergangen, fehlt dir → leer.
std::vector<std::string> musicFiles(FileSystem& fs, std::string_view dir);

}  // namespace nl
