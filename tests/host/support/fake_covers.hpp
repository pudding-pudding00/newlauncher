#pragma once
// Test-Doppel für ui::CoverProvider: zählt want() pro Spiel-Schlüssel und Priorität,
// meldet für ausgewählte Schlüssel eine Textur mit Alpha.

#include <map>
#include <string>

#include "ui/cover_provider.hpp"

namespace nl::test {

class FakeCovers final : public ui::CoverProvider {
public:
    void want(const Game& g, CoverPriority p) override {
        auto& c = wants[g.key];
        (p == CoverPriority::Visible ? c.visible : c.prefetch) += 1;
    }
    ui::CoverView view(const Game& g) const override {
        const auto it = views.find(g.key);
        return it == views.end() ? ui::CoverView{} : it->second;
    }

    struct Count {
        int visible = 0, prefetch = 0;
    };
    std::map<std::string, Count> wants;
    std::map<std::string, ui::CoverView> views;
};

}  // namespace nl::test
