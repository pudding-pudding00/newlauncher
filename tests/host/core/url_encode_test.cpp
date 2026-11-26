#include "core/url_encode.hpp"

#include <doctest/doctest.h>

#include <string_view>
#include <utility>

using namespace nl;

TEST_CASE("percentEncode: RFC 3986, UTF-8 byteweise") {
    CHECK(percentEncode("a b/\xc3\xa4") == "a%20b%2F%C3%A4");
    CHECK(percentEncode("AZaz09-._~") == "AZaz09-._~");
    CHECK(percentEncode("'x'='y'") == "%27x%27%3D%27y%27");
}

TEST_CASE("formEncode: Paare mit & und =") {
    const std::pair<std::string_view, std::string_view> one[] = {
        {"grant_type", "urn:ietf:params:oauth:grant-type:device_code"}};
    CHECK(formEncode(one) == "grant_type=urn%3Aietf%3Aparams%3Aoauth%3Agrant-type%3Adevice_code");
    const std::pair<std::string_view, std::string_view> two[] = {{"a", "1 2"}, {"b&", ""}};
    CHECK(formEncode(two) == "a=1%202&b%26=");
}
