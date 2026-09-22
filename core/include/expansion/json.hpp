// Minimal, strict, dependency-free JSON reader/writer.
//
// Why hand-rolled: TDD 19.4 requires pinned dependencies and a core that builds
// with no engine and no network fetch. This reader is deliberately narrow:
//
//   * No floating point. Every numeric literal must be an integer. Content
//     authoring uses integers only (milli-units and basis points, TDD 4.1) and
//     runtime saves encode 64-bit quantities as decimal strings.
//   * Object keys are stored in a bytewise-sorted map, so parse order cannot
//     influence anything downstream (TDD 5.3).
//   * Serialisation is canonical: bytewise-sorted keys, no optional whitespace,
//     normalised escaping (TDD 16.1).
//   * Input limits are enforced: nesting depth, array length, payload size
//     (TDD 16.4).
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "expansion/units.hpp"

namespace expansion::json {

// Bytewise (unsigned) string ordering, independent of char signedness.
struct BytewiseLess {
  bool operator()(const std::string& a, const std::string& b) const noexcept {
    const std::size_t n = a.size() < b.size() ? a.size() : b.size();
    for (std::size_t i = 0; i < n; ++i) {
      unsigned char ca = static_cast<unsigned char>(a[i]);
      unsigned char cb = static_cast<unsigned char>(b[i]);
      if (ca != cb) return ca < cb;
    }
    return a.size() < b.size();
  }
};

class Value;
using Object = std::map<std::string, Value, BytewiseLess>;
using Array = std::vector<Value>;

enum class Kind { Null, Bool, Int, Str, Arr, Obj };

class Value {
 public:
  Value() : kind_(Kind::Null) {}
  static Value null() { return Value(); }
  static Value boolean(bool b);
  static Value integer(std::int64_t v);
  static Value string(std::string s);
  static Value array(Array a);
  static Value object(Object o);

  Kind kind() const { return kind_; }
  bool is_null() const { return kind_ == Kind::Null; }
  bool is_bool() const { return kind_ == Kind::Bool; }
  bool is_int() const { return kind_ == Kind::Int; }
  bool is_string() const { return kind_ == Kind::Str; }
  bool is_array() const { return kind_ == Kind::Arr; }
  bool is_object() const { return kind_ == Kind::Obj; }

  bool as_bool() const;
  std::int64_t as_int() const;
  const std::string& as_string() const;
  const Array& as_array() const;
  const Object& as_object() const;
  Array& mutable_array();
  Object& mutable_object();

  // Object field access. `find` returns nullptr when absent.
  const Value* find(const std::string& key) const;
  // Throws SimError naming `context` and `key` when absent or of the wrong type.
  const Value& require(const std::string& key, const std::string& context) const;
  std::int64_t require_int(const std::string& key, const std::string& context) const;
  const std::string& require_string(const std::string& key, const std::string& context) const;
  const Array& require_array(const std::string& key, const std::string& context) const;
  const Object& require_object(const std::string& key, const std::string& context) const;
  std::int64_t int_or(const std::string& key, std::int64_t fallback) const;
  bool bool_or(const std::string& key, bool fallback) const;
  std::string string_or(const std::string& key, const std::string& fallback) const;
  // 64-bit quantity encoded as a decimal string (runtime save format, TDD 4.1).
  std::int64_t require_decimal(const std::string& key, const std::string& context) const;
  std::uint64_t require_decimal_u(const std::string& key, const std::string& context) const;

  void set(const std::string& key, Value v);
  void push_back(Value v);
  // Rejects unexpected keys so a typo in content cannot be silently ignored.
  void reject_unknown_keys(const std::vector<std::string>& allowed, const std::string& context) const;

 private:
  Kind kind_;
  bool b_ = false;
  std::int64_t i_ = 0;
  std::string s_;
  std::shared_ptr<Array> a_;
  std::shared_ptr<Object> o_;
};

struct Limits {
  std::size_t max_bytes = 16u * 1024u * 1024u;  // TDD 16.4
  std::size_t max_depth = 64;
  std::size_t max_array = 100000;
  std::size_t max_object_keys = 4096;
};

// Parses strict JSON. Throws SimError with a byte offset on any violation.
Value parse(const std::string& text, const Limits& limits = Limits{});
// Canonical serialisation: sorted keys, no optional whitespace, normalised escapes.
std::string serialize_canonical(const Value& v);
// Human-readable serialisation for diagnostics only. Never hashed or checksummed.
std::string serialize_pretty(const Value& v, int indent = 0);

// Convenience builders for encoding state.
Value dec(std::int64_t v);   // integer as a canonical decimal string
Value dec_u(std::uint64_t v);

}  // namespace expansion::json
