// Sovereign Call: Expansion -- fixed-point units and checked integer arithmetic.
// TDD Section 4.1. All economic arithmetic in this project is integer arithmetic.
#pragma once

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

#include "expansion/wide_math.hpp"

namespace expansion {

// A quantity of a stored commodity, in milli-units. 1 displayed unit == 1000 internal units.
using Milli = std::int64_t;
// A ratio in basis points. 10000 bp == 100%.
using Bp = std::int64_t;
// Power capacity, in power-unit milli-units. Never a stored commodity (TDD 6).
using PowerMilli = std::int64_t;
// Work, in milli person-days.
using WorkMilli = std::int64_t;

constexpr Milli kMilliOne = 1000;
constexpr Bp kBpOne = 10000;
constexpr Bp kThroughputCapBp = 20000;  // TDD 8.2

using Day = std::int64_t;
using Revision = std::uint64_t;
using InstanceId = std::uint64_t;
using People = std::int64_t;

// What went wrong, in terms a host can branch on without reading the message.
enum class ErrorCode {
  Ok,
  InvalidContent,     // a definition file failed to load or validate
  InvalidArgument,    // a parameter to a public operation was not usable
  NotFound,           // a named scenario, faction or instance does not exist
  IncompatibleSave,   // the save was written by another simulation or catalog
  CorruptSave,        // the payload is truncated, mistyped or fails its checksum
  Overflow,           // checked arithmetic could not represent a result
  InvariantViolated,  // the state would have left a bound the design fixes
  Internal,           // anything the core did not classify
};
const char* error_code_id(ErrorCode code);

// Raised for any arithmetic or invariant violation inside the authoritative
// core. Internal only: every public operation in api.hpp catches these and
// returns typed error information instead, because the core must not throw
// through a host adapter (TDD 19.2). Unreal disables exceptions in many module
// configurations, so this boundary matters in practice, not just on paper.
class SimError : public std::runtime_error {
 public:
  explicit SimError(const std::string& what) : std::runtime_error(what), code_(ErrorCode::Internal) {}
  SimError(ErrorCode code, const std::string& what) : std::runtime_error(what), code_(code) {}
  ErrorCode code() const { return code_; }

 private:
  ErrorCode code_;
};

[[noreturn]] void throw_overflow(const char* op);

// ---------------------------------------------------------------------------
// Checked addition / subtraction / multiplication. Overflow is an error, never
// a wrap and never a silent clamp (TDD 4.1).
// ---------------------------------------------------------------------------

inline std::int64_t checked_add(std::int64_t a, std::int64_t b) {
  std::int64_t out = 0;
  if (wide::add_overflow(a, b, &out)) throw_overflow("add");
  return out;
}

inline std::int64_t checked_sub(std::int64_t a, std::int64_t b) {
  std::int64_t out = 0;
  if (wide::sub_overflow(a, b, &out)) throw_overflow("sub");
  return out;
}

inline std::int64_t checked_mul(std::int64_t a, std::int64_t b) {
  std::int64_t out = 0;
  if (wide::mul_overflow(a, b, &out)) throw_overflow("mul");
  return out;
}

// floor(a * b / c) using a wide intermediate. Exact for all int64 inputs whose
// result fits in int64. c must be positive.
std::int64_t mul_div_floor(std::int64_t a, std::int64_t b, std::int64_t c);
// ceil(a * b / c) using a wide intermediate. c must be positive.
std::int64_t mul_div_ceil(std::int64_t a, std::int64_t b, std::int64_t c);

// floor / ceil division for possibly-negative numerators. d must be positive.
std::int64_t div_floor(std::int64_t n, std::int64_t d);
std::int64_t div_ceil(std::int64_t n, std::int64_t d);

// Scale a nonnegative quantity by a basis-point ratio. Output rounds down,
// required inputs round up (TDD 4.1).
inline std::int64_t scale_out(std::int64_t qty, Bp ratio) { return mul_div_floor(qty, ratio, kBpOne); }
inline std::int64_t scale_in(std::int64_t qty, Bp ratio) { return mul_div_ceil(qty, ratio, kBpOne); }

// Multiply two basis-point ratios, quantizing the result down to one basis point.
// Each multiplicative step is quantized separately and in a fixed order (TDD 8.2).
inline Bp bp_mul(Bp a, Bp b) { return mul_div_floor(a, b, kBpOne); }

inline std::int64_t clamp_i64(std::int64_t v, std::int64_t lo, std::int64_t hi) {
  if (lo > hi) throw SimError("clamp_i64: inverted bounds");
  return v < lo ? lo : (v > hi ? hi : v);
}

inline Bp clamp_bp(Bp v) { return clamp_i64(v, 0, kBpOne); }

// A fulfilment fraction as integer basis points: floor(10000 * fulfilled / required).
// A zero-demand fraction is 1 (TDD 7.2).
inline Bp fulfilment_bp(std::int64_t fulfilled, std::int64_t required) {
  if (required <= 0) return kBpOne;
  if (fulfilled <= 0) return 0;
  return clamp_bp(mul_div_floor(fulfilled, kBpOne, required));
}

// Decimal rendering of a milli-unit quantity, e.g. 3280 -> "3.28", 1000 -> "1".
std::string format_milli(std::int64_t v);
// Decimal rendering of basis points as a percentage, e.g. 7350 -> "73.5%".
std::string format_bp_percent(Bp v);
// Canonical decimal string for a 64-bit value (used by the save codec, TDD 4.1).
std::string to_decimal_string(std::int64_t v);
std::string to_decimal_string_u(std::uint64_t v);
// Strict parse of a canonical decimal string. Rejects empty, overlong, and
// non-numeric input rather than clamping.
std::int64_t parse_decimal_string(const std::string& s);
std::uint64_t parse_decimal_string_u(const std::string& s);

}  // namespace expansion
