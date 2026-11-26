#pragma once
// JSON nach RFC 8259 (M27, Cloud-Anbieter): Parser mit harten Grenzen und ein kleiner Writer. Kein Fremdcode.

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/result.hpp"
#include "core/types.hpp"

namespace nl {

enum class JsonType : u8 { Null, Bool, Number, String, Array, Object };

struct JsonValue {
    JsonType type = JsonType::Null;
    bool boolean = false;
    double num = 0.0;
    std::string str;
    std::vector<JsonValue> items;
    std::vector<std::pair<std::string, JsonValue>> members;

    const JsonValue* get(std::string_view key) const;     // erster Treffer; nullptr, wenn kein Objekt oder fehlt
    std::string_view string(std::string_view key) const;  // leer, wenn fehlt oder kein String
    // Ganzzahl aus einer Zahl ohne Nachkommastellen oder aus einem String nur aus Ziffern (Drive: "version":"12").
    std::optional<s64> integer(std::string_view key) const;
};

struct JsonLimits {
    u32 depth = 32;
    u32 nodes = 4096;
    std::size_t bytes = 1024 * 1024;
};

Result<JsonValue> parseJson(std::string_view text, JsonLimits limits = {});

// Schreibt kompaktes JSON. Aufrufer klammern selbst korrekt (obj/arr … end); key nur in Objekten.
class JsonWriter {
public:
    void obj();
    void arr();
    void end();
    void key(std::string_view k);
    void str(std::string_view s);
    void num(s64 n);
    void boolean(bool b);
    std::string take();

private:
    void comma();
    void open(char c);
    std::string out_;
    std::string open_;   // offene Klammern '{' oder '['
    std::string first_;  // je offener Ebene: '1' = noch kein Element
    bool afterKey_ = false;
};

}  // namespace nl
