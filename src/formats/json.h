#pragma once

// Minimal JSON reader (RFC 8259) for mod content such as glTF models. Numbers
// are doubles; \u escapes are converted to UTF-8. A value can be changed and written
// back out (dump), for rewriting a downloaded model (formats/model/gltf.h).

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gt3r::json {

class Value {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type type() const { return type_; }
    bool is_null() const { return type_ == Type::Null; }
    bool is_number() const { return type_ == Type::Number; }
    bool is_string() const { return type_ == Type::String; }
    bool is_array() const { return type_ == Type::Array; }
    bool is_object() const { return type_ == Type::Object; }

    double number(double fallback = 0) const { return type_ == Type::Number ? num_ : fallback; }
    // Out-of-range numbers (and NaN) give the fallback: converting them to int is undefined.
    int integer(int fallback = 0) const {
        return type_ == Type::Number && num_ >= -2147483648.0 && num_ <= 2147483647.0 ? int(num_) : fallback;
    }
    bool boolean(bool fallback = false) const { return type_ == Type::Bool ? b_ : fallback; }
    const std::string& string() const { return str_; }

    // Arrays.
    size_t size() const { return type_ == Type::Array ? arr_.size() : type_ == Type::Object ? obj_.size() : 0; }
    const Value& operator[](size_t i) const;
    const std::vector<Value>& items() const { return arr_; }
    // Objects: a null value when the key is missing (so lookups chain).
    const Value& operator[](std::string_view key) const;
    bool has(std::string_view key) const { return type_ == Type::Object && obj_.count(std::string(key)); }
    const std::map<std::string, Value>& members() const { return obj_; }

    // Throws FormatError with the byte offset on malformed input.
    static Value parse(std::string_view text);

    // Building and changing values.
    static Value of_number(double v);
    static Value of_string(std::string s);
    static Value empty_object();
    Value& set(const std::string& key, Value v); // (becomes an object if it is none)
    void erase(const std::string& key);
    Value& push(Value v);                        // (becomes an array if it is none)
    Value* member(const std::string& key);       // null when missing
    Value* item(size_t i);                       // null when missing
    // Compact JSON text; numbers as the shortest form that reads back the same double. Object members in key order.
    std::string dump() const;

private:
    friend struct Parser;
    Type type_ = Type::Null;
    bool b_ = false;
    double num_ = 0;
    std::string str_;
    std::vector<Value> arr_;
    std::map<std::string, Value> obj_;
};

} // namespace gt3r::json
