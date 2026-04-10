# jsoncpp

A JSON parser and emitter for C++. No dependencies.

## Installation

### exon

```toml
[dependencies]
"github.com/misut/jsoncpp" = "0.1.0"
```

### CMake

```cmake
add_library(jsoncpp)
target_sources(jsoncpp PUBLIC FILE_SET CXX_MODULES FILES path/to/json.cppm)
target_link_libraries(your_target PRIVATE jsoncpp)
```

## Quick Start

```cpp
import json;

// Parse
auto v = json::parse(R"({"name": "alice", "tags": ["a", "b"]})");
auto name = v.as_object().at("name").as_string();
auto tag0 = v["tags"][0].as_string();

// Build + emit
json::Object o;
o.emplace("count", json::Value{std::int64_t{42}});
o.emplace("ok", json::Value{true});
auto json_str = json::emit(json::Value{std::move(o)});
// {"count":42,"ok":true}

// Pretty print
auto pretty = json::emit(json::Value{std::move(o)}, /*pretty=*/true);
```

## API

### Parsing

```cpp
json::Value parse(std::string_view input);          // throws json::ParseError
json::Value parse_file(std::string_view path);
```

### Emitting

```cpp
std::string emit(json::Value const& value, bool pretty = false);
void        emit_to(json::Value const& value, std::string& out, bool pretty = false);
```

### Types

| Type | Alias |
|------|-------|
| `json::Object` | `std::map<std::string, Value>` |
| `json::Array`  | `std::vector<Value>` |
| `json::Value`  | variant of `nullptr_t`, `bool`, `int64_t`, `double`, `string`, `Array`, `Object` |

### Value methods

```cpp
// Type checks
v.is_null() v.is_bool()    v.is_integer() v.is_float() v.is_number()
v.is_string() v.is_array() v.is_object()

// Accessors
v.as_bool() v.as_integer() v.as_float() v.as_string()
v.as_array() v.as_object()

// Helpers
v.contains("key")           // object only
v["key"]                    // object only
v[0]                        // array only
```

### Errors

```cpp
try {
    auto v = json::parse(input);
} catch (json::ParseError const& e) {
    // e.what() → "line 3: expected ':'"
    // e.line() → 3
}
```

## Supported JSON features

- Primitive types: `null`, `true`, `false`, integers, floats (incl. exponent notation), strings
- String escapes: `\"`, `\\`, `\/`, `\b`, `\f`, `\n`, `\r`, `\t`, `\uXXXX` (BMP)
- Arrays, objects (nested, mixed types, empty)
- Whitespace handling per RFC 8259

Surrogate pair handling for `\uXXXX` outside BMP is not yet implemented (round-trips
within `as_string()` work via raw UTF-8, but `\u` escape pairs > U+FFFF aren't decoded).

## License

MIT
