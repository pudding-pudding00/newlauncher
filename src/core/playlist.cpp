#include "core/playlist.hpp"

#include <algorithm>
#include <utility>

namespace nl {

Playlist::Playlist(std::vector<std::string> files, u32 seed) : files_(std::move(files)), state_(seed == 0 ? 1 : seed) {
    order_.resize(files_.size());
    pos_ = order_.size();  // erste next() mischt
}

u32 Playlist::random() {
    u32 x = state_;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    state_ = x;
    return x;
}

void Playlist::shuffle() {
    for (std::size_t i = 0; i < order_.size(); ++i) order_[i] = i;
    for (std::size_t i = order_.size(); i > 1; --i) {
        const std::size_t j = random() % i;
        std::swap(order_[i - 1], order_[j]);
    }
    // Ein Tausch mit dem Rundenende erhält die Permutation und vermeidet die Wiederholung an der Rundengrenze.
    if (hasLast_ && order_.size() >= 2 && order_.front() == last_) std::swap(order_.front(), order_.back());
    pos_ = 0;
}

const std::string& Playlist::next() {
    if (pos_ >= order_.size()) shuffle();
    last_ = order_[pos_++];
    hasLast_ = true;
    return files_[last_];
}

int asndVolume(int percent) {
    return std::clamp(percent, 0, 100) * 255 / 100;
}

std::vector<std::string> musicFiles(FileSystem& fs, std::string_view dir) {
    std::vector<std::string> out;
    auto entries = fs.list(dir);
    if (!entries.isOk()) return out;
    for (const DirEntry& e : entries.value()) {
        if (e.isDir || e.name.size() < 4) continue;
        std::string ext = e.name.substr(e.name.size() - 4);
        std::transform(ext.begin(), ext.end(), ext.begin(), [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; });
        if (ext != ".ogg") continue;
        out.push_back(std::string(dir) + "/" + e.name);
    }
    std::sort(out.begin(), out.end());
    return out;
}

}  // namespace nl
