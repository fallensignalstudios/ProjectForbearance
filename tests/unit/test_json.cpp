// Malformed-data tests for the strict reader and canonical writer (TDD 16.4).
#include "expansion/json.hpp"
#include "harness.hpp"

using namespace expansion;
using namespace expansion::json;

TEST(json_canonical, "canonical output sorts keys bytewise and omits whitespace") {
  Value v = parse(R"({"b":2,"a":1,"C":{"z":[1,2,3],"y":"x"}})");
  CHECK_EQ(serialize_canonical(v), std::string(R"({"C":{"y":"x","z":[1,2,3]},"a":1,"b":2})"));
  // Parse order cannot influence the canonical form.
  Value w = parse(R"({"a":1,"C":{"y":"x","z":[1,2,3]},"b":2})");
  CHECK_EQ(serialize_canonical(v), serialize_canonical(w));
}

TEST(json_rejects_malformed, "malformed and unsupported input is rejected with a byte offset") {
  CHECK_THROWS(parse("{"));
  CHECK_THROWS(parse("{\"a\":1,}"));
  CHECK_THROWS(parse("{\"a\":1}{"));
  CHECK_THROWS(parse("{\"a\":01}"));
  CHECK_THROWS(parse("{\"a\":1.5}"));      // no floating point anywhere
  CHECK_THROWS(parse("{\"a\":1e3}"));
  CHECK_THROWS(parse("{\"a\":1,\"a\":2}"));  // duplicate key
  CHECK_THROWS(parse("// comment\n{}"));
  CHECK_THROWS(parse("{\"a\":\"\x01\"}"));   // unescaped control character
  CHECK_THROWS(parse("[\"\\ud800\"]"));      // unpaired surrogate
}

TEST(json_limits, "input limits are enforced rather than trusted") {
  Limits small;
  small.max_depth = 3;
  small.max_array = 2;
  small.max_bytes = 40;
  CHECK_THROWS(parse("[[[[1]]]]", small));
  CHECK_THROWS(parse("[1,2,3]", small));
  CHECK_THROWS(parse(std::string("[\"") + std::string(60, 'x') + "\"]", small));
}

TEST(json_escapes, "escaping is normalised and round-trips") {
  Value v = parse(R"({"s":"a\"b\\c\nd\u0007e"})");
  const std::string out = serialize_canonical(v);
  CHECK_EQ(out, std::string(R"({"s":"a\"b\\c\nd\u0007e"})"));
  CHECK_EQ(serialize_canonical(parse(out)), out);
}

TEST(json_unknown_keys, "an unexpected field is an error, not silently ignored") {
  Value v = parse(R"({"id":"x","typo":1})");
  CHECK_THROWS(v.reject_unknown_keys({"id"}, "test"));
  v.reject_unknown_keys({"id", "typo"}, "test");
}
