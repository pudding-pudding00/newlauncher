#pragma once
// Selbsttest-DOL (nur Ziel nl_selftest, Define NL_SELFTEST): spielt geskriptete Eingaben gegen die
// Fixture-Bibliothek ab (tests/fixtures/sd), prüft den UI-Zustand mit NL_ASSERT, zählt Allokationen
// im Leerlauf und löst am Ende absichtlich einen Absturz aus, damit der Smoke-Test auch den
// Absturzbildschirm sieht.

#include "core/library_store.hpp"
#include "ui/debug_overlay.hpp"
#include "ui/input.hpp"
#include "ui/keyboard.hpp"
#include "ui/main_scene.hpp"
#include "ui/scene.hpp"

namespace nl::app {

struct SelftestView {
    const ui::MainScene& main;
    const ui::SceneStack& scenes;
    const ui::DebugOverlay& debug;
    bool scanRunning;  // Hintergrund-Scan aktiv (darf im Leerlauf-Fenster nicht laufen)
    const LibraryStore& store;
    const ui::UiContext& ctx;
    const ui::Keyboard& keyboard;
};

ui::RawInput selftestInput(int frame, const SelftestView& v);
void selftestAfterFrame(int frame, const SelftestView& v);

}  // namespace nl::app
