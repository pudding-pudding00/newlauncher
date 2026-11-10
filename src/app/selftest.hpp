#pragma once
// Selbsttest-DOL (nur Ziel nl_selftest, Define NL_SELFTEST): spielt geskriptete Eingaben gegen die
// Fixture-Bibliothek ab (tests/fixtures/sd), prüft den UI-Zustand mit NL_ASSERT, zählt Allokationen
// im Leerlauf und löst am Ende absichtlich einen Absturz aus, damit der Smoke-Test auch den
// Absturzbildschirm sieht.

#include "app/cover_service.hpp"
#include "core/config.hpp"
#include "core/library_store.hpp"
#include "ui/debug_overlay.hpp"
#include "ui/details_scene.hpp"
#include "ui/input.hpp"
#include "ui/keyboard.hpp"
#include "ui/main_scene.hpp"
#include "ui/scene.hpp"
#include "ui/settings_scene.hpp"
#include "ui/vc_map_scene.hpp"

namespace nl::app {

struct SelftestView {
    const ui::MainScene& main;
    const ui::SceneStack& scenes;
    const ui::DebugOverlay& debug;
    bool scanRunning;  // Hintergrund-Scan aktiv (darf im Leerlauf-Fenster nicht laufen)
    const LibraryStore& store;
    const ui::UiContext& ctx;
    const ui::Keyboard& keyboard;
    const ui::Scene* dialog;  // Fehlerdialog beim Start (M3)
    const ui::DetailsScene* details;  // Detailansicht (M4: Startmethode)
    const ui::VcMapScene* vcMap;      // VC-Zuordnungsdialog (M4)
    const ui::SettingsScene* settings;  // Einstellungen hinter der HOME-Kachel (M6)
    const Config* config;               // live geänderte Konfiguration (M6)
    const CoverService* covers;       // Cover-Cache und -Thread (M5)
};

ui::RawInput selftestInput(int frame, const SelftestView& v);
void selftestAfterFrame(int frame, const SelftestView& v);

}  // namespace nl::app
