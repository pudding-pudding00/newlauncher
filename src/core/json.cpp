#include "core/json.hpp"

#include <cmath>
#include <cstdlib>

namespace nl {

const JsonValue* JsonValue::get(std::string_view key) const {
    if (type != JsonType::Object) return nullptr;
    for (const auto& [k, v] : members) {
        if (k == key) return &v;
    }
    return nullptr;
}

std::string_view JsonValue::string(std::string_view key) const {
    const JsonValue* v = get(key);
    return v != nullptr && v->type == JsonType::String ? std::string_view(v->str) : std::string_view();
}

std::optional<s64> JsonValue::integer(std::string_view key) const {
    const JsonValue* v = get(key);
    if (v == nullptr) return std::nullopt;
    if (v->type == JsonType::Number) {
        if (std::floor(v->num) != v->num || std::fabs(v->num) > 9.0e15) return std::nullopt;
        return static_cast<s64>(v->num);
    }
    if (v->type != JsonType::String || v->str.empty() || v->str.size() > 18) return std::nullopt;
    s64 n = 0;
    for (const char c : v->str) {
        if (c < '0' || c > '9') return std::nullopt;
        n = n * 10 + (c - '0');
    }
    return n;
}

namespace {

class Parser {
public:
    Parser(std::string_view t, JsonLimits l) : t_(t), lim_(l) {}

    bool parseValue(JsonValue& out, u32 depth) {
        if (depth > lim_.depth || ++nodes_ > lim_.nodes) return false;
        ws();
        if (p_ >= t_.size()) return false;
        const char c = t_[p_];
        if (c == '{') return parseObject(out, depth);
        if (c == '[') return parseArray(out, depth);
        if (c == '"') {
            out.type = JsonType::String;
            return parseString(out.str);
        }
        if (c == 't') return literal("true", out, JsonType::Bool, true);
        if (c == 'f') return literal("false", out, JsonType::Bool, false);
        if (c == 'n') return literal("null", out, JsonType::Null, false);
        return parseNumber(out);
    }

    bool atEnd() {
        ws();
        return p_ == t_.size();
    }

private:
    void ws() {
        while (p_ < t_.size() && (t_[p_] == ' ' || t_[p_] == '\t' || t_[p_] == '\n' || t_[p_] == '\r')) ++p_;
    }

    bool eat(char c) {
        ws();
        if (p_ < t_.size() && t_[p_] == c) {
            ++p_;
            return true;
        }
        return false;
    }

    bool literal(std::string_view word, JsonValue& out, JsonType type, bool b) {
        if (t_.substr(p_, word.size()) != word) return false;
        p_ += word.size();
        out.type = type;
        out.boolean = b;
        return true;
    }

    bool parseObject(JsonValue& out, u32 depth) {
        ++p_;
        out.type = JsonType::Object;
        if (eat('}')) return true;
        for (;;) {
            ws();
            if (p_ >= t_.size() || t_[p_] != '"') return false;
            std::string key;
            if (!parseString(key) || !eat(':')) return false;
            JsonValue v;
            if (!parseValue(v, depth + 1)) return false;
            out.members.emplace_back(std::move(key), std::move(v));
            if (eat('}')) return true;
            if (!eat(',')) return false;
        }
    }

    bool parseArray(JsonValue& out, u32 depth) {
        ++p_;
        out.type = JsonType::Array;
        if (eat(']')) return true;
        for (;;) {
            JsonValue v;
            if (!parseValue(v, depth + 1)) return false;
            out.items.push_back(std::move(v));
            if (eat(']')) return true;
            if (!eat(',')) return false;
        }
    }

    bool hex4(u32& out) {
        if (p_ + 4 > t_.size()) return false;
        out = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = t_[p_++];
            u32 d = 0;
            if (c >= '0' && c <= '9') {
                d = static_cast<u32>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                d = static_cast<u32>(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                d = static_cast<u32>(c - 'A' + 10);
            } else {
                return false;
            }
            out = (out << 4) | d;
        }
        return true;
    }

    static void utf8(std::string& s, u32 cp) {
        if (cp < 0x80) {
            s += static_cast<char>(cp);
        } else if (cp < 0x800) {
            s += static_cast<char>(0xC0 | (cp >> 6));
            s += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            s += static_cast<char>(0xE0 | (cp >> 12));
            s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            s += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            s += static_cast<char>(0xF0 | (cp >> 18));
            s += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            s += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    bool parseEscape(std::string& out) {
        if (p_ >= t_.size()) return false;
        const char e = t_[p_++];
        switch (e) {
            case '"': out += '"'; return true;
            case '\\': out += '\\'; return true;
            case '/': out += '/'; return true;
            case 'b': out += '\b'; return true;
            case 'f': out += '\f'; return true;
            case 'n': out += '\n'; return true;
            case 'r': out += '\r'; return true;
            case 't': out += '\t'; return true;
            case 'u': break;
            default: return false;
        }
        u32 cp = 0;
        if (!hex4(cp)) return false;
        if (cp >= 0xDC00 && cp <= 0xDFFF) return false;  // einzelnes unteres Surrogat
        if (cp >= 0xD800 && cp <= 0xDBFF) {
            if (p_ + 2 > t_.size() || t_[p_] != '\\' || t_[p_ + 1] != 'u') return false;
            p_ += 2;
            u32 lo = 0;
            if (!hex4(lo) || lo < 0xDC00 || lo > 0xDFFF) return false;
            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
        }
        utf8(out, cp);
        return true;
    }

    bool parseString(std::string& out) {
        ++p_;  // öffnendes "
        while (p_ < t_.size()) {
            const char c = t_[p_++];
            if (c == '"') return true;
            if (static_cast<unsigned char>(c) < 0x20) return false;
            if (c == '\\') {
                if (!parseEscape(out)) return false;
            } else {
                out += c;
            }
        }
        return false;
    }

    bool digit() const { return p_ < t_.size() && t_[p_] >= '0' && t_[p_] <= '9'; }

    bool parseNumber(JsonValue& out) {
        const std::size_t start = p_;
        if (p_ < t_.size() && t_[p_] == '-') ++p_;
        if (!digit()) return false;
        if (t_[p_] == '0') {
            ++p_;
        } else {
            while (digit()) ++p_;
        }
        if (p_ < t_.size() && t_[p_] == '.') {
            ++p_;
            if (!digit()) return false;
            while (digit()) ++p_;
        }
        if (p_ < t_.size() && (t_[p_] == 'e' || t_[p_] == 'E')) {
            ++p_;
            if (p_ < t_.size() && (t_[p_] == '+' || t_[p_] == '-')) ++p_;
            if (!digit()) return false;
            while (digit()) ++p_;
        }
        const std::string text(t_.substr(start, p_ - start));
        out.type = JsonType::Number;
        out.num = std::strtod(text.c_str(), nullptr);
        return true;
    }

    std::string_view t_;
    JsonLimits lim_;
    std::size_t p_ = 0;
    u32 nodes_ = 0;
};

}  // namespace

Result<JsonValue> parseJson(std::string_view text, JsonLimits limits) {
    if (text.size() > limits.bytes) return Result<JsonValue>::err(Error{ErrorCode::InvalidData, "json: zu groß"});
    Parser p(text, limits);
    JsonValue v;
    if (!p.parseValue(v, 1) || !p.atEnd()) {
        return Result<JsonValue>::err(Error{ErrorCode::InvalidData, "json: ungültig"});
    }
    return Result<JsonValue>::ok(std::move(v));
}

void JsonWriter::comma() {
    if (afterKey_) {
        afterKey_ = false;
        return;
    }
    if (!first_.empty()) {
        if (first_.back() == '0') out_ += ',';
        first_.back() = '0';
    }
}

void JsonWriter::open(char c) {
    comma();
    out_ += c;
    open_ += c;
    first_ += '1';
}

void JsonWriter::obj() { open('{'); }
void JsonWriter::arr() { open('['); }

void JsonWriter::end() {
    if (open_.empty()) return;
    out_ += open_.back() == '{' ? '}' : ']';
    open_.pop_back();
    first_.pop_back();
}

void JsonWriter::key(std::string_view k) {
    str(k);
    out_ += ':';
    afterKey_ = true;
}

void JsonWriter::str(std::string_view s) {
    comma();
    static constexpr char kHex[] = "0123456789abcdef";
    out_ += '"';
    for (const char ch : s) {
        const auto c = static_cast<unsigned char>(ch);
        if (ch == '"') {
            out_ += "\\\"";
        } else if (ch == '\\') {
            out_ += "\\\\";
        } else if (ch == '\n') {
            out_ += "\\n";
        } else if (ch == '\r') {
            out_ += "\\r";
        } else if (ch == '\t') {
            out_ += "\\t";
        } else if (c < 0x20) {
            out_ += "\\u00";
            out_ += kHex[c >> 4];
            out_ += kHex[c & 0x0F];
        } else {
            out_ += ch;
        }
    }
    out_ += '"';
}

void JsonWriter::num(s64 n) {
    comma();
    out_ += std::to_string(n);
}

void JsonWriter::boolean(bool b) {
    comma();
    out_ += b ? "true" : "false";
}

std::string JsonWriter::take() { return std::move(out_); }

}  // namespace nl
