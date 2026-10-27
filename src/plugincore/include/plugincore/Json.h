// STYM2149 — plugincore: JSON (RFC 8259) of the exchange files of the prototype
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Reading and writing of the JSON files that the prototype writes and reads (Patches library, project
// files). Objects keep the order of their keys, like JSON.stringify; writing follows JSON.stringify(value, null,
// indent): shortest numbers that read back as the same value, integers without a fractional part, strings in UTF-8
// with only the mandatory escapes.
// An array that holds only numbers is kept compact (one double per element, without a Value each): a preset of the
// prototype writes 31 rows of 256 steps per track and per pattern, i.e. millions of numbers.
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace stym::plugin::json {

// A JSON value of any type, held by value (arrays and objects own their elements). It allocates: never used on the
// audio thread.
class Value {
public:
    enum class Type : uint8_t { Null, Bool, Number, String, Array, Object };
    using Members = std::vector<std::pair<std::string, Value>>;  // object members, in insertion order

    Value() = default;
    Value(std::nullptr_t) {}
    Value(bool b) : type_(Type::Bool), bool_(b) {}
    Value(double n) : type_(Type::Number), number_(n) {}
    Value(int n) : type_(Type::Number), number_(n) {}
    Value(std::string s) : type_(Type::String), string_(std::move(s)) {}
    Value(const char* s) : type_(Type::String), string_(s) {}
    static Value array() { return Value(Type::Array); }
    static Value object() { return Value(Type::Object); }

    Type type() const noexcept { return type_; }
    bool isNull() const noexcept { return type_ == Type::Null; }
    bool isBool() const noexcept { return type_ == Type::Bool; }
    bool isNumber() const noexcept { return type_ == Type::Number; }
    bool isString() const noexcept { return type_ == Type::String; }
    bool isArray() const noexcept { return type_ == Type::Array; }
    bool isObject() const noexcept { return type_ == Type::Object; }

    // Value read as the prototype would: a boolean is 0 or 1, a number is true if it is not zero; otherwise
    // `fallback`.
    double number(double fallback = 0) const noexcept;
    bool truthy(bool fallback = false) const noexcept;
    const std::string& string() const noexcept { return string_; }

    // Array: elements (expanded on the first request for a compact array), number of elements, append.
    const std::vector<Value>& items() const;
    size_t size() const noexcept { return compact_ ? numbers_.size() : items_.size(); }
    // Appending to a compact array turns it into an ordinary one first.
    void push(Value v);
    // Compact array: its numbers (nullptr if the array is not compact); construction.
    const std::vector<double>* numbers() const noexcept { return compact_ ? &numbers_ : nullptr; }
    static Value numberArray(std::vector<double> v);

    // Object: member of a key (nullptr if missing), member written (replaced if it exists, otherwise appended).
    const Value* find(std::string_view key) const noexcept;
    void set(std::string key, Value v);
    const Members& members() const noexcept { return members_; }

    bool operator==(const Value& o) const;

private:
    explicit Value(Type t) : type_(t) {}

    Type type_ = Type::Null;
    bool bool_ = false;
    bool compact_ = false;  // array of numbers: numbers_ is authoritative, items_ is its expanded copy (or empty)
    double number_ = 0;
    std::string string_;
    mutable std::vector<Value> items_;
    std::vector<double> numbers_;
    Members members_;
};

// Complete JSON text (surrounding spaces allowed); nullopt and the reason if the text is not JSON.
std::optional<Value> parse(std::string_view text, std::string* error = nullptr);
// JSON.stringify(v, null, indent): indent = 0, everything on one line.
std::string dump(const Value& v, int indent = 0);

}  // namespace stym::plugin::json
