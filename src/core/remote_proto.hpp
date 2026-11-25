#pragma once
// Fernsteuerung (M20): Zeilenprotokoll über TCP für Deployment und Fehlersuche vom PC aus.
// Anfrage: eine Textzeile („KEY a 10“), bei PUT gefolgt von <größe> Rohbytes.
// Antwort: „OK <länge> [info]\n“ bzw. „ERR <länge> <grund>\n“, danach <länge> Bytes Nutzdaten.
// Plattformfrei: Socket-Thread und Ausführung liegen in platform/remote_server bzw. app/main.

#include <optional>
#include <string>
#include <string_view>

#include "core/result.hpp"
#include "core/types.hpp"

namespace nl {

inline constexpr u16 kRemotePort = 4300;
inline constexpr std::size_t kRemoteMaxLine = 512;
inline constexpr u32 kRemoteMaxPut = 16u * 1024u * 1024u;
inline constexpr u32 kRemoteDefaultKeyFrames = 6;   // ~0,1 s: ein sauberer Tastendruck für InputMapper
inline constexpr u32 kRemoteMaxKeyFrames = 600;     // 10 s bei 60 Hz

// Gleiche Bits wie ui::RawInput::Button (core kennt ui nicht; static_assert in remote_server.cpp).
namespace RawButtons {
inline constexpr u64 DUp = 1ull << 0, DDown = 1ull << 1, DLeft = 1ull << 2, DRight = 1ull << 3;
inline constexpr u64 A = 1ull << 4, B = 1ull << 5, Minus = 1ull << 6, Plus = 1ull << 7;
inline constexpr u64 One = 1ull << 8, Two = 1ull << 9, HomeBtn = 1ull << 10;
inline constexpr u64 GcA = 1ull << 11, GcB = 1ull << 12, GcX = 1ull << 13, GcY = 1ull << 14;
inline constexpr u64 GcL = 1ull << 15, GcR = 1ull << 16, GcZ = 1ull << 17, GcStart = 1ull << 18;
}  // namespace RawButtons

enum class RemoteCmd : u8 { Hello, Log, Shot, State, Games, Key, Launch, Put, Restart, Exit };

struct RemoteRequest {
    RemoteCmd cmd = RemoteCmd::Hello;
    u64 buttons = 0;   // KEY
    u32 frames = 0;    // KEY
    u32 size = 0;      // PUT
    u32 crc = 0;       // PUT (CRC-32 wie core/crc32)
    std::string text;  // LAUNCH: Spielschlüssel; PUT: Zielpfad
};

Result<RemoteRequest> parseRemoteLine(std::string_view line);
// „a,b,one“ → Bits; nullopt bei leerem oder unbekanntem Namen.
std::optional<u64> parseRemoteButtons(std::string_view names);
// PUT darf nur Dateien unter <gerät>:/apps/newlauncher/ und unter dataRoot/ schreiben (kein „..“, kein „\“).
bool remotePutAllowed(std::string_view path, std::string_view dataRoot);
std::string remoteReplyHeader(bool ok, std::size_t length, std::string_view info);
// argv[1…] enthält „--remote“ (z. B. „wiiload newlauncher.dol --remote“).
bool hasRemoteFlag(int argc, const char* const* argv);

}  // namespace nl
