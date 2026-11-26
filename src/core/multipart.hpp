#pragma once
// multipart/related für Uploads mit Metadaten (M27, Google Drive uploadType=multipart).

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/types.hpp"

namespace nl {

// Erster Teil JSON (application/json; charset=UTF-8), zweiter Teil die Daten (application/zip). Zeilenenden CRLF.
std::vector<u8> multipartRelated(std::string_view boundary, std::string_view json, std::span<const u8> data);
std::string multipartContentType(std::string_view boundary);  // "multipart/related; boundary=<b>"

}  // namespace nl
