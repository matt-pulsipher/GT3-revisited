#include "formats/json.h"

#include "base/bytes.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace gt3r::json {

const Value& Value::operator[](size_t i) const {
    static const Value null;
    return type_ == Type::Array && i < arr_.size() ? arr_[i] : null;
}

const Value& Value::operator[](std::string_view key) const {
    static const Value null;
    if (type_ != Type::Object) return null;
    auto it = obj_.find(std::string(key));
    return it == obj_.end() ? null : it->second;
}

struct Parser {
    std::string_view s;
    size_t i = 0;
    int depth = 0;

    [[noreturn]] void fail(const char* what) const {
        throw FormatError(std::string("JSON: ") + what + " at byte " + std::to_string(i));
    }
    void ws() {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
    }
    bool eat(char c) {
        ws();
        if (i < s.size() && s[i] == c) {
            ++i;
            return true;
        }
        return false;
    }
    void expect(char c) {
        if (!eat(c)) fail("unexpected character");
    }
    bool word(const char* w) {
        size_t n = std::strlen(w);
        if (s.substr(i, n) == w) {
            i += n;
            return true;
        }
        return false;
    }
    static void utf8(std::string& out, unsigned cp) {
        if (cp < 0x80) out += char(cp);
        else if (cp < 0x800) out += char(0xC0 | cp >> 6), out += char(0x80 | (cp & 0x3F));
        else if (cp < 0x10000) out += char(0xE0 | cp >> 12), out += char(0x80 | (cp >> 6 & 0x3F)), out += char(0x80 | (cp & 0x3F));
        else
            out += char(0xF0 | cp >> 18), out += char(0x80 | (cp >> 12 & 0x3F)), out += char(0x80 | (cp >> 6 & 0x3F)),
                out += char(0x80 | (cp & 0x3F));
    }
    unsigned hex4() {
        if (i + 4 > s.size()) fail("bad \\u escape");
        unsigned v = 0;
        for (int k = 0; k < 4; ++k) {
            char c = s[i++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= unsigned(c - '0');
            else if (c >= 'a' && c <= 'f') v |= unsigned(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= unsigned(c - 'A' + 10);
            else fail("bad \\u escape");
        }
        return v;
    }
    std::string string() {
        expect('"');
        std::string out;
        while (true) {
            if (i >= s.size()) fail("unterminated string");
            char c = s[i++];
            if (c == '"') break;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (i >= s.size()) fail("unterminated string");
            char e = s[i++];
            switch (e) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                unsigned cp = hex4();
                if (cp >= 0xD800 && cp < 0xDC00 && s.substr(i, 2) == "\\u") {
                    i += 2;
                    unsigned lo = hex4();
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                }
                utf8(out, cp);
                break;
            }
            default: fail("bad escape");
            }
        }
        return out;
    }
    Value value() {
        if (++depth > 256) fail("nested too deeply");
        ws();
        if (i >= s.size()) fail("unexpected end");
        Value v;
        char c = s[i];
        if (c == '{') {
            ++i;
            v.type_ = Value::Type::Object;
            if (!eat('}')) {
                do {
                    ws();
                    std::string k = string();
                    expect(':');
                    v.obj_[k] = value();
                } while (eat(','));
                expect('}');
            }
        } else if (c == '[') {
            ++i;
            v.type_ = Value::Type::Array;
            if (!eat(']')) {
                do v.arr_.push_back(value());
                while (eat(','));
                expect(']');
            }
        } else if (c == '"') {
            v.type_ = Value::Type::String;
            v.str_ = string();
        } else if (word("true")) {
            v.type_ = Value::Type::Bool, v.b_ = true;
        } else if (word("false")) {
            v.type_ = Value::Type::Bool, v.b_ = false;
        } else if (word("null")) {
            v.type_ = Value::Type::Null;
        } else if (c == '-' || (c >= '0' && c <= '9')) {
            size_t start = i;
            while (i < s.size() && std::strchr("+-0123456789.eE", s[i])) ++i;
            std::string t(s.substr(start, i - start));
            char* end = nullptr;
            v.type_ = Value::Type::Number;
            v.num_ = std::strtod(t.c_str(), &end);
            if (!end || *end) fail("bad number");
        } else {
            fail("unexpected character");
        }
        --depth;
        return v;
    }
};

Value Value::parse(std::string_view text) {
    Parser p{text};
    Value v = p.value();
    p.ws();
    if (p.i != text.size()) p.fail("trailing data");
    return v;
}

Value Value::of_number(double v) {
    Value r;
    r.type_ = Type::Number, r.num_ = v;
    return r;
}

Value Value::of_string(std::string s) {
    Value r;
    r.type_ = Type::String, r.str_ = std::move(s);
    return r;
}

Value Value::empty_object() {
    Value r;
    r.type_ = Type::Object;
    return r;
}

Value& Value::set(const std::string& key, Value v) {
    if (type_ != Type::Object) *this = empty_object();
    return obj_[key] = std::move(v);
}

void Value::erase(const std::string& key) { obj_.erase(key); }

Value& Value::push(Value v) {
    if (type_ != Type::Array) {
        *this = Value();
        type_ = Type::Array;
    }
    arr_.push_back(std::move(v));
    return arr_.back();
}

Value* Value::member(const std::string& key) {
    auto it = obj_.find(key);
    return type_ == Type::Object && it != obj_.end() ? &it->second : nullptr;
}

Value* Value::item(size_t i) { return type_ == Type::Array && i < arr_.size() ? &arr_[i] : nullptr; }

namespace {
void dump_string(std::string& out, const std::string& s) {
    out += '"';
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') out += '\\', out += char(c);
        else if (c < 0x20) {
            char b[8];
            std::snprintf(b, sizeof b, "\\u%04x", c);
            out += b;
        } else out += char(c);
    }
    out += '"';
}
void dump_value(std::string& out, const Value& v) {
    switch (v.type()) {
    case Value::Type::Null: out += "null"; break;
    case Value::Type::Bool: out += v.boolean() ? "true" : "false"; break;
    case Value::Type::Number: {
        const double d = v.number();
        if (!std::isfinite(d)) {
            out += "0";
            break;
        }
        char b[32];
        for (int prec = 1; prec <= 17; ++prec) { // the shortest that reads back the same
            std::snprintf(b, sizeof b, "%.*g", prec, d);
            if (std::strtod(b, nullptr) == d) break;
        }
        out += b;
        break;
    }
    case Value::Type::String: dump_string(out, v.string()); break;
    case Value::Type::Array: {
        out += '[';
        bool first = true;
        for (const Value& x : v.items()) {
            if (!first) out += ',';
            first = false;
            dump_value(out, x);
        }
        out += ']';
        break;
    }
    case Value::Type::Object: {
        out += '{';
        bool first = true;
        for (const auto& [k, x] : v.members()) {
            if (!first) out += ',';
            first = false;
            dump_string(out, k);
            out += ':';
            dump_value(out, x);
        }
        out += '}';
        break;
    }
    }
}
} // namespace

std::string Value::dump() const {
    std::string out;
    dump_value(out, *this);
    return out;
}

} // namespace gt3r::json
