#pragma once
// URL- und Formularkodierung (M27): Prozentkodierung nach RFC 3986, unreserviert sind nur A-Z a-z 0-9 - . _ ~.

#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace nl {

std::string percentEncode(std::string_view s);
void appendPercentEncoded(std::string& out, std::string_view s);
// application/x-www-form-urlencoded mit %20 statt '+': "k=v&k2=v2".
std::string formEncode(std::span<const std::pair<std::string_view, std::string_view>> pairs);

}  // namespace nl
