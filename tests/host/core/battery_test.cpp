#include "core/battery.hpp"
#include <doctest/doctest.h>
using namespace nl;

TEST_CASE("batteryBars: 200 ist voll, aufgerundet auf Viertel") {
    CHECK(batteryBars(0) == 0);
    CHECK(batteryBars(1) == 1);
    CHECK(batteryBars(50) == 1);
    CHECK(batteryBars(51) == 2);
    CHECK(batteryBars(150) == 3);
    CHECK(batteryBars(151) == 4);
    CHECK(batteryBars(200) == 4);
    CHECK(batteryBars(255) == 4);
    static_assert(batteryBars(100) == 2);
}
