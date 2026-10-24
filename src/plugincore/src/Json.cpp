// STYM2149 — plugincore: JSON (reading, writing in the manner of JSON.stringify)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "plugincore/Json.h"

#include <clocale>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace stym::plugin::json {

// ------------------------------------------------------------------------------------------- Value

double Value::number(double fallback) const noexcept {
    if (type_ == Type::Number) return number_;
    if (type_ == Type::Bool) return bool_ ? 1 : 0;
    return fallback;
}

bool Value::truthy(bool fallback) const noexcept {
    if (type_ == Type::Bool) return bool_;
    if (type_ == Type::Number) return number_ != 0 && !std::isnan(number_);
    return fallback;
}

const Value* Value::find(std::string_view key) const noexcept {
    for (const auto& [k, v] : members_)
        if (k == key) return &v;
    return nullptr;
}

// A compact array builds its Value copy on the first call only (items_ is mutable for that); later calls reuse it.
const std::vector<Value>& Value::items() const {
    if (compact_ && items_.size() != numbers_.size()) items_.assign(numbers_.begin(), numbers_.end());
    return items_;
}

void Value::push(Value v) {
    if (compact_) {
        items();
        numbers_.clear();
        compact_ = false;
    }
    type_ = Type::Array;
    items_.push_back(std::move(v));
}

Value Value::numberArray(std::vector<double> v) {
    Value a(Type::Array);
    a.compact_ = true;
    a.numbers_ = std::move(v);
    return a;
}

bool Value::operator==(const Value& o) const {
    if (type_ != o.type_) return false;
    switch (type_) {
    case Type::Null: return true;
    case Type::Bool: return bool_ == o.bool_;
    case Type::Number: return number_ == o.number_;
    case Type::String: return string_ == o.string_;
    case Type::Array: return compact_ && o.compact_ ? numbers_ == o.numbers_ : items() == o.items();
    case Type::Object: return members_ == o.members_;
    }
    return false;
}

void Value::set(std::string key, Value v) {
    type_ = Type::Object;
    for (auto& [k, old] : members_)
        if (k == key) {
            old = std::move(v);
            return;
        }
    members_.emplace_back(std::move(key), std::move(v));
}

namespace {

// strtod and snprintf follow the locale of the program (a host may have changed it): the JSON point becomes the
// decimal separator of the locale before reading, and the reverse after writing.
char localePoint() {
    const std::lconv* lc = std::localeconv();
    return lc && lc->decimal_point && lc->decimal_point[0] ? lc->decimal_point[0] : '.';
}

double readNumber(std::string text) {
    const char point = localePoint();
    for (char& c : text)
        if (c == '.') c = point;
    return std::strtod(text.c_str(), nullptr);
}

// ------------------------------------------------------------------------------------------- reading

// Recursive descent over the text, strict RFC 8259 (no comments, no trailing comma); the first error is kept with
// its offset.
class Parser {
public:
    explicit Parser(std::string_view t) : t_(t) {}

    std::optional<Value> document(std::string* error) {
        skip();
        std::optional<Value> v = value(0);
        if (v) {
            skip();
            if (i_ != t_.size()) v = fail("text after the value");
        }
        if (!v && error) *error = error_ + " at offset " + std::to_string(i_);
        return v;
    }

private:
    static constexpr int kMaxDepth = 256;  // nested arrays and objects: bound of the stack

    std::nullopt_t fail(const char* why) {
        if (error_.empty()) error_ = why;
        return std::nullopt;
    }
    void skip() {
        while (i_ < t_.size() && (t_[i_] == ' ' || t_[i_] == '\t' || t_[i_] == '\n' || t_[i_] == '\r')) ++i_;
    }
    bool literal(std::string_view word) {
        if (t_.substr(i_, word.size()) != word) return false;
        i_ += word.size();
        return true;
    }

    std::optional<Value> value(int depth) {
        if (depth > kMaxDepth) return fail("nesting too deep");
        if (i_ >= t_.size()) return fail("unexpected end");
        const char c = t_[i_];
        if (c == '{') return object(depth);
        if (c == '[') return array(depth);
        if (c == '"') {
            std::optional<std::string> s = string();
            if (!s) return std::nullopt;
            return Value(std::move(*s));
        }
        if (literal("true")) return Value(true);
        if (literal("false")) return Value(false);
        if (literal("null")) return Value(nullptr);
        if (c == '-' || (c >= '0' && c <= '9')) return number();
        return fail("unexpected character");
    }

    // Grammar of RFC 8259: optional minus, 0 or digits without a leading zero, optional fraction and exponent.
    std::optional<Value> number() {
        const size_t from = i_;
        auto digits = [this] {
            const size_t s = i_;
            while (i_ < t_.size() && t_[i_] >= '0' && t_[i_] <= '9') ++i_;
            return i_ - s;
        };
        if (t_[i_] == '-') ++i_;
        if (i_ < t_.size() && t_[i_] == '0') ++i_;
        else if (digits() == 0) return fail("bad number");
        if (i_ < t_.size() && t_[i_] == '.') {
            ++i_;
            if (digits() == 0) return fail("bad number");
        }
        if (i_ < t_.size() && (t_[i_] == 'e' || t_[i_] == 'E')) {
            ++i_;
            if (i_ < t_.size() && (t_[i_] == '+' || t_[i_] == '-')) ++i_;
            if (digits() == 0) return fail("bad number");
        }
        const std::string_view tok = t_.substr(from, i_ - from);
        // Short integer (the case of the sequencer rows): exact without strtod.
        if (tok.size() <= 16 && tok.find_first_of(".eE") == std::string_view::npos) {
            const bool neg = tok[0] == '-';
            int64_t n = 0;
            for (size_t k = neg ? 1 : 0; k < tok.size(); ++k) n = n * 10 + (tok[k] - '0');
            return Value(neg ? (n ? -double(n) : -0.0) : double(n));
        }
        return Value(readNumber(std::string(tok)));
    }

    // Code point to UTF-8: 1 to 4 bytes.
    static void utf8(std::string& out, uint32_t cp) {
        if (cp < 0x80) {
            out += char(cp);
        } else if (cp < 0x800) {
            out += char(0xC0 | (cp >> 6));
            out += char(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += char(0xE0 | (cp >> 12));
            out += char(0x80 | ((cp >> 6) & 0x3F));
            out += char(0x80 | (cp & 0x3F));
        } else {
            out += char(0xF0 | (cp >> 18));
            out += char(0x80 | ((cp >> 12) & 0x3F));
            out += char(0x80 | ((cp >> 6) & 0x3F));
            out += char(0x80 | (cp & 0x3F));
        }
    }

    std::optional<uint32_t> hex4() {
        if (i_ + 4 > t_.size()) return std::nullopt;
        uint32_t v = 0;
        for (int k = 0; k < 4; ++k) {
            const char c = t_[i_++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= uint32_t(c - '0');
            else if (c >= 'a' && c <= 'f') v |= uint32_t(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= uint32_t(c - 'A' + 10);
            else return std::nullopt;
        }
        return v;
    }

    std::optional<std::string> string() {
        ++i_;  // "
        std::string out;
        while (true) {
            if (i_ >= t_.size()) return fail("unterminated string");
            const char c = t_[i_++];
            if (c == '"') return out;
            if (static_cast<unsigned char>(c) < 0x20) return fail("control character in string");
            if (c != '\\') {
                out += c;
                continue;
            }
            if (i_ >= t_.size()) return fail("unterminated string");
            switch (t_[i_++]) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                std::optional<uint32_t> cp = hex4();
                if (!cp) return fail("bad \\u escape");
                // Surrogate pair: a character outside the basic plane; alone, U+FFFD.
                if (*cp >= 0xD800 && *cp < 0xDC00 && t_.substr(i_, 2) == "\\u") {
                    const size_t save = i_;
                    i_ += 2;
                    const std::optional<uint32_t> lo = hex4();
                    if (lo && *lo >= 0xDC00 && *lo < 0xE000) {
                        utf8(out, 0x10000 + ((*cp - 0xD800) << 10) + (*lo - 0xDC00));
                        break;
                    }
                    i_ = save;
                }
                utf8(out, (*cp >= 0xD800 && *cp < 0xE000) ? 0xFFFD : *cp);
                break;
            }
            default: return fail("bad escape");
            }
        }
    }

    std::optional<Value> array(int depth) {
        ++i_;  // [
        // Only numbers so far: kept compact; at the first element of another type, ordinary array.
        std::vector<double> nums;
        Value a = Value::array();
        bool numeric = true;
        skip();
        if (i_ < t_.size() && t_[i_] == ']') {
            ++i_;
            return a;
        }
        while (true) {
            skip();
            std::optional<Value> v = value(depth + 1);
            if (!v) return std::nullopt;
            if (numeric && v->isNumber()) {
                nums.push_back(v->number());
            } else {
                if (numeric) {
                    numeric = false;
                    for (double d : nums) a.push(Value(d));
                }
                a.push(std::move(*v));
            }
            skip();
            if (i_ >= t_.size()) return fail("unterminated array");
            const char c = t_[i_++];
            if (c == ']') return numeric ? Value::numberArray(std::move(nums)) : a;
            if (c != ',') return fail("expected , or ]");
        }
    }

    std::optional<Value> object(int depth) {
        ++i_;  // {
        Value o = Value::object();
        skip();
        if (i_ < t_.size() && t_[i_] == '}') {
            ++i_;
            return o;
        }
        while (true) {
            skip();
            if (i_ >= t_.size() || t_[i_] != '"') return fail("expected a key");
            std::optional<std::string> k = string();
            if (!k) return std::nullopt;
            skip();
            if (i_ >= t_.size() || t_[i_] != ':') return fail("expected :");
            ++i_;
            skip();
            std::optional<Value> v = value(depth + 1);
            if (!v) return std::nullopt;
            o.set(std::move(*k), std::move(*v));  // repeated key: the last one wins, like JSON.parse
            skip();
            if (i_ >= t_.size()) return fail("unterminated object");
            const char c = t_[i_++];
            if (c == '}') return o;
            if (c != ',') return fail("expected , or }");
        }
    }

    std::string_view t_;
    size_t i_ = 0;
    std::string error_;
};

// ------------------------------------------------------------------------------------------- writing

// Number::toString of ECMAScript: shortest significant digits that read back as the same value, then decimal
// notation from 1e-6 to 1e21, scientific beyond.
std::string formatNumber(double v) {
    if (!std::isfinite(v)) return "null";  // JSON.stringify(NaN) = null
    if (v == 0) return "0";                // -0 too
    std::string out = v < 0 ? "-" : "";
    const double a = std::fabs(v);
    if (a < 1e15 && a == std::floor(a)) return out + std::to_string(int64_t(a));  // integer: its digits
    char buf[40];
    std::string digits;
    int exp10 = 0;
    // Fewest significant digits (1 to 17, the most a double needs) that read back as the same double.
    for (int p = 1; p <= 17; ++p) {
        std::snprintf(buf, sizeof buf, "%.*e", p - 1, a);
        if (readNumber(buf) != a && p < 17) continue;
        // buf: d[.ddd]e±xx (locale separator) -> digits and exponent.
        std::string s(buf);
        const size_t e = s.find('e');
        exp10 = std::atoi(s.c_str() + e + 1);
        for (size_t k = 0; k < e; ++k)
            if (s[k] >= '0' && s[k] <= '9') digits += s[k];
        break;
    }
    // k significant digits, and n such that the value is 0.digits × 10^n: the four layouts of Number::toString
    // (integer padded with zeros, point inside the digits, leading "0.000", exponent).
    while (digits.size() > 1 && digits.back() == '0') digits.pop_back();
    const int k = int(digits.size()), n = exp10 + 1;
    if (k <= n && n <= 21) {
        out += digits + std::string(size_t(n - k), '0');
    } else if (0 < n && n <= 21) {
        out += digits.substr(0, size_t(n)) + "." + digits.substr(size_t(n));
    } else if (-6 < n && n <= 0) {
        out += "0." + std::string(size_t(-n), '0') + digits;
    } else {
        out += digits.substr(0, 1);
        if (k > 1) out += "." + digits.substr(1);
        out += (n - 1 >= 0 ? "e+" : "e-") + std::to_string(std::abs(n - 1));
    }
    return out;
}

// String in quotes with the escapes of JSON.stringify: the short forms, \u00xx for the other control characters;
// UTF-8 bytes go out as they are.
void quote(std::string& out, const std::string& s) {
    out += '"';
    for (char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char b[8];
                std::snprintf(b, sizeof b, "\\u%04x", unsigned(static_cast<unsigned char>(c)));
                out += b;
            } else {
                out += c;
            }
        }
    }
    out += '"';
}

// Layout of JSON.stringify(v, null, indent): with an indent, each element and member on its own line, `indent`
// spaces per level, ": " after a key; empty arrays and objects stay "[]" and "{}".
void write(std::string& out, const Value& v, int indent, int level) {
    auto newline = [&](int l) {
        if (!indent) return;
        out += '\n';
        out.append(size_t(indent * l), ' ');
    };
    switch (v.type()) {
    case Value::Type::Null: out += "null"; break;
    case Value::Type::Bool: out += v.truthy() ? "true" : "false"; break;
    case Value::Type::Number: out += formatNumber(v.number()); break;
    case Value::Type::String: quote(out, v.string()); break;
    case Value::Type::Array:
        if (v.size() == 0) {
            out += "[]";
            break;
        }
        out += '[';
        if (const std::vector<double>* nums = v.numbers()) {
            for (size_t i = 0; i < nums->size(); ++i) {
                if (i) out += ',';
                newline(level + 1);
                out += formatNumber((*nums)[i]);
            }
        } else {
            for (size_t i = 0; i < v.items().size(); ++i) {
                if (i) out += ',';
                newline(level + 1);
                write(out, v.items()[i], indent, level + 1);
            }
        }
        newline(level);
        out += ']';
        break;
    case Value::Type::Object:
        if (v.members().empty()) {
            out += "{}";
            break;
        }
        out += '{';
        for (size_t i = 0; i < v.members().size(); ++i) {
            if (i) out += ',';
            newline(level + 1);
            quote(out, v.members()[i].first);
            out += indent ? ": " : ":";
            write(out, v.members()[i].second, indent, level + 1);
        }
        newline(level);
        out += '}';
        break;
    }
}

}  // namespace

std::optional<Value> parse(std::string_view text, std::string* error) {
    // UTF-8 byte order mark at the start: ignored.
    if (text.substr(0, 3) == "\xEF\xBB\xBF") text.remove_prefix(3);
    return Parser(text).document(error);
}

std::string dump(const Value& v, int indent) {
    std::string out;
    write(out, v, indent, 0);
    return out;
}

}  // namespace stym::plugin::json
