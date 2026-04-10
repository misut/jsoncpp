module;
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>
export module json;

export namespace json {

class Value;
using Object = std::map<std::string, Value>;
using Array = std::vector<Value>;

// ParseError carries line + message info. On hosts that disable C++
// exceptions (wasi-sdk WASM target), the parse path that would normally
// throw aborts with a diagnostic instead — see fail() below. The class
// itself still compiles in either mode for callers that catch it.
class ParseError : public std::runtime_error {
public:
    ParseError(std::size_t line, std::string const& msg)
        : std::runtime_error(std::format("line {}: {}", line, msg))
        , line_{line} {}

    std::size_t line() const { return line_; }

private:
    std::size_t line_;
};

namespace detail {
// Throw when C++ exceptions are enabled, abort otherwise. Lets the same
// parser implementation compile under both -fexceptions (native) and
// -fno-exceptions (wasi-sdk).
[[noreturn]] inline void fail(std::size_t line, std::string const& msg) {
#if __cpp_exceptions
    throw ParseError(line, msg);
#else
    std::fprintf(stderr, "json: line %zu: %s\n", line, msg.c_str());
    std::abort();
#endif
}
} // namespace detail

class Value {
public:
    Value() : data_{nullptr} {}
    Value(std::nullptr_t) : data_{nullptr} {}
    Value(bool v) : data_{v} {}
    Value(std::int64_t v) : data_{v} {}
    Value(int v) : data_{static_cast<std::int64_t>(v)} {}
    Value(unsigned v) : data_{static_cast<std::int64_t>(v)} {}
    Value(unsigned long v) : data_{static_cast<std::int64_t>(v)} {}
    Value(unsigned long long v) : data_{static_cast<std::int64_t>(v)} {}
    Value(double v) : data_{v} {}
    Value(std::string v) : data_{std::move(v)} {}
    Value(char const* v) : data_{std::string{v}} {}
    Value(std::string_view v) : data_{std::string{v}} {}
    Value(Array v) : data_{std::make_unique<Array>(std::move(v))} {}
    Value(Object v) : data_{std::make_unique<Object>(std::move(v))} {}

    Value(Value const& other);
    Value(Value&&) noexcept = default;
    Value& operator=(Value const& other);
    Value& operator=(Value&&) noexcept = default;
    ~Value() = default;

    bool is_null() const { return std::holds_alternative<std::nullptr_t>(data_); }
    bool is_bool() const { return std::holds_alternative<bool>(data_); }
    bool is_integer() const { return std::holds_alternative<std::int64_t>(data_); }
    bool is_float() const { return std::holds_alternative<double>(data_); }
    bool is_number() const { return is_integer() || is_float(); }
    bool is_string() const { return std::holds_alternative<std::string>(data_); }
    bool is_array() const { return std::holds_alternative<std::unique_ptr<Array>>(data_); }
    bool is_object() const { return std::holds_alternative<std::unique_ptr<Object>>(data_); }

    bool as_bool() const { return std::get<bool>(data_); }
    std::int64_t as_integer() const {
        if (is_float()) return static_cast<std::int64_t>(std::get<double>(data_));
        return std::get<std::int64_t>(data_);
    }
    double as_float() const {
        if (is_integer()) return static_cast<double>(std::get<std::int64_t>(data_));
        return std::get<double>(data_);
    }
    std::string const& as_string() const { return std::get<std::string>(data_); }
    Array const& as_array() const { return *std::get<std::unique_ptr<Array>>(data_); }
    Array& as_array() { return *std::get<std::unique_ptr<Array>>(data_); }
    Object const& as_object() const { return *std::get<std::unique_ptr<Object>>(data_); }
    Object& as_object() { return *std::get<std::unique_ptr<Object>>(data_); }

    bool contains(std::string const& key) const {
        return is_object() && as_object().contains(key);
    }

    Value const& operator[](std::string const& key) const {
        return as_object().at(key);
    }
    Value& operator[](std::string const& key) {
        return as_object().at(key);
    }
    Value const& operator[](std::size_t index) const {
        return as_array().at(index);
    }
    Value& operator[](std::size_t index) {
        return as_array().at(index);
    }

private:
    using Data = std::variant<
        std::nullptr_t, bool, std::int64_t, double, std::string,
        std::unique_ptr<Array>, std::unique_ptr<Object>
    >;
    Data data_;
};

// Parse — throws ParseError on malformed input
Value parse(std::string_view input);
Value parse_file(std::string_view path);

// Emit — minified by default, pretty=true uses 2-space indent
std::string emit(Value const& value, bool pretty = false);
void emit_to(Value const& value, std::string& out, bool pretty = false);

} // namespace json

// =============================================================
// Implementation
// =============================================================

namespace json {

Value::Value(Value const& other) {
    std::visit([this](auto const& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, std::unique_ptr<Array>>) {
            data_ = std::make_unique<Array>(*v);
        } else if constexpr (std::is_same_v<T, std::unique_ptr<Object>>) {
            data_ = std::make_unique<Object>(*v);
        } else {
            data_ = v;
        }
    }, other.data_);
}

Value& Value::operator=(Value const& other) {
    if (this != &other) {
        Value tmp{other};
        *this = std::move(tmp);
    }
    return *this;
}

namespace detail {

// =============================================================
// Parser
// =============================================================

class Parser {
public:
    explicit Parser(std::string_view src) : src_{src} {}

    Value parse_root() {
        skip_whitespace();
        Value v = parse_value();
        skip_whitespace();
        if (pos_ != src_.size())
            fail(line_, "trailing data after root value");
        return v;
    }

private:
    std::string_view src_;
    std::size_t      pos_  = 0;
    std::size_t      line_ = 1;

    [[noreturn]] void error(std::string const& msg) {
        fail(line_, msg);
    }

    char peek() {
        if (pos_ >= src_.size()) error("unexpected end of input");
        return src_[pos_];
    }

    char consume() {
        if (pos_ >= src_.size()) error("unexpected end of input");
        char c = src_[pos_++];
        if (c == '\n') ++line_;
        return c;
    }

    bool match(char c) {
        if (pos_ < src_.size() && src_[pos_] == c) { ++pos_; return true; }
        return false;
    }

    void expect(char c) {
        if (pos_ >= src_.size() || src_[pos_] != c)
            error(std::format("expected '{}'", c));
        if (src_[pos_] == '\n') ++line_;
        ++pos_;
    }

    void skip_whitespace() {
        while (pos_ < src_.size()) {
            char c = src_[pos_];
            if (c == ' ' || c == '\t' || c == '\r') { ++pos_; continue; }
            if (c == '\n') { ++line_; ++pos_; continue; }
            break;
        }
    }

    Value parse_value() {
        skip_whitespace();
        if (pos_ >= src_.size()) error("unexpected end of input");
        char c = src_[pos_];
        if (c == '"') return parse_string();
        if (c == '{') return parse_object();
        if (c == '[') return parse_array();
        if (c == 't' || c == 'f') return parse_bool();
        if (c == 'n') return parse_null();
        if (c == '-' || (c >= '0' && c <= '9')) return parse_number();
        error(std::format("unexpected character '{}'", c));
    }

    Value parse_null() {
        if (src_.substr(pos_, 4) != "null") error("expected 'null'");
        pos_ += 4;
        return Value{nullptr};
    }

    Value parse_bool() {
        if (src_.substr(pos_, 4) == "true") { pos_ += 4; return Value{true}; }
        if (src_.substr(pos_, 5) == "false") { pos_ += 5; return Value{false}; }
        error("expected 'true' or 'false'");
    }

    Value parse_number() {
        std::size_t start = pos_;
        if (src_[pos_] == '-') ++pos_;
        if (pos_ >= src_.size()) error("expected digit");
        if (src_[pos_] == '0') {
            ++pos_;
        } else if (src_[pos_] >= '1' && src_[pos_] <= '9') {
            while (pos_ < src_.size() && src_[pos_] >= '0' && src_[pos_] <= '9') ++pos_;
        } else {
            error("expected digit");
        }
        bool is_float = false;
        if (pos_ < src_.size() && src_[pos_] == '.') {
            is_float = true;
            ++pos_;
            if (pos_ >= src_.size() || src_[pos_] < '0' || src_[pos_] > '9')
                error("expected digit after '.'");
            while (pos_ < src_.size() && src_[pos_] >= '0' && src_[pos_] <= '9') ++pos_;
        }
        if (pos_ < src_.size() && (src_[pos_] == 'e' || src_[pos_] == 'E')) {
            is_float = true;
            ++pos_;
            if (pos_ < src_.size() && (src_[pos_] == '+' || src_[pos_] == '-')) ++pos_;
            if (pos_ >= src_.size() || src_[pos_] < '0' || src_[pos_] > '9')
                error("expected digit in exponent");
            while (pos_ < src_.size() && src_[pos_] >= '0' && src_[pos_] <= '9') ++pos_;
        }
        std::string text{src_.substr(start, pos_ - start)};
        if (is_float) {
            return Value{std::stod(text)};
        }
        // Hand-roll the i64 parse so we can detect overflow without
        // exceptions (wasi-sdk builds with -fno-exceptions, so std::stoll's
        // exception path can't be caught). On overflow we fall through to
        // a double, mirroring the previous catch behavior.
        bool negative = (text[0] == '-');
        std::size_t i = negative ? 1 : 0;
        std::uint64_t mag = 0;
        bool overflowed = false;
        for (; i < text.size(); ++i) {
            std::uint64_t digit = static_cast<std::uint64_t>(text[i] - '0');
            if (mag > (std::numeric_limits<std::uint64_t>::max() - digit) / 10) {
                overflowed = true;
                break;
            }
            mag = mag * 10 + digit;
        }
        std::uint64_t i64_max_mag =
            negative ? static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) + 1
                     : static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
        if (overflowed || mag > i64_max_mag) {
            return Value{std::stod(text)};
        }
        std::int64_t signed_value = negative
            ? -static_cast<std::int64_t>(mag)
            : static_cast<std::int64_t>(mag);
        return Value{signed_value};
    }

    Value parse_string() {
        expect('"');
        std::string out;
        while (true) {
            if (pos_ >= src_.size()) error("unterminated string");
            char c = src_[pos_];
            if (c == '"') { ++pos_; return Value{std::move(out)}; }
            if (c == '\\') {
                ++pos_;
                if (pos_ >= src_.size()) error("unterminated escape");
                char esc = src_[pos_++];
                switch (esc) {
                    case '"':  out.push_back('"');  break;
                    case '\\': out.push_back('\\'); break;
                    case '/':  out.push_back('/');  break;
                    case 'b':  out.push_back('\b'); break;
                    case 'f':  out.push_back('\f'); break;
                    case 'n':  out.push_back('\n'); break;
                    case 'r':  out.push_back('\r'); break;
                    case 't':  out.push_back('\t'); break;
                    case 'u': {
                        if (pos_ + 4 > src_.size()) error("incomplete \\u escape");
                        unsigned int cp = 0;
                        for (int i = 0; i < 4; ++i) {
                            char h = src_[pos_++];
                            cp <<= 4;
                            if (h >= '0' && h <= '9') cp |= (h - '0');
                            else if (h >= 'a' && h <= 'f') cp |= (h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F') cp |= (h - 'A' + 10);
                            else error("invalid hex digit in \\u escape");
                        }
                        // Encode as UTF-8 (BMP only — surrogate pairs not handled)
                        if (cp < 0x80) {
                            out.push_back(static_cast<char>(cp));
                        } else if (cp < 0x800) {
                            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                        } else {
                            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                        }
                        break;
                    }
                    default: error(std::format("invalid escape '\\{}'", esc));
                }
                continue;
            }
            if (static_cast<unsigned char>(c) < 0x20)
                error("control character in string");
            out.push_back(c);
            ++pos_;
        }
    }

    Value parse_array() {
        expect('[');
        Array arr;
        skip_whitespace();
        if (match(']')) return Value{std::move(arr)};
        while (true) {
            arr.push_back(parse_value());
            skip_whitespace();
            if (match(',')) { skip_whitespace(); continue; }
            if (match(']')) return Value{std::move(arr)};
            error("expected ',' or ']' in array");
        }
    }

    Value parse_object() {
        expect('{');
        Object obj;
        skip_whitespace();
        if (match('}')) return Value{std::move(obj)};
        while (true) {
            skip_whitespace();
            Value key = parse_string();
            if (!key.is_string()) error("object key must be string");
            skip_whitespace();
            expect(':');
            Value val = parse_value();
            obj.emplace(key.as_string(), std::move(val));
            skip_whitespace();
            if (match(',')) continue;
            if (match('}')) return Value{std::move(obj)};
            error("expected ',' or '}' in object");
        }
    }
};

// =============================================================
// Emitter
// =============================================================

class Emitter {
public:
    explicit Emitter(std::string& out, bool pretty)
        : out_{out}, pretty_{pretty} {}

    void emit_value(Value const& v, int indent = 0) {
        if (v.is_null())     { out_ += "null"; return; }
        if (v.is_bool())     { out_ += v.as_bool() ? "true" : "false"; return; }
        if (v.is_integer())  { out_ += std::format("{}", v.as_integer()); return; }
        if (v.is_float())    { emit_float(v.as_float()); return; }
        if (v.is_string())   { emit_string(v.as_string()); return; }
        if (v.is_array())    { emit_array(v.as_array(), indent); return; }
        if (v.is_object())   { emit_object(v.as_object(), indent); return; }
    }

private:
    std::string& out_;
    bool pretty_;

    void newline_indent(int indent) {
        if (!pretty_) return;
        out_.push_back('\n');
        for (int i = 0; i < indent * 2; ++i) out_.push_back(' ');
    }

    void emit_float(double d) {
        if (std::isnan(d) || std::isinf(d)) {
            out_ += "null"; // JSON has no NaN/Inf; emit null per common convention
            return;
        }
        out_ += std::format("{}", d);
    }

    void emit_string(std::string const& s) {
        out_.push_back('"');
        for (char c : s) {
            switch (c) {
                case '"':  out_ += "\\\""; break;
                case '\\': out_ += "\\\\"; break;
                case '\b': out_ += "\\b";  break;
                case '\f': out_ += "\\f";  break;
                case '\n': out_ += "\\n";  break;
                case '\r': out_ += "\\r";  break;
                case '\t': out_ += "\\t";  break;
                default:
                    if (static_cast<unsigned char>(c) < 0x20) {
                        out_ += std::format("\\u{:04x}", static_cast<unsigned int>(c));
                    } else {
                        out_.push_back(c);
                    }
            }
        }
        out_.push_back('"');
    }

    void emit_array(Array const& arr, int indent) {
        if (arr.empty()) { out_ += "[]"; return; }
        out_.push_back('[');
        bool first = true;
        for (auto const& el : arr) {
            if (!first) out_.push_back(',');
            first = false;
            newline_indent(indent + 1);
            emit_value(el, indent + 1);
        }
        newline_indent(indent);
        out_.push_back(']');
    }

    void emit_object(Object const& obj, int indent) {
        if (obj.empty()) { out_ += "{}"; return; }
        out_.push_back('{');
        bool first = true;
        for (auto const& [k, v] : obj) {
            if (!first) out_.push_back(',');
            first = false;
            newline_indent(indent + 1);
            emit_string(k);
            out_.push_back(':');
            if (pretty_) out_.push_back(' ');
            emit_value(v, indent + 1);
        }
        newline_indent(indent);
        out_.push_back('}');
    }
};

} // namespace detail

Value parse(std::string_view input) {
    detail::Parser p{input};
    return p.parse_root();
}

Value parse_file(std::string_view path) {
    std::ifstream file{std::string{path}, std::ios::binary};
    if (!file) detail::fail(0, std::format("cannot open file '{}'", path));
    std::stringstream buf;
    buf << file.rdbuf();
    return parse(buf.str());
}

std::string emit(Value const& value, bool pretty) {
    std::string out;
    emit_to(value, out, pretty);
    return out;
}

void emit_to(Value const& value, std::string& out, bool pretty) {
    detail::Emitter e{out, pretty};
    e.emit_value(value);
}

} // namespace json
