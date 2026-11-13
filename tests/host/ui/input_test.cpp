#include "ui/input.hpp"
#include <doctest/doctest.h>
#include <initializer_list>
using namespace nl::ui;

namespace {
constexpr float kDt = 16.0f;

int countAction(InputMapper& m, const RawInput& raw, Action a, int frames, float dt = kDt) {
    int n = 0;
    for (int i = 0; i < frames; ++i)
        if (m.update(raw, dt).pressed(a)) ++n;
    return n;
}

RawInput held(nl::u64 bits) {
    RawInput r;
    r.held = bits;
    return r;
}
}  // namespace

TEST_CASE("A löst Confirm einmal aus, nicht bei Halten") {
    InputMapper m;
    CHECK(countAction(m, held(RawInput::A), Action::Confirm, 30) == 1);
    m.update(RawInput{}, kDt);
    CHECK(m.update(held(RawInput::A), kDt).pressed(Action::Confirm));
}

TEST_CASE("Steuerkreuz wiederholt nach 400 ms, dann alle 80 ms") {
    InputMapper m;
    // Druck-Frame + 62 Frames = 992 ms gehalten → 1 + 1 (400 ms) + 7 (480…960 ms)
    CHECK(countAction(m, held(RawInput::DRight), Action::Right, 63) == 9);
}

TEST_CASE("Loslassen setzt die Wiederholung zurück") {
    InputMapper m;
    countAction(m, held(RawInput::DDown), Action::Down, 20);
    m.update(RawInput{}, kDt);
    CHECK(countAction(m, held(RawInput::DDown), Action::Down, 20) == 1);
}

TEST_CASE("Stick innerhalb der Totzone löst nichts aus") {
    InputMapper m;
    RawInput r;
    r.stickX = 0.3f;
    for (int i = 0; i < 40; ++i) CHECK(m.update(r, kDt).actions == 0);
}

TEST_CASE("Stick über der Totzone verhält sich wie Steuerkreuz") {
    InputMapper m;
    RawInput r;
    r.stickX = 0.8f;
    CHECK(countAction(m, r, Action::Right, 63) == 9);
}

TEST_CASE("diagonaler Stick wählt die dominante Achse") {
    InputMapper m;
    RawInput r;
    r.stickX = 0.9f;
    r.stickY = 0.5f;
    InputFrame f = m.update(r, kDt);
    CHECK(f.pressed(Action::Right));
    CHECK_FALSE(f.pressed(Action::Up));
}

TEST_CASE("Stick +Y ist Up") {
    InputMapper m;
    RawInput r;
    r.stickY = 0.9f;
    CHECK(m.update(r, kDt).pressed(Action::Up));
}

TEST_CASE("GC-Tasten bilden auf dieselben Aktionen ab") {
    struct Case { nl::u32 bit; Action action; };
    const Case cases[] = {{RawInput::GcA, Action::Confirm}, {RawInput::GcB, Action::Back},
                          {RawInput::GcL, Action::PrevConsole}, {RawInput::GcR, Action::NextConsole},
                          {RawInput::GcY, Action::Favorite}, {RawInput::GcX, Action::Search},
                          {RawInput::GcUp, Action::Up}, {RawInput::GcLeft, Action::Left},
                          {RawInput::Minus, Action::PrevConsole}, {RawInput::Plus, Action::NextConsole},
                          {RawInput::B, Action::Back}};
    for (const Case& c : cases) {
        InputMapper m;
        CHECK(m.update(held(c.bit), kDt).pressed(c.action));
    }
}

TEST_CASE("1 allein löst Favorite beim Loslassen aus") {
    InputMapper m;
    CHECK_FALSE(m.update(held(RawInput::One), kDt).pressed(Action::Favorite));
    CHECK(m.update(RawInput{}, kDt).pressed(Action::Favorite));
}

TEST_CASE("2 allein löst Search beim Loslassen aus") {
    InputMapper m;
    m.update(held(RawInput::Two), kDt);
    CHECK(m.update(RawInput{}, kDt).pressed(Action::Search));
}

TEST_CASE("1+2 löst nur DebugToggle aus, einmal") {
    InputMapper m;
    int debug = 0, other = 0;
    const RawInput seq[] = {held(RawInput::One), held(RawInput::One | RawInput::Two),
                            held(RawInput::One | RawInput::Two), held(RawInput::Two), RawInput{}};
    for (const RawInput& r : seq) {
        InputFrame f = m.update(r, kDt);
        if (f.pressed(Action::DebugToggle)) ++debug;
        if (f.pressed(Action::Favorite) || f.pressed(Action::Search)) ++other;
    }
    CHECK(debug == 1);
    CHECK(other == 0);
}

TEST_CASE("Start allein → Home beim Loslassen; Z+Start → DebugToggle, kein Home") {
    InputMapper m;
    CHECK_FALSE(m.update(held(RawInput::GcStart), kDt).pressed(Action::Home));
    CHECK(m.update(RawInput{}, kDt).pressed(Action::Home));

    InputMapper m2;
    int home = 0, debug = 0;
    const RawInput seq[] = {held(RawInput::GcZ), held(RawInput::GcZ | RawInput::GcStart),
                            held(RawInput::GcZ), RawInput{}};
    for (const RawInput& r : seq) {
        InputFrame f = m2.update(r, kDt);
        if (f.pressed(Action::Home)) ++home;
        if (f.pressed(Action::DebugToggle)) ++debug;
    }
    CHECK(home == 0);
    CHECK(debug == 1);
}

TEST_CASE("HOME-Taste der Wiimote löst Home beim Drücken aus") {
    InputMapper m;
    CHECK(m.update(held(RawInput::HomeBtn), kDt).pressed(Action::Home));
    CHECK_FALSE(m.update(held(RawInput::HomeBtn), kDt).pressed(Action::Home));
}

TEST_CASE("Zeiger wird gültig → pointerMoved einmal true; Zittern unter 4 px zählt nicht") {
    InputMapper m;
    RawInput r;
    r.pointerValid = true;
    r.pointerX = 100;
    r.pointerY = 100;
    InputFrame f = m.update(r, kDt);
    CHECK(f.pointerValid);
    CHECK(f.pointerMoved);
    CHECK(f.pointerX == 100);
    r.pointerX = 102;
    r.pointerY = 98;
    CHECK_FALSE(m.update(r, kDt).pointerMoved);
    r.pointerX = 98;
    CHECK_FALSE(m.update(r, kDt).pointerMoved);
    r.pointerX = 105;
    CHECK(m.update(r, kDt).pointerMoved);
}

TEST_CASE("IR-Aussetzer an gleicher Stelle gilt nicht als Bewegung") {
    InputMapper m;
    RawInput r;
    r.pointerValid = true;
    r.pointerX = 100;
    r.pointerY = 100;
    CHECK(m.update(r, kDt).pointerMoved);
    RawInput lost;
    m.update(lost, kDt);
    r.pointerX = 101;
    CHECK_FALSE(m.update(r, kDt).pointerMoved);
    r.pointerX = 110;
    CHECK(m.update(r, kDt).pointerMoved);
}

TEST_CASE("kein Zeiger: pointerValid false, pointerMoved false, Tasten funktionieren") {
    InputMapper m;
    InputFrame f = m.update(held(RawInput::A), kDt);
    CHECK_FALSE(f.pointerValid);
    CHECK_FALSE(f.pointerMoved);
    CHECK(f.pressed(Action::Confirm));
}

TEST_CASE("dt 0 und sehr großes dt lösen höchstens eine Wiederholung pro Frame aus") {
    InputMapper m;
    m.update(held(RawInput::DUp), kDt);
    CHECK_FALSE(m.update(held(RawInput::DUp), 0.0f).pressed(Action::Up));
    CHECK(m.update(held(RawInput::DUp), 5000.0f).pressed(Action::Up));
    CHECK_FALSE(m.update(held(RawInput::DUp), 0.0f).pressed(Action::Up));
    CHECK(countAction(m, held(RawInput::DUp), Action::Up, 5, 80.0f) == 5);
}

TEST_CASE("Wiimote + und GC R lösen zusätzlich Options aus (M14)") {
    for (const nl::u32 bit : {nl::u32{RawInput::Plus}, nl::u32{RawInput::GcR}}) {
        InputMapper m;
        const InputFrame f = m.update(held(bit), kDt);
        CHECK(f.pressed(Action::Options));
        CHECK(f.pressed(Action::NextConsole));
    }
    InputMapper m;
    CHECK_FALSE(m.update(held(RawInput::Minus), kDt).pressed(Action::Options));
}

// --- Classic Controller (Spec 10.1) ---

TEST_CASE("Classic-Belegung nach Spec 10.1") {
    struct Row {
        nl::u64 btn;
        Action a;
    };
    const Row rows[] = {
        {RawInput::CcA, Action::Confirm},      {RawInput::CcB, Action::Back},
        {RawInput::CcL, Action::PrevConsole},  {RawInput::CcR, Action::NextConsole},
        {RawInput::CcY, Action::Favorite},     {RawInput::CcX, Action::Search},
        {RawInput::CcPlus, Action::Options},   {RawInput::CcHome, Action::Home},
    };
    for (const Row& r : rows) {
        InputMapper m;
        const InputFrame f = m.update(held(r.btn), kDt);
        CHECK(f.pressed(r.a));
        CHECK(f.actions == (1u << static_cast<nl::u32>(r.a)));  // genau diese eine Aktion
    }
}

TEST_CASE("Classic + löst Optionen aus, aber nicht NextConsole") {
    InputMapper m;
    const InputFrame f = m.update(held(RawInput::CcPlus), kDt);
    CHECK(f.pressed(Action::Options));
    CHECK_FALSE(f.pressed(Action::NextConsole));
}

TEST_CASE("Wiimote + löst NextConsole und Optionen aus") {
    InputMapper m;
    const InputFrame f = m.update(held(RawInput::Plus), kDt);
    CHECK(f.pressed(Action::NextConsole));
    CHECK(f.pressed(Action::Options));
}

TEST_CASE("Classic ZL+ZR löst DebugToggle einmal aus, erneut erst nach Loslassen") {
    InputMapper m;
    const nl::u64 both = RawInput::CcZL | RawInput::CcZR;
    CHECK(countAction(m, held(both), Action::DebugToggle, 30) == 1);
    CHECK(countAction(m, held(RawInput::CcZL), Action::DebugToggle, 5) == 0);  // ZR los, ZL gehalten
    m.update(RawInput{}, kDt);
    CHECK(countAction(m, held(both), Action::DebugToggle, 5) == 1);
}

TEST_CASE("Classic ZL oder ZR einzeln und Classic − sind unbelegt") {
    for (nl::u64 b : {RawInput::CcZL, RawInput::CcZR, RawInput::CcMinus}) {
        InputMapper m;
        for (int i = 0; i < 3; ++i) CHECK(m.update(held(b), kDt).actions == 0);
        CHECK(m.update(RawInput{}, kDt).actions == 0);
    }
}

TEST_CASE("Classic abstecken: Bits verschwinden, keine Aktion und keine Wiederholung") {
    InputMapper m;
    CHECK(countAction(m, held(RawInput::DRight | RawInput::CcA), Action::Right, 10) == 1);
    for (int i = 0; i < 60; ++i) CHECK(m.update(RawInput{}, kDt).actions == 0);
}

TEST_CASE("Classic abstecken während ZL+ZR: kein DebugToggle beim Wiederanstecken ohne neuen Druck") {
    InputMapper m;
    const nl::u64 both = RawInput::CcZL | RawInput::CcZR;
    CHECK(countAction(m, held(both), Action::DebugToggle, 3) == 1);
    CHECK(m.update(RawInput{}, kDt).actions == 0);
    CHECK(countAction(m, held(both), Action::DebugToggle, 3) == 1);  // neuer Druck → einmal
}
