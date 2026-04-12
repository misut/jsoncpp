import json;
import std;

int failed = 0;

void check(bool cond, std::string_view msg) {
    if (!cond) {
        std::println(std::cerr, "FAIL: {}", msg);
        ++failed;
    }
}

// =============================================================
// Parse — primitive types
// =============================================================

void test_parse_null() {
    auto v = json::parse("null");
    check(v.is_null(), "null type");
}

void test_parse_bool() {
    auto t = json::parse("true");
    auto f = json::parse("false");
    check(t.is_bool() && t.as_bool() == true, "true");
    check(f.is_bool() && f.as_bool() == false, "false");
}

void test_parse_integer() {
    auto v = json::parse("42");
    check(v.is_integer(), "integer type");
    check(v.as_integer() == 42, "integer value");
}

void test_parse_negative_integer() {
    auto v = json::parse("-17");
    check(v.as_integer() == -17, "negative integer");
}

void test_parse_zero() {
    auto v = json::parse("0");
    check(v.as_integer() == 0, "zero");
}

void test_parse_float() {
    auto v = json::parse("3.14");
    check(v.is_float(), "float type");
    check(std::abs(v.as_float() - 3.14) < 1e-9, "float value");
}

void test_parse_float_exponent() {
    auto v = json::parse("1.5e3");
    check(v.is_float(), "float exp type");
    check(v.as_float() == 1500.0, "float exp value");
}

void test_parse_negative_float() {
    auto v = json::parse("-2.5");
    check(v.as_float() == -2.5, "negative float");
}

void test_parse_string() {
    auto v = json::parse(R"("hello")");
    check(v.is_string(), "string type");
    check(v.as_string() == "hello", "string value");
}

void test_parse_string_escapes() {
    auto v = json::parse(R"("a\nb\tc\\d\"e")");
    check(v.as_string() == "a\nb\tc\\d\"e", "string escapes");
}

void test_parse_string_unicode_escape() {
    // \u00e9 is 'é'
    auto v = json::parse(R"("caf\u00e9")");
    // UTF-8 of 'é' is 0xC3 0xA9
    std::string expected = "caf\xc3\xa9";
    check(v.as_string() == expected, "unicode \\u escape");
}

void test_parse_empty_string() {
    auto v = json::parse(R"("")");
    check(v.is_string() && v.as_string().empty(), "empty string");
}

// =============================================================
// Parse — collections
// =============================================================

void test_parse_empty_array() {
    auto v = json::parse("[]");
    check(v.is_array(), "empty array type");
    check(v.as_array().empty(), "empty array size");
}

void test_parse_array() {
    auto v = json::parse("[1, 2, 3]");
    check(v.is_array(), "array type");
    auto const& a = v.as_array();
    check(a.size() == 3, "array size");
    check(a[0].as_integer() == 1, "array[0]");
    check(a[2].as_integer() == 3, "array[2]");
}

void test_parse_mixed_array() {
    auto v = json::parse(R"([1, "two", true, null, 3.5])");
    auto const& a = v.as_array();
    check(a.size() == 5, "mixed array size");
    check(a[0].is_integer(), "mixed[0] integer");
    check(a[1].is_string(), "mixed[1] string");
    check(a[2].is_bool(), "mixed[2] bool");
    check(a[3].is_null(), "mixed[3] null");
    check(a[4].is_float(), "mixed[4] float");
}

void test_parse_nested_array() {
    auto v = json::parse("[[1, 2], [3, 4]]");
    auto const& outer = v.as_array();
    check(outer.size() == 2, "nested outer size");
    check(outer[0].as_array().size() == 2, "nested inner size");
    check(outer[1].as_array()[1].as_integer() == 4, "nested inner value");
}

void test_parse_empty_object() {
    auto v = json::parse("{}");
    check(v.is_object(), "empty object type");
    check(v.as_object().empty(), "empty object size");
    // table aliases
    check(v.is_table(), "empty table alias");
    check(v.as_table().empty(), "empty table alias size");
}

void test_parse_object() {
    auto v = json::parse(R"({"name": "alice", "age": 30})");
    check(v.is_object(), "object type");
    auto const& o = v.as_object();
    check(o.contains("name"), "object key name");
    check(o.contains("age"), "object key age");
    check(o.at("name").as_string() == "alice", "object name value");
    check(o.at("age").as_integer() == 30, "object age value");
    // table aliases return the same reference
    check(&v.as_table() == &v.as_object(), "table alias same ref");
}

void test_parse_nested_object() {
    auto v = json::parse(R"({"server": {"host": "localhost", "port": 8080}})");
    auto const& server = v.as_object().at("server").as_object();
    check(server.at("host").as_string() == "localhost", "nested host");
    check(server.at("port").as_integer() == 8080, "nested port");
}

void test_parse_object_with_array() {
    auto v = json::parse(R"({"tags": ["c++", "json", "wasm"]})");
    auto const& tags = v.as_object().at("tags").as_array();
    check(tags.size() == 3, "object array size");
    check(tags[0].as_string() == "c++", "object array element");
}

// =============================================================
// Parse — whitespace + edge cases
// =============================================================

void test_parse_whitespace() {
    auto v = json::parse("  \n\t [\n  1,\n  2\n]  ");
    check(v.as_array().size() == 2, "whitespace array");
}

void test_parse_invalid_throws() {
    bool threw = false;
    try {
        json::parse("{invalid}");
    } catch (json::ParseError const&) {
        threw = true;
    }
    check(threw, "invalid throws");
}

void test_parse_trailing_data_throws() {
    bool threw = false;
    try {
        json::parse("1 2");
    } catch (json::ParseError const&) {
        threw = true;
    }
    check(threw, "trailing data throws");
}

// =============================================================
// Emit
// =============================================================

void test_emit_null() {
    check(json::emit(json::Value{nullptr}) == "null", "emit null");
}

void test_emit_bool() {
    check(json::emit(json::Value{true}) == "true", "emit true");
    check(json::emit(json::Value{false}) == "false", "emit false");
}

void test_emit_integer() {
    check(json::emit(json::Value{std::int64_t{42}}) == "42", "emit integer");
    check(json::emit(json::Value{std::int64_t{-7}}) == "-7", "emit negative integer");
}

void test_emit_string() {
    check(json::emit(json::Value{"hello"}) == "\"hello\"", "emit string");
}

void test_emit_string_escapes() {
    auto out = json::emit(json::Value{"a\nb\"c\\d"});
    check(out == R"("a\nb\"c\\d")", "emit string escapes");
}

void test_emit_string_control_char() {
    auto out = json::emit(json::Value{std::string{"x\x01y"}});
    check(out == R"("x\u0001y")", "emit control char");
}

void test_emit_empty_array() {
    check(json::emit(json::Value{json::Array{}}) == "[]", "emit empty array");
}

void test_emit_array() {
    json::Array a;
    a.push_back(json::Value{std::int64_t{1}});
    a.push_back(json::Value{std::int64_t{2}});
    a.push_back(json::Value{std::int64_t{3}});
    check(json::emit(json::Value{std::move(a)}) == "[1,2,3]", "emit array");
}

void test_emit_empty_object() {
    check(json::emit(json::Value{json::Object{}}) == "{}", "emit empty object");
}

void test_emit_object() {
    json::Object o;
    o.emplace("a", json::Value{std::int64_t{1}});
    o.emplace("b", json::Value{"x"});
    auto out = json::emit(json::Value{std::move(o)});
    // std::map orders by key alphabetically, so output is deterministic
    check(out == R"({"a":1,"b":"x"})", "emit object");
}

void test_emit_pretty() {
    json::Object o;
    o.emplace("k", json::Value{std::int64_t{1}});
    auto out = json::emit(json::Value{std::move(o)}, /*pretty=*/true);
    check(out == "{\n  \"k\": 1\n}", "emit pretty");
}

void test_emit_nested_pretty() {
    json::Object inner;
    inner.emplace("x", json::Value{std::int64_t{1}});
    json::Object outer;
    outer.emplace("nested", json::Value{std::move(inner)});
    auto out = json::emit(json::Value{std::move(outer)}, /*pretty=*/true);
    std::string expected = "{\n  \"nested\": {\n    \"x\": 1\n  }\n}";
    check(out == expected, "emit nested pretty");
}

// =============================================================
// Round-trip
// =============================================================

void test_round_trip() {
    // Object keys in std::map are sorted alphabetically, so the input
    // here is already in canonical (sorted) order — emit(parse(x)) == x.
    std::string_view input = R"({"authors":["misut"],"deps":[],"name":"jsoncpp","stable":true,"version":"0.1.0"})";
    auto v = json::parse(input);
    auto out = json::emit(v);
    check(out == input, "round-trip preserves shape");
}

// =============================================================
// Runner
// =============================================================

int main() {
    test_parse_null();
    test_parse_bool();
    test_parse_integer();
    test_parse_negative_integer();
    test_parse_zero();
    test_parse_float();
    test_parse_float_exponent();
    test_parse_negative_float();
    test_parse_string();
    test_parse_string_escapes();
    test_parse_string_unicode_escape();
    test_parse_empty_string();
    test_parse_empty_array();
    test_parse_array();
    test_parse_mixed_array();
    test_parse_nested_array();
    test_parse_empty_object();
    test_parse_object();
    test_parse_nested_object();
    test_parse_object_with_array();
    test_parse_whitespace();
    test_parse_invalid_throws();
    test_parse_trailing_data_throws();

    test_emit_null();
    test_emit_bool();
    test_emit_integer();
    test_emit_string();
    test_emit_string_escapes();
    test_emit_string_control_char();
    test_emit_empty_array();
    test_emit_array();
    test_emit_empty_object();
    test_emit_object();
    test_emit_pretty();
    test_emit_nested_pretty();

    test_round_trip();

    if (failed > 0) {
        std::println(std::cerr, "{} test(s) failed", failed);
        return 1;
    }
    std::println("all tests passed");
    return 0;
}
