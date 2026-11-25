#pragma once
// Szenen (Spec 4.1): ein Stapel aus Vollbild-Szenen und Overlays; Overlays blenden 200 ms ein/aus.

#include <array>

#include "core/easing.hpp"
#include "core/game.hpp"
#include "core/net_error.hpp"
#include "core/theme.hpp"
#include "core/ui_sound.hpp"
#include "gfx/draw_list.hpp"
#include "gfx/font.hpp"
#include "gfx/shapes.hpp"
#include "ui/input.hpp"

namespace nl::ui {

// Pro Frame von main befüllt; Szenen lesen nur.
struct UiContext {
    float width = 640.0f, height = 480.0f;
    const Theme* theme = nullptr;
    bool background = false;           // Theme-Paket hat ein Hintergrundbild (gfx::kBackgroundTexture)
    float backgroundU1 = 1.0f, backgroundV1 = 1.0f;  // sichtbarer Teil der aufgefüllten Textur
    const gfx::FontSet* fonts = nullptr;
    const gfx::AtlasShapes* shapes = nullptr;
    ThemeMode themeMode = ThemeMode::Auto;
    int minuteOfDay = 0;  // 0..1439
    int day = 1, month = 1, year = 2026, weekday = 0;  // weekday 0 = Montag
    std::array<s8, 4> battery{-1, -1, -1, -1};         // Wiimote P1..P4: -1 = nicht verbunden, sonst 0..4 Balken
    WifiStatus wifi = WifiStatus::Disconnected;        // aktueller WLAN-Stand für die untere Leiste
};

enum class AppCommand : u8 {
    None, ExitToHbc, WiiMenu, PowerOff, CycleTheme, ToggleFavorite, Launch,
    SetN64Method,  // game + n64Choice
    OpenVcMap,
    SaveVcMap,
    FetchCovers,
    SettingsChanged,  // Einstellungsszene hat die Live-Config geändert (Sprache/Theme/Audio sofort anwenden)
    SaveSettings,     // config.ini schreiben
    Rescan,           // Bibliothek neu scannen
    OpenEmulators,    // Emulator-Verwaltung öffnen (Spec 6.9)
    EmuFetchList,     // OSC-Liste laden
    EmuInstall,       // Ziele: EmulatorsScene::takeTargets()
    EmuInstallAll,
    EmuCancel,
    OpenAbout,        // Seite „Über“ öffnen (M11, Spec v2 3.4)
    OpenSysInfo,      // Systeminfo öffnen (M13, Spec v2 6.1)
    SaveSysInfo,      // <data>/sysinfo.txt schreiben
    OpenSetup,        // Einrichtungs-Assistent öffnen (M13, Spec v2 6.4)
    SetupNetTest,     // Assistent: Verbindungstest (mit Zeitabgleich bei TLS) im NetService
    FinishSetup,      // Assistent: config.ini (und emulators.ini) schreiben, Szene schließen
    OpenOptions,      // Spieloptionen öffnen (M14): game gesetzt = pro Spiel, sonst global für console
    SaveOptions,      // Options-Szene geschlossen mit Änderungen: OptionsScene::model() schreiben
    OpenCheats,       // Cheat-Auswahl öffnen (M15): game = Wii- oder GC-Spiel
    FetchCheats,      // Codeliste laden: CheatsScene::id6(), overwrite()
    SaveCheats,       // Cheat-Szene geschlossen mit Änderungen: CheatsScene::model().selText() als .sel schreiben
    ToggleCategory,   // Details (M17): game in Kategorie category (Index der setCategories-Liste) an/aus
    OpenParental,     // Kindersicherung öffnen (M17); mit PIN erst nach der PIN-Abfrage
    ChangePin,        // Kindersicherung: neue PIN zweimal abfragen (PinScene im Modus Set)
    RemovePin,        // Kindersicherung: PIN löschen, Kindersicherung aus
    PinEntered,       // PinScene: vier Ziffern bestätigt, PinScene::pinText()
    ReadDisc,         // HOME-Menü (M19): Laufwerk im Hintergrund lesen
    SelfUpdate,       // Einstellungen (M20): Update aus dem GitHub-Release suchen und installieren
    RestartSelf,      // Einstellungen (M20): newlauncher von der SD neu laden
};
const char* appCommandName(AppCommand c);

class Scene;
struct SceneRequests {
    Scene* push = nullptr;
    bool pop = false;
    AppCommand command = AppCommand::None;
    const Game* game = nullptr;  // Ziel von ToggleFavorite, Launch und SetN64Method
    u8 n64Choice = 0;            // SetN64Method: 0 = Automatisch, sonst 1 + N64Method
    u8 category = 0;             // ToggleCategory: Index in der Kategorienliste der Detailansicht
    Console console = Console::Wii;  // OpenOptions ohne game: Wii oder Gc
    UiSound sound = UiSound::None;  // main spielt ihn nach dem Update
};

// Klang-Regel (Spec 3.7): Overlay öffnen → Open, schließen → Back, Befehl → Confirm, Fokuswechsel → Move.
// Ein schon gesetzter Klang bleibt; Szenen setzen Confirm selbst, wenn A ohne Befehl wirkt.
inline void deriveSound(SceneRequests& out, bool focusMoved) {
    if (out.sound != UiSound::None) return;
    if (out.push != nullptr) out.sound = UiSound::Open;
    else if (out.pop) out.sound = UiSound::Back;
    else if (out.command != AppCommand::None) out.sound = UiSound::Confirm;
    else if (focusMoved) out.sound = UiSound::Move;
}

// Ruft deriveSound beim Verlassen von update auf, auch nach frühem return.
class FocusSound {
public:
    FocusSound(SceneRequests& out, const int& focus) : out_(out), focus_(focus), before_(focus) {}
    ~FocusSound() {
        if (!silent_) deriveSound(out_, focus_ != before_);
    }
    FocusSound(const FocusSound&) = delete;
    FocusSound& operator=(const FocusSound&) = delete;
    void silence() { silent_ = true; }  // dieser Frame bleibt still

private:
    SceneRequests& out_;
    const int& focus_;
    int before_;
    bool silent_ = false;
};

class Scene {
public:
    virtual ~Scene() = default;
    virtual void update(const UiContext& ctx, const InputFrame& in, float dtMs, SceneRequests& out) = 0;
    virtual void draw(const UiContext& ctx, gfx::DrawList& dl) = 0;
    virtual bool isOverlay() const { return false; }
    virtual void onEnter() {}
};

inline constexpr float kOverlayFadeMs = 200.0f;

// Besitzt keine Szenen; feste Tiefe 4.
class SceneStack {
public:
    void push(Scene* s);  // blendet die oberste Szene gerade aus, folgt s erst danach
    // topRequests (optional) erhält die Anforderung der obersten Szene, z. B. das Spiel zu ToggleFavorite.
    AppCommand update(const UiContext& ctx, const InputFrame& in, float dtMs, SceneRequests* topRequests = nullptr);
    void draw(const UiContext& ctx, gfx::DrawList& dl);
    Scene* top() const { return depth_ > 0 ? entries_[depth_ - 1].scene : nullptr; }
    int depth() const { return depth_; }
    bool contains(const Scene* s) const;  // auf dem Stapel oder nach dem Ausblenden angefordert

private:
    static constexpr int kMaxDepth = 4;
    struct Entry {
        Scene* scene = nullptr;
        Tween alpha;
        bool popping = false;
    };
    int lowestVisible() const;  // unterste Szene, die gezeichnet/aktualisiert wird

    Entry entries_[kMaxDepth];
    int depth_ = 0;
    Scene* pushAfterPop_ = nullptr;  // pop + push in einer Anforderung: nach dem Ausblenden öffnen
};

}  // namespace nl::ui
