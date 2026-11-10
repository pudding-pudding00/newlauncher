#pragma once
// Fehlertexte für den Preflight-Dialog (Spec 9, M3) und UTF-8-sicheres Kürzen mit „…“.

#include <cstddef>
#include <string_view>

#include "core/result.hpp"

namespace nl::ui {

// Kopiert src nach out (immer NUL-terminiert). Passt es nicht, endet out mit „…“; ein UTF-8-Zeichen
// wird nie zerschnitten. Rückgabe = geschriebene Bytes ohne NUL.
std::size_t copyEllipsized(char* out, std::size_t cap, std::string_view src);

// Titel und Text zu einem Fehler aus preflightEmulator. Rückgabe = Länge des Textes.
int launchErrorText(const Error& e, char* title, std::size_t titleCap, char* text, std::size_t textCap);

}  // namespace nl::ui
