#pragma once
// HTTP-GET über die IOS-Sockets (Spec 6.4). Nur im Netz-Thread aufrufen; net_init muss gelaufen sein.

#include <vector>

#include "core/http.hpp"
#include "core/types.hpp"

namespace nl::platform {

// Ok nur bei Status 200 mit vollständigem Body (höchstens maxBody Bytes); 404 → NotFound; alles andere Failed.
// DNS über net_gethostbyname, IP-Literale direkt; Verbindungsaufbau, Senden und Empfangen je mit 10 s Timeout.
// cancel (optional) wird beim Warten etwa alle 100 ms geprüft; gesetzt → Failed. Die DNS-Abfrage selbst ist nicht
// unterbrechbar.
HttpOutcome httpGet(const Url& url, u32 maxBody, std::vector<u8>& body, const volatile bool* cancel = nullptr);

}  // namespace nl::platform
