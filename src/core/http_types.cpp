#include "core/http_types.hpp"

namespace nl {

const char* httpMethodName(HttpMethod m) {
    switch (m) {
        case HttpMethod::Get: return "GET";
        case HttpMethod::Post: return "POST";
        case HttpMethod::Put: return "PUT";
        case HttpMethod::Patch: return "PATCH";
        case HttpMethod::Delete: return "DELETE";
    }
    return "GET";
}

namespace {
char lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

bool sameNoCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (lower(a[i]) != lower(b[i])) return false;
    }
    return true;
}
}  // namespace

std::string_view HttpResponse::header(std::string_view name) const {
    for (const auto& [k, v] : headers) {
        if (sameNoCase(k, name)) return v;
    }
    return {};
}

}  // namespace nl
