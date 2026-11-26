#include "core/multipart.hpp"

namespace nl {

namespace {
void append(std::vector<u8>& out, std::string_view s) { out.insert(out.end(), s.begin(), s.end()); }
}  // namespace

std::vector<u8> multipartRelated(std::string_view boundary, std::string_view json, std::span<const u8> data) {
    std::vector<u8> out;
    out.reserve(data.size() + json.size() + 3 * boundary.size() + 128);
    append(out, "--");
    append(out, boundary);
    append(out, "\r\nContent-Type: application/json; charset=UTF-8\r\n\r\n");
    append(out, json);
    append(out, "\r\n--");
    append(out, boundary);
    append(out, "\r\nContent-Type: application/zip\r\n\r\n");
    out.insert(out.end(), data.begin(), data.end());
    append(out, "\r\n--");
    append(out, boundary);
    append(out, "--\r\n");
    return out;
}

std::string multipartContentType(std::string_view boundary) {
    return "multipart/related; boundary=" + std::string(boundary);
}

}  // namespace nl
