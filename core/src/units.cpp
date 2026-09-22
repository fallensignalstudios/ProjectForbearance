#include "expansion/units.hpp"

#include <cstdlib>

namespace expansion {

void throw_overflow(const char* op) {
  throw SimError(std::string("integer overflow in checked ") + op);
}

namespace {
__extension__ typedef __int128 Wide;

std::int64_t narrow(Wide v, const char* op) {
  if (v > static_cast<Wide>(std::numeric_limits<std::int64_t>::max()) ||
      v < static_cast<Wide>(std::numeric_limits<std::int64_t>::min())) {
    throw_overflow(op);
  }
  return static_cast<std::int64_t>(v);
}
}  // namespace

std::int64_t mul_div_floor(std::int64_t a, std::int64_t b, std::int64_t c) {
  if (c <= 0) throw SimError("mul_div_floor: non-positive divisor");
  Wide num = static_cast<Wide>(a) * static_cast<Wide>(b);
  Wide q = num / c;
  if ((num % c) != 0 && num < 0) --q;  // floor toward negative infinity
  return narrow(q, "mul_div_floor");
}

std::int64_t mul_div_ceil(std::int64_t a, std::int64_t b, std::int64_t c) {
  if (c <= 0) throw SimError("mul_div_ceil: non-positive divisor");
  Wide num = static_cast<Wide>(a) * static_cast<Wide>(b);
  Wide q = num / c;
  if ((num % c) != 0 && num > 0) ++q;  // ceil toward positive infinity
  return narrow(q, "mul_div_ceil");
}

std::int64_t div_floor(std::int64_t n, std::int64_t d) { return mul_div_floor(n, 1, d); }
std::int64_t div_ceil(std::int64_t n, std::int64_t d) { return mul_div_ceil(n, 1, d); }

std::string to_decimal_string(std::int64_t v) { return std::to_string(v); }
std::string to_decimal_string_u(std::uint64_t v) { return std::to_string(v); }

std::int64_t parse_decimal_string(const std::string& s) {
  if (s.empty() || s.size() > 20) throw SimError("parse_decimal_string: bad length: '" + s + "'");
  std::size_t i = 0;
  bool neg = false;
  if (s[0] == '-') {
    neg = true;
    i = 1;
    if (s.size() == 1) throw SimError("parse_decimal_string: lone minus");
  }
  if (s[i] == '0' && s.size() > i + 1) throw SimError("parse_decimal_string: leading zero: '" + s + "'");
  unsigned long long mag = 0;
  for (; i < s.size(); ++i) {
    char c = s[i];
    if (c < '0' || c > '9') throw SimError("parse_decimal_string: not a digit: '" + s + "'");
    unsigned long long next = mag * 10ULL + static_cast<unsigned long long>(c - '0');
    if (next < mag) throw SimError("parse_decimal_string: overflow: '" + s + "'");
    mag = next;
  }
  const unsigned long long kMaxPos = 9223372036854775807ULL;
  if (!neg && mag > kMaxPos) throw SimError("parse_decimal_string: out of range: '" + s + "'");
  if (neg && mag > kMaxPos + 1ULL) throw SimError("parse_decimal_string: out of range: '" + s + "'");
  if (neg) {
    if (mag == kMaxPos + 1ULL) return std::numeric_limits<std::int64_t>::min();
    return -static_cast<std::int64_t>(mag);
  }
  return static_cast<std::int64_t>(mag);
}

std::uint64_t parse_decimal_string_u(const std::string& s) {
  if (s.empty() || s.size() > 20) throw SimError("parse_decimal_string_u: bad length: '" + s + "'");
  if (s[0] == '0' && s.size() > 1) throw SimError("parse_decimal_string_u: leading zero: '" + s + "'");
  unsigned long long mag = 0;
  for (char c : s) {
    if (c < '0' || c > '9') throw SimError("parse_decimal_string_u: not a digit: '" + s + "'");
    unsigned long long next = mag * 10ULL + static_cast<unsigned long long>(c - '0');
    if (next < mag) throw SimError("parse_decimal_string_u: overflow: '" + s + "'");
    mag = next;
  }
  return static_cast<std::uint64_t>(mag);
}

std::string format_milli(std::int64_t v) {
  bool neg = v < 0;
  unsigned long long mag = neg ? (0ULL - static_cast<unsigned long long>(v)) : static_cast<unsigned long long>(v);
  unsigned long long whole = mag / 1000ULL;
  unsigned long long frac = mag % 1000ULL;
  std::string out = (neg ? "-" : "") + std::to_string(whole);
  if (frac != 0) {
    std::string f = std::to_string(frac);
    while (f.size() < 3) f = "0" + f;
    while (!f.empty() && f.back() == '0') f.pop_back();
    out += "." + f;
  }
  return out;
}

std::string format_bp_percent(Bp v) {
  bool neg = v < 0;
  unsigned long long mag = neg ? (0ULL - static_cast<unsigned long long>(v)) : static_cast<unsigned long long>(v);
  unsigned long long whole = mag / 100ULL;
  unsigned long long frac = mag % 100ULL;
  std::string out = (neg ? "-" : "") + std::to_string(whole);
  if (frac != 0) {
    std::string f = std::to_string(frac);
    while (f.size() < 2) f = "0" + f;
    while (!f.empty() && f.back() == '0') f.pop_back();
    out += "." + f;
  }
  return out + "%";
}

}  // namespace expansion
