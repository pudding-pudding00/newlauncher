#pragma once
// HTTP-GET über die IOS-Sockets (Spec 6.4). Nur im Netz-Thread (NetService) aufrufen; dort läuft net_init.

#include <string>
#include <vector>

#include "core/http.hpp"
#include "core/types.hpp"

namespace nl::platform {

// Ok nur bei Status 200 mit vollständigem Body (höchstens maxBody Bytes); 404 → NotFound; alles andere Failed.
// DNS über net_gethostbyname, IP-Literale direkt; Verbindungsaufbau, Senden und Empfangen je mit 10 s Timeout.
// cancel (optional) wird beim Warten etwa alle 100 ms geprüft; gesetzt → Failed. Die DNS-Abfrage selbst ist nicht
// unterbrechbar.
HttpOutcome httpGet(const Url& url, u32 maxBody, std::vector<u8>& body, const volatile bool* cancel = nullptr);

// Wie httpGet, schreibt den Body aber per fopen/fwrite (64-KB-Puffer) nach destFile ("sd:/…"). received zählt die
// Body-Bytes mit (für die Fortschrittsanzeige). Bei jedem Ergebnis außer Ok wird destFile gelöscht.
HttpOutcome httpDownload(const Url& url, const std::string& destFile, u32 maxBody, const volatile bool* cancel,
                         volatile u32* received);

}  // namespace nl::platform
