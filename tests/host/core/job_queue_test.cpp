#include "core/job_queue.hpp"
#include <doctest/doctest.h>
#include <string>
using namespace nl;

namespace {
std::function<void(const volatile bool&)> noop() {
    return [](const volatile bool&) {};
}
}  // namespace

TEST_CASE("JobQueue: FIFO-Reihenfolge") {
    JobQueue q;
    std::string order;
    q.push([&](const volatile bool&) { order += 'a'; });
    q.push([&](const volatile bool&) { order += 'b'; });
    q.push([&](const volatile bool&) { order += 'c'; });
    CHECK(q.pending() == 3);
    while (auto job = q.take()) {
        job->run(q.cancelFlag());
        q.finish();
    }
    CHECK(order == "abc");
    CHECK(q.pending() == 0);
    CHECK_FALSE(q.take().has_value());
}

TEST_CASE("JobQueue: IDs steigen monoton, 0 ist nie eine ID") {
    JobQueue q;
    u32 last = 0;
    for (int i = 0; i < 100; ++i) {
        const u32 id = q.push(noop());
        CHECK(id != 0);
        CHECK(id > last);
        last = id;
    }
    auto job = q.take();
    REQUIRE(job.has_value());
    CHECK(job->id != 0);
}

TEST_CASE("JobQueue: cancel eines wartenden Auftrags entfernt ihn") {
    JobQueue q;
    const u32 a = q.push(noop());
    const u32 b = q.push(noop());
    const u32 c = q.push(noop());
    q.cancel(b);
    CHECK(q.pending() == 2);
    CHECK_FALSE(q.active(b));
    auto first = q.take();
    REQUIRE(first.has_value());
    CHECK(first->id == a);
    q.finish();
    auto second = q.take();
    REQUIRE(second.has_value());
    CHECK(second->id == c);
}

TEST_CASE("JobQueue: cancel des laufenden Auftrags setzt cancelled und das Flag") {
    JobQueue q;
    const u32 a = q.push(noop());
    const u32 b = q.push(noop());
    auto job = q.take();
    REQUIRE(job.has_value());
    CHECK(q.active(a));
    CHECK_FALSE(q.cancelled(a));
    CHECK_FALSE(q.cancelFlag());
    q.cancel(a);
    CHECK(q.cancelled(a));
    CHECK(q.cancelFlag());
    CHECK(q.pending() == 1);  // der nächste wartet weiter
    q.finish();
    CHECK_FALSE(q.active(a));
    auto next = q.take();
    REQUIRE(next.has_value());
    CHECK(next->id == b);
    CHECK_FALSE(q.cancelFlag());  // jeder Auftrag beginnt ohne Abbruch
}

TEST_CASE("JobQueue: cancelAll leert die Warteschlange und markiert den laufenden") {
    JobQueue q;
    const u32 a = q.push(noop());
    const u32 b = q.push(noop());
    q.push(noop());
    auto job = q.take();
    REQUIRE(job.has_value());
    q.cancelAll();
    CHECK(q.pending() == 0);
    CHECK(q.cancelled(a));
    CHECK_FALSE(q.active(b));
    q.finish();
    CHECK_FALSE(q.take().has_value());
}

TEST_CASE("JobQueue: cancel unbekannter oder fertiger IDs ist harmlos") {
    JobQueue q;
    const u32 a = q.push(noop());
    auto job = q.take();
    q.finish();
    q.cancel(a);
    q.cancel(12345);
    q.cancel(0);
    CHECK_FALSE(q.cancelled(a));
    CHECK_FALSE(q.cancelFlag());
    const u32 b = q.push(noop());
    auto next = q.take();
    REQUIRE(next.has_value());
    CHECK(next->id == b);
    CHECK_FALSE(q.cancelFlag());
}
