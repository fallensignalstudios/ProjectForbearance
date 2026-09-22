// Portable 128-bit intermediate arithmetic.
//
// The economy multiplies 64-bit milli-units by basis points and divides by
// 10,000, so every such step needs a 128-bit intermediate to stay exact. The
// first implementation used `__int128` and `__builtin_*_overflow`, which are
// GCC and Clang extensions. MSVC has neither, and MSVC is the compiler for the
// Windows target the design names first, so the core would not have compiled
// for it at all.
//
// This header provides one portable definition plus faster paths where the
// compiler offers them. Define EXPANSION_FORCE_PORTABLE_MATH to compile only the
// portable path; the test suite runs both and requires identical results.
#pragma once

#include <cstdint>
#include <limits>

#if !defined(EXPANSION_FORCE_PORTABLE_MATH)
#if defined(__SIZEOF_INT128__)
#define EXPANSION_HAS_INT128 1
// `__extension__` keeps the 128-bit type from tripping -Wpedantic, which the
// project builds with as an error.
__extension__ typedef unsigned __int128 EXPANSION_U128_T;
#define EXPANSION_U128 EXPANSION_U128_T
#elif defined(_MSC_VER) && (defined(_M_X64) || defined(_M_ARM64))
#define EXPANSION_HAS_MSVC_INTRIN 1
#include <intrin.h>
#endif
#endif

namespace expansion::wide {

// An unsigned 128-bit value as two 64-bit halves.
struct U128 {
  std::uint64_t hi = 0;
  std::uint64_t lo = 0;
};

// Full 64x64 -> 128 product. Exact on every platform.
inline U128 mul_u64(std::uint64_t a, std::uint64_t b) {
#if defined(EXPANSION_HAS_INT128)
  EXPANSION_U128 p = static_cast<EXPANSION_U128>(a) * b;
  return U128{static_cast<std::uint64_t>(p >> 64), static_cast<std::uint64_t>(p)};
#elif defined(EXPANSION_HAS_MSVC_INTRIN)
  std::uint64_t hi = 0;
  std::uint64_t lo = _umul128(a, b, &hi);
  return U128{hi, lo};
#else
  // Four 32-bit limb products, carried by hand.
  const std::uint64_t a_lo = a & 0xFFFFFFFFULL, a_hi = a >> 32;
  const std::uint64_t b_lo = b & 0xFFFFFFFFULL, b_hi = b >> 32;
  const std::uint64_t ll = a_lo * b_lo;
  const std::uint64_t lh = a_lo * b_hi;
  const std::uint64_t hl = a_hi * b_lo;
  const std::uint64_t hh = a_hi * b_hi;
  const std::uint64_t mid = (ll >> 32) + (lh & 0xFFFFFFFFULL) + (hl & 0xFFFFFFFFULL);
  const std::uint64_t lo = (ll & 0xFFFFFFFFULL) | (mid << 32);
  const std::uint64_t hi = hh + (lh >> 32) + (hl >> 32) + (mid >> 32);
  return U128{hi, lo};
#endif
}

// Divides a 128-bit value by a 64-bit divisor. Returns false when the quotient
// does not fit in 64 bits, or when `d` is zero, and writes nothing in that case.
inline bool divmod_u128(U128 n, std::uint64_t d, std::uint64_t* quotient, std::uint64_t* remainder) {
  if (d == 0) return false;
  if (n.hi >= d) return false;  // the quotient would exceed 64 bits
#if defined(EXPANSION_HAS_INT128)
  EXPANSION_U128 v = (static_cast<EXPANSION_U128>(n.hi) << 64) | n.lo;
  *quotient = static_cast<std::uint64_t>(v / d);
  *remainder = static_cast<std::uint64_t>(v % d);
  return true;
#elif defined(EXPANSION_HAS_MSVC_INTRIN)
  std::uint64_t rem = 0;
  *quotient = _udiv128(n.hi, n.lo, d, &rem);
  *remainder = rem;
  return true;
#else
  // Shift-and-subtract long division. Exact, and only used where the compiler
  // offers no wide type of its own.
  std::uint64_t q = 0;
  std::uint64_t r = n.hi;
  for (int bit = 63; bit >= 0; --bit) {
    const std::uint64_t carry = r >> 63;
    r = (r << 1) | ((n.lo >> bit) & 1ULL);
    q <<= 1;
    if (carry != 0 || r >= d) {
      r -= d;
      q |= 1ULL;
    }
  }
  *quotient = q;
  *remainder = r;
  return true;
#endif
}

// --- checked 64-bit operations, without compiler builtins -------------------

inline bool add_overflow(std::int64_t a, std::int64_t b, std::int64_t* out) {
  constexpr std::int64_t kMax = std::numeric_limits<std::int64_t>::max();
  constexpr std::int64_t kMin = std::numeric_limits<std::int64_t>::min();
  if (b > 0 && a > kMax - b) return true;
  if (b < 0 && a < kMin - b) return true;
  *out = a + b;
  return false;
}

inline bool sub_overflow(std::int64_t a, std::int64_t b, std::int64_t* out) {
  constexpr std::int64_t kMax = std::numeric_limits<std::int64_t>::max();
  constexpr std::int64_t kMin = std::numeric_limits<std::int64_t>::min();
  if (b < 0 && a > kMax + b) return true;
  if (b > 0 && a < kMin + b) return true;
  *out = a - b;
  return false;
}

inline bool mul_overflow(std::int64_t a, std::int64_t b, std::int64_t* out) {
  constexpr std::int64_t kMax = std::numeric_limits<std::int64_t>::max();
  constexpr std::int64_t kMin = std::numeric_limits<std::int64_t>::min();
  if (a == 0 || b == 0) {
    *out = 0;
    return false;
  }
  if (a == -1) {
    if (b == kMin) return true;
    *out = -b;
    return false;
  }
  if (b == -1) {
    if (a == kMin) return true;
    *out = -a;
    return false;
  }
  if (a > 0) {
    if (b > 0) {
      if (a > kMax / b) return true;
    } else {
      if (b < kMin / a) return true;
    }
  } else {
    if (b > 0) {
      if (a < kMin / b) return true;
    } else {
      if (a < kMax / b) return true;
    }
  }
  *out = a * b;
  return false;
}

// Magnitude of a signed value as an unsigned, correct for the minimum value.
inline std::uint64_t magnitude(std::int64_t v) {
  return v < 0 ? (0ULL - static_cast<std::uint64_t>(v)) : static_cast<std::uint64_t>(v);
}

}  // namespace expansion::wide
