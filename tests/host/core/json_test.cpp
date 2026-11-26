#include "core/json.hpp"

#include <doctest/doctest.h>

#include <string>

using namespace nl;

TEST_CASE("json: Objekte und Arrays verschachtelt") {
    const auto r = parseJson(R"( {"a":[1,true,null,{"b":"x"}],"c":{}} )");
    REQUIRE(r.isOk());
    const JsonValue& v = r.value();
    CHECK(v.type == JsonType::Object);
    const JsonValue* a = v.get("a");
    REQUIRE(a != nullptr);
    REQUIRE(a->items.size() == 4);
    CHECK(a->items[0].num == 1.0);
    CHECK(a->items[1].type == JsonType::Bool);
    CHECK(a->items[1].boolean);
    CHECK(a->items[2].type == JsonType::Null);
    CHECK(a->items[3].string("b") == "x");
    CHECK(v.get("c")->type == JsonType::Object);
    CHECK(v.get("fehlt") == nullptr);
    CHECK(v.string("a").empty());
}

TEST_CASE("json: Escapes und Unicode") {
    const auto r = parseJson(R"({"s":"\"\\\/\b\f\n\r\t","u":"ä","e":"😀"})");
    REQUIRE(r.isOk());
    CHECK(r.value().string("s") == "\"\\/\b\f\n\r\t");
    CHECK(r.value().string("u") == "\xc3\xa4");
    CHECK(r.value().string("e") == "\xf0\x9f\x98\x80");
    CHECK_FALSE(parseJson(R"({"x":"\ud83d"})").isOk());
    CHECK_FALSE(parseJson(R"({"x":"\ude00"})").isOk());
}

TEST_CASE("json: Zahlen") {
    const auto r = parseJson(R"([-0, 1e3, 12.5, -7])");
    REQUIRE(r.isOk());
    CHECK(r.value().items[0].num == 0.0);
    CHECK(r.value().items[1].num == 1000.0);
    CHECK(r.value().items[2].num == 12.5);
    CHECK(r.value().items[3].num == -7.0);
    CHECK_FALSE(parseJson("01").isOk());
    CHECK_FALSE(parseJson("1.").isOk());
    CHECK_FALSE(parseJson("-").isOk());
}

TEST_CASE("json: Müll und Grenzen") {
    CHECK_FALSE(parseJson(R"({"a":1} x)").isOk());
    CHECK_FALSE(parseJson(R"({"a":1 "b":2})").isOk());
    CHECK_FALSE(parseJson("{\"a\":\"x\ty\"}").isOk());
    CHECK_FALSE(parseJson("").isOk());
    CHECK_FALSE(parseJson("[1,]").isOk());
    std::string deep(33, '[');
    deep += std::string(33, ']');
    CHECK_FALSE(parseJson(deep).isOk());
    std::string ok32(32, '[');
    ok32 += std::string(32, ']');
    CHECK(parseJson(ok32).isOk());
    std::string many = "[";
    for (int i = 0; i < 4096; ++i) many += i ? ",0" : "0";
    many += "]";
    CHECK_FALSE(parseJson(many).isOk());  // 4096 Zahlen + Array = 4097 Knoten
    JsonLimits small;
    small.bytes = 4;
    CHECK_FALSE(parseJson("[1,2]", small).isOk());
}

TEST_CASE("json: integer liest Zahl und Ziffern-String") {
    const auto r = parseJson(R"({"size":"123","n":42,"x":"12a","f":1.5})");
    REQUIRE(r.isOk());
    CHECK(r.value().integer("size") == 123);
    CHECK(r.value().integer("n") == 42);
    CHECK_FALSE(r.value().integer("x").has_value());
    CHECK_FALSE(r.value().integer("f").has_value());
    CHECK_FALSE(r.value().integer("fehlt").has_value());
}

TEST_CASE("json: Writer-Rundlauf") {
    JsonWriter w;
    w.obj();
    w.key("name");
    w.str("a\"b\\c\n\x01\xc3\xa4");
    w.key("n");
    w.num(-12);
    w.key("ok");
    w.boolean(true);
    w.key("p");
    w.arr();
    w.str("x");
    w.str("y");
    w.end();
    w.key("o");
    w.obj();
    w.end();
    w.end();
    const std::string text = w.take();
    CHECK(text == R"({"name":"a\"b\\c\n\u0001)" "\xc3\xa4" R"(","n":-12,"ok":true,"p":["x","y"],"o":{}})");
    const auto r = parseJson(text);
    REQUIRE(r.isOk());
    CHECK(r.value().string("name") == "a\"b\\c\n\x01\xc3\xa4");
    CHECK(r.value().integer("n") == -12);
}
