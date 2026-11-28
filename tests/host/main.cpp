#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

TEST_CASE("doctest::String self-append beyond SSO buffer") {
    doctest::String s("012345678901234567890123456789");
    s += s;
    CHECK(s == "012345678901234567890123456789012345678901234567890123456789");
}
