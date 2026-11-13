#pragma once
// Fehlertexte für den Preflight-Dialog (Spec 9, M3) und UTF-8-sicheres Kürzen mit „…“.

#include <cstddef>
#include <string_view>
#include <vector>

#include "core/launch_plan.hpp"

namespace nl::ui {

// Kopiert src nach out (immer NUL-terminiert). Passt es nicht, endet out mit „…“; ein UTF-8-Zeichen
// wird nie zerschnitten. Rückgabe = geschriebene Bytes ohne NUL.
std::size_t copyEllipsized(char* out, std::size_t cap, std::string_view src);

// Titel und Text zu einem Fehler aus preflightEmulator. Rückgabe = Länge des Textes.
int launchErrorText(const LaunchError& e, char* title, std::size_t titleCap, char* text, std::size_t textCap);

// Titel „Hinweis“ und Text zu Preflight-Hinweisen (Spec 7.3): je Hinweis eine Zeile, dann „A erneut …“.
int launchWarningText(const std::vector<LaunchWarning>& warnings, char* title, std::size_t titleCap, char* text,
                      std::size_t textCap, std::string_view emuName = {});  // emuName: Hinweis EmulatorFs (M16)

}  // namespace nl::ui
