#include "core/result.hpp"
#include <doctest/doctest.h>
#include <memory>
using nl::Error; using nl::ErrorCode; using nl::Result;

TEST_CASE("Result hält einen Wert") {
    auto r = Result<int>::ok(42);
    CHECK(r.isOk());
    CHECK(r.value() == 42);
}
TEST_CASE("Result hält einen Fehler mit Kontext") {
    auto r = Result<int>::err(Error{ErrorCode::NotFound, "sd:/apps/x/boot.dol"});
    CHECK_FALSE(r.isOk());
    CHECK(r.error().code == ErrorCode::NotFound);
    CHECK(r.error().context == "sd:/apps/x/boot.dol");
}
TEST_CASE("valueOr liefert den Ersatzwert nur im Fehlerfall") {
    CHECK(Result<int>::ok(7).valueOr(0) == 7);
    CHECK(Result<int>::err(Error{ErrorCode::IoError, ""}).valueOr(5) == 5);
}
TEST_CASE("Result<void>") {
    CHECK(Result<void>::ok().isOk());
    auto r = Result<void>::err(Error{ErrorCode::Timeout, "usb"});
    CHECK_FALSE(r.isOk());
    CHECK(r.error().code == ErrorCode::Timeout);
}
TEST_CASE("Result unterstützt move-only Typen") {
    auto r = Result<std::unique_ptr<int>>::ok(std::make_unique<int>(3));
    REQUIRE(r.isOk());
    std::unique_ptr<int> p = std::move(r).value();
    CHECK(*p == 3);
}
