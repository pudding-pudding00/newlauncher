#include "core/http_types.hpp"
#include "core/multipart.hpp"

#include <doctest/doctest.h>

#include <string>

#include "support/fake_transport.hpp"

using namespace nl;
using nl::test::FakeTransport;
using nl::test::jsonResponse;

TEST_CASE("HttpResponse::header ohne Groß/klein") {
    HttpResponse r;
    r.headers.emplace_back("Content-Type", "application/json");
    r.headers.emplace_back("location", "https://x/1");
    CHECK(r.header("content-type") == "application/json");
    CHECK(r.header("LOCATION") == "https://x/1");
    CHECK(r.header("etag").empty());
}

TEST_CASE("httpMethodName") {
    CHECK(std::string(httpMethodName(HttpMethod::Get)) == "GET");
    CHECK(std::string(httpMethodName(HttpMethod::Patch)) == "PATCH");
    CHECK(std::string(httpMethodName(HttpMethod::Delete)) == "DELETE");
}

TEST_CASE("multipartRelated: genaues Format") {
    const u8 data[] = {'P', 'K', 0x00, 0xff};
    const auto body = multipartRelated("b", R"({"name":"x.zip"})", data);
    std::string expect = "--b\r\nContent-Type: application/json; charset=UTF-8\r\n\r\n{\"name\":\"x.zip\"}\r\n"
                         "--b\r\nContent-Type: application/zip\r\n\r\n";
    expect += std::string("PK\0\xff", 4);
    expect += "\r\n--b--\r\n";
    CHECK(std::string(body.begin(), body.end()) == expect);
    CHECK(multipartContentType("b") == "multipart/related; boundary=b");
}

TEST_CASE("FakeTransport: Skript in Reihenfolge, Unerwartetes → 599") {
    FakeTransport t;
    t.expect(HttpMethod::Get, "https://a/1", jsonResponse(200, "{}"));
    t.expect(HttpMethod::Post, "https://a/2", jsonResponse(201, "{}"));
    const bool cancel = false;
    HttpRequest r1;
    r1.url = "https://a/1?x";
    CHECK(t.send(r1, cancel).status == 200);
    CHECK_FALSE(t.done());
    HttpRequest wrong;
    wrong.url = "https://a/2";  // GET statt POST
    CHECK(t.send(wrong, cancel).status == 599);
    HttpRequest r2;
    r2.method = HttpMethod::Post;
    r2.url = "https://a/2";
    CHECK(t.send(r2, cancel).status == 201);
    CHECK(t.done());
    CHECK(t.send(r2, cancel).status == 599);
    CHECK(t.seen.size() == 4);
}
