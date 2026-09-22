#include "expansion/json.hpp"

#include <cstddef>
#include <cstring>
#include <utility>

namespace expansion::json {

Value Value::boolean(bool b) {
  Value v;
  v.kind_ = Kind::Bool;
  v.b_ = b;
  return v;
}
Value Value::integer(std::int64_t i) {
  Value v;
  v.kind_ = Kind::Int;
  v.i_ = i;
  return v;
}
Value Value::string(std::string s) {
  Value v;
  v.kind_ = Kind::Str;
  v.s_ = std::move(s);
  return v;
}
Value Value::array(Array a) {
  Value v;
  v.kind_ = Kind::Arr;
  v.a_ = std::make_shared<Array>(std::move(a));
  return v;
}
Value Value::object(Object o) {
  Value v;
  v.kind_ = Kind::Obj;
  v.o_ = std::make_shared<Object>(std::move(o));
  return v;
}

bool Value::as_bool() const {
  if (kind_ != Kind::Bool) throw SimError("json: expected bool");
  return b_;
}
std::int64_t Value::as_int() const {
  if (kind_ != Kind::Int) throw SimError("json: expected integer");
  return i_;
}
const std::string& Value::as_string() const {
  if (kind_ != Kind::Str) throw SimError("json: expected string");
  return s_;
}
const Array& Value::as_array() const {
  if (kind_ != Kind::Arr) throw SimError("json: expected array");
  return *a_;
}
const Object& Value::as_object() const {
  if (kind_ != Kind::Obj) throw SimError("json: expected object");
  return *o_;
}
Array& Value::mutable_array() {
  if (kind_ != Kind::Arr) {
    kind_ = Kind::Arr;
    a_ = std::make_shared<Array>();
  }
  return *a_;
}
Object& Value::mutable_object() {
  if (kind_ != Kind::Obj) {
    kind_ = Kind::Obj;
    o_ = std::make_shared<Object>();
  }
  return *o_;
}

const Value* Value::find(const std::string& key) const {
  if (kind_ != Kind::Obj) return nullptr;
  auto it = o_->find(key);
  return it == o_->end() ? nullptr : &it->second;
}

const Value& Value::require(const std::string& key, const std::string& context) const {
  const Value* v = find(key);
  if (v == nullptr) throw SimError("json: " + context + ": missing required field '" + key + "'");
  return *v;
}

std::int64_t Value::require_int(const std::string& key, const std::string& context) const {
  const Value& v = require(key, context);
  if (!v.is_int()) throw SimError("json: " + context + ": field '" + key + "' must be an integer");
  return v.as_int();
}

const std::string& Value::require_string(const std::string& key, const std::string& context) const {
  const Value& v = require(key, context);
  if (!v.is_string()) throw SimError("json: " + context + ": field '" + key + "' must be a string");
  return v.as_string();
}

const Array& Value::require_array(const std::string& key, const std::string& context) const {
  const Value& v = require(key, context);
  if (!v.is_array()) throw SimError("json: " + context + ": field '" + key + "' must be an array");
  return v.as_array();
}

const Object& Value::require_object(const std::string& key, const std::string& context) const {
  const Value& v = require(key, context);
  if (!v.is_object()) throw SimError("json: " + context + ": field '" + key + "' must be an object");
  return v.as_object();
}

std::int64_t Value::int_or(const std::string& key, std::int64_t fallback) const {
  const Value* v = find(key);
  if (v == nullptr || v->is_null()) return fallback;
  if (!v->is_int()) throw SimError("json: field '" + key + "' must be an integer");
  return v->as_int();
}

bool Value::bool_or(const std::string& key, bool fallback) const {
  const Value* v = find(key);
  if (v == nullptr || v->is_null()) return fallback;
  if (!v->is_bool()) throw SimError("json: field '" + key + "' must be a boolean");
  return v->as_bool();
}

std::string Value::string_or(const std::string& key, const std::string& fallback) const {
  const Value* v = find(key);
  if (v == nullptr || v->is_null()) return fallback;
  if (!v->is_string()) throw SimError("json: field '" + key + "' must be a string");
  return v->as_string();
}

std::int64_t Value::require_decimal(const std::string& key, const std::string& context) const {
  return parse_decimal_string(require_string(key, context));
}

std::uint64_t Value::require_decimal_u(const std::string& key, const std::string& context) const {
  return parse_decimal_string_u(require_string(key, context));
}

void Value::set(const std::string& key, Value v) { mutable_object()[key] = std::move(v); }
void Value::push_back(Value v) { mutable_array().push_back(std::move(v)); }

void Value::reject_unknown_keys(const std::vector<std::string>& allowed, const std::string& context) const {
  if (kind_ != Kind::Obj) throw SimError("json: " + context + ": expected an object");
  for (const auto& [key, unused] : *o_) {
    (void)unused;
    bool ok = false;
    for (const auto& a : allowed) {
      if (a == key) {
        ok = true;
        break;
      }
    }
    if (!ok) throw SimError("json: " + context + ": unknown field '" + key + "'");
  }
}

// ---------------------------------------------------------------------------
// Parser
// ---------------------------------------------------------------------------
namespace {

class Parser {
 public:
  Parser(const std::string& text, const Limits& limits) : s_(text), lim_(limits) {}

  Value run() {
    if (s_.size() > lim_.max_bytes) fail("payload exceeds the maximum accepted size");
    skip_bom();
    skip_ws();
    Value v = parse_value(0);
    skip_ws();
    if (pos_ != s_.size()) fail("trailing content after the top-level value");
    return v;
  }

 private:
  const std::string& s_;
  Limits lim_;
  std::size_t pos_ = 0;

  [[noreturn]] void fail(const std::string& msg) const {
    throw SimError("json: byte " + std::to_string(pos_) + ": " + msg);
  }

  void skip_bom() {
    if (s_.size() >= 3 && static_cast<unsigned char>(s_[0]) == 0xEF &&
        static_cast<unsigned char>(s_[1]) == 0xBB && static_cast<unsigned char>(s_[2]) == 0xBF) {
      pos_ = 3;
    }
  }

  bool eof() const { return pos_ >= s_.size(); }
  char peek() const {
    if (eof()) fail("unexpected end of input");
    return s_[pos_];
  }
  char next() {
    char c = peek();
    ++pos_;
    return c;
  }
  void expect(char c) {
    if (eof() || s_[pos_] != c) fail(std::string("expected '") + c + "'");
    ++pos_;
  }

  void skip_ws() {
    while (!eof()) {
      char c = s_[pos_];
      if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
        ++pos_;
      } else if (c == '/') {
        fail("comments are not accepted in content or save JSON");
      } else {
        return;
      }
    }
  }

  Value parse_value(std::size_t depth) {
    if (depth > lim_.max_depth) fail("nesting depth exceeds the accepted limit");
    skip_ws();
    char c = peek();
    switch (c) {
      case '{': return parse_object(depth);
      case '[': return parse_array(depth);
      case '"': return Value::string(parse_string());
      case 't': literal("true"); return Value::boolean(true);
      case 'f': literal("false"); return Value::boolean(false);
      case 'n': literal("null"); return Value::null();
      default:
        if (c == '-' || (c >= '0' && c <= '9')) return parse_number();
        fail("unexpected character");
    }
  }

  void literal(const char* lit) {
    std::size_t n = std::strlen(lit);
    if (s_.compare(pos_, n, lit) != 0) fail("malformed literal");
    pos_ += n;
  }

  Value parse_object(std::size_t depth) {
    expect('{');
    Object obj;
    skip_ws();
    if (peek() == '}') {
      ++pos_;
      return Value::object(std::move(obj));
    }
    while (true) {
      skip_ws();
      if (peek() != '"') fail("object keys must be strings");
      std::string key = parse_string();
      if (obj.count(key) != 0) fail("duplicate object key '" + key + "'");
      if (obj.size() >= lim_.max_object_keys) fail("too many keys in one object");
      skip_ws();
      expect(':');
      obj.emplace(std::move(key), parse_value(depth + 1));
      skip_ws();
      char c = next();
      if (c == '}') break;
      if (c != ',') fail("expected ',' or '}' in object");
    }
    return Value::object(std::move(obj));
  }

  Value parse_array(std::size_t depth) {
    expect('[');
    Array arr;
    skip_ws();
    if (peek() == ']') {
      ++pos_;
      return Value::array(std::move(arr));
    }
    while (true) {
      if (arr.size() >= lim_.max_array) fail("array length exceeds the accepted limit");
      arr.push_back(parse_value(depth + 1));
      skip_ws();
      char c = next();
      if (c == ']') break;
      if (c != ',') fail("expected ',' or ']' in array");
    }
    return Value::array(std::move(arr));
  }

  Value parse_number() {
    std::size_t start = pos_;
    if (!eof() && s_[pos_] == '-') ++pos_;
    if (eof() || s_[pos_] < '0' || s_[pos_] > '9') fail("malformed number");
    if (s_[pos_] == '0') {
      ++pos_;
      if (!eof() && s_[pos_] >= '0' && s_[pos_] <= '9') fail("leading zeros are not accepted");
    } else {
      while (!eof() && s_[pos_] >= '0' && s_[pos_] <= '9') ++pos_;
    }
    if (!eof() && (s_[pos_] == '.' || s_[pos_] == 'e' || s_[pos_] == 'E')) {
      fail("fractional and exponent numbers are not accepted; author integer milli-units or basis points");
    }
    std::string text = s_.substr(start, pos_ - start);
    return Value::integer(parse_decimal_string(text));
  }

  void append_utf8(std::string& out, std::uint32_t cp) const {
    if (cp <= 0x7F) {
      out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
      out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
      out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
      out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
  }

  std::uint32_t parse_hex4() {
    if (pos_ + 4 > s_.size()) fail("truncated \\u escape");
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i) {
      char c = s_[pos_ + static_cast<std::size_t>(i)];
      v <<= 4;
      if (c >= '0' && c <= '9') {
        v |= static_cast<std::uint32_t>(c - '0');
      } else if (c >= 'a' && c <= 'f') {
        v |= static_cast<std::uint32_t>(c - 'a' + 10);
      } else if (c >= 'A' && c <= 'F') {
        v |= static_cast<std::uint32_t>(c - 'A' + 10);
      } else {
        fail("malformed \\u escape");
      }
    }
    pos_ += 4;
    return v;
  }

  std::string parse_string() {
    expect('"');
    std::string out;
    while (true) {
      if (eof()) fail("unterminated string");
      unsigned char c = static_cast<unsigned char>(s_[pos_]);
      if (c == '"') {
        ++pos_;
        return out;
      }
      if (c == '\\') {
        ++pos_;
        char e = next();
        switch (e) {
          case '"': out.push_back('"'); break;
          case '\\': out.push_back('\\'); break;
          case '/': out.push_back('/'); break;
          case 'b': out.push_back('\b'); break;
          case 'f': out.push_back('\f'); break;
          case 'n': out.push_back('\n'); break;
          case 'r': out.push_back('\r'); break;
          case 't': out.push_back('\t'); break;
          case 'u': {
            std::uint32_t cp = parse_hex4();
            if (cp >= 0xD800 && cp <= 0xDBFF) {
              if (pos_ + 2 > s_.size() || s_[pos_] != '\\' || s_[pos_ + 1] != 'u') {
                fail("unpaired high surrogate");
              }
              pos_ += 2;
              std::uint32_t low = parse_hex4();
              if (low < 0xDC00 || low > 0xDFFF) fail("invalid low surrogate");
              cp = 0x10000u + ((cp - 0xD800u) << 10) + (low - 0xDC00u);
            } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
              fail("unpaired low surrogate");
            }
            append_utf8(out, cp);
            break;
          }
          default: fail("unknown escape sequence");
        }
        continue;
      }
      if (c < 0x20) fail("unescaped control character in string");
      // Validate UTF-8 continuation bytes so a malformed payload is rejected.
      std::size_t extra = 0;
      if (c < 0x80) {
        extra = 0;
      } else if ((c & 0xE0) == 0xC0) {
        extra = 1;
      } else if ((c & 0xF0) == 0xE0) {
        extra = 2;
      } else if ((c & 0xF8) == 0xF0) {
        extra = 3;
      } else {
        fail("invalid UTF-8 lead byte");
      }
      if (pos_ + extra >= s_.size()) fail("truncated UTF-8 sequence");
      for (std::size_t i = 1; i <= extra; ++i) {
        if ((static_cast<unsigned char>(s_[pos_ + i]) & 0xC0) != 0x80) fail("invalid UTF-8 continuation byte");
      }
      out.append(s_, pos_, extra + 1);
      pos_ += extra + 1;
    }
  }
};

void escape_into(const std::string& s, std::string& out) {
  out.push_back('"');
  for (char ch : s) {
    unsigned char c = static_cast<unsigned char>(ch);
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          static const char* kHex = "0123456789abcdef";
          out += "\\u00";
          out.push_back(kHex[(c >> 4) & 0xF]);
          out.push_back(kHex[c & 0xF]);
        } else {
          out.push_back(ch);
        }
    }
  }
  out.push_back('"');
}

void write_canonical(const Value& v, std::string& out) {
  switch (v.kind()) {
    case Kind::Null: out += "null"; break;
    case Kind::Bool: out += v.as_bool() ? "true" : "false"; break;
    case Kind::Int: out += std::to_string(v.as_int()); break;
    case Kind::Str: escape_into(v.as_string(), out); break;
    case Kind::Arr: {
      out.push_back('[');
      bool first = true;
      for (const auto& e : v.as_array()) {
        if (!first) out.push_back(',');
        first = false;
        write_canonical(e, out);
      }
      out.push_back(']');
      break;
    }
    case Kind::Obj: {
      out.push_back('{');
      bool first = true;
      for (const auto& [key, val] : v.as_object()) {  // std::map: bytewise-sorted
        if (!first) out.push_back(',');
        first = false;
        escape_into(key, out);
        out.push_back(':');
        write_canonical(val, out);
      }
      out.push_back('}');
      break;
    }
  }
}

}  // namespace

Value parse(const std::string& text, const Limits& limits) { return Parser(text, limits).run(); }

std::string serialize_canonical(const Value& v) {
  std::string out;
  out.reserve(1024);
  write_canonical(v, out);
  return out;
}

std::string serialize_pretty(const Value& v, int indent) {
  std::string pad(static_cast<std::size_t>(indent) * 2, ' ');
  std::string pad2(static_cast<std::size_t>(indent + 1) * 2, ' ');
  switch (v.kind()) {
    case Kind::Arr: {
      if (v.as_array().empty()) return "[]";
      std::string out = "[\n";
      bool first = true;
      for (const auto& e : v.as_array()) {
        if (!first) out += ",\n";
        first = false;
        out += pad2 + serialize_pretty(e, indent + 1);
      }
      return out + "\n" + pad + "]";
    }
    case Kind::Obj: {
      if (v.as_object().empty()) return "{}";
      std::string out = "{\n";
      bool first = true;
      for (const auto& [key, val] : v.as_object()) {
        if (!first) out += ",\n";
        first = false;
        std::string k;
        escape_into(key, k);
        out += pad2 + k + ": " + serialize_pretty(val, indent + 1);
      }
      return out + "\n" + pad + "}";
    }
    default: return serialize_canonical(v);
  }
}

Value dec(std::int64_t v) { return Value::string(to_decimal_string(v)); }
Value dec_u(std::uint64_t v) { return Value::string(to_decimal_string_u(v)); }

}  // namespace expansion::json
