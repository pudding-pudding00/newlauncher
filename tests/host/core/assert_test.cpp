#include "core/assert.hpp"
#include "core/result.hpp"
#include <doctest/doctest.h>
#include <csetjmp>
#include <cstring>

namespace {
std::jmp_buf g_jump;
const char* g_expr = nullptr;
int g_line = 0;

[[noreturn]] void jumpingHandler(const char* expr, const char*, int line) {
    g_expr = expr;
    g_line = line;
    std::longjmp(g_jump, 1);
}

struct HandlerGuard {
    HandlerGuard() { nl::setPanicHandler(&jumpingHandler); }
    ~HandlerGuard() { nl::setPanicHandler(nullptr); }
};
}  // namespace

TEST_CASE("NL_ASSERT ruft den Panic-Handler") {
    HandlerGuard guard;
    g_expr = nullptr;
    if (setjmp(g_jump) == 0) {
        NL_ASSERT(1 == 2);
        FAIL("Panic-Handler wurde nicht aufgerufen");
    }
    REQUIRE(g_expr != nullptr);
    CHECK(std::strcmp(g_expr, "1 == 2") == 0);
    CHECK(g_line > 0);
}

TEST_CASE("NL_ASSERT mit wahrer Bedingung tut nichts") {
    HandlerGuard guard;
    g_expr = nullptr;
    if (setjmp(g_jump) == 0) {
        NL_ASSERT(2 == 2);
    }
    CHECK(g_expr == nullptr);
}

TEST_CASE("Result::value auf Fehler löst Panic aus") {
    HandlerGuard guard;
    g_expr = nullptr;
    auto r = nl::Result<int>::err(nl::Error{nl::ErrorCode::NotFound, "x"});
    if (setjmp(g_jump) == 0) {
        [[maybe_unused]] int v = r.value();
        FAIL("Panic-Handler wurde nicht aufgerufen");
    }
    CHECK(g_expr != nullptr);
}
