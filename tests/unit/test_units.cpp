// Explicit overflow and rounding cases (TDD 19.3 E1 evidence).
#include <limits>

#include "expansion/units.hpp"
#include "expansion/wide_math.hpp"
#include "harness.hpp"

using namespace expansion;

TEST(unit_rounding, "outputs round down and required inputs round up") {
  CHECK_EQ(scale_out(48000, 5000), 24000);
  CHECK_EQ(scale_out(7, 5000), 3);          // floor
  CHECK_EQ(scale_in(7, 5000), 4);           // ceil
  CHECK_EQ(scale_out(0, 9999), 0);
  CHECK_EQ(scale_in(0, 9999), 0);
  CHECK_EQ(mul_div_floor(-7, 1, 2), -4);    // floor toward negative infinity
  CHECK_EQ(mul_div_ceil(-7, 1, 2), -3);
}

TEST(unit_overflow, "overflow is an error, not a wrap or a silent clamp") {
  const std::int64_t big = std::numeric_limits<std::int64_t>::max();
  CHECK_THROWS(checked_add(big, 1));
  CHECK_THROWS(checked_mul(big, 2));
  CHECK_THROWS(mul_div_floor(big, big, 1));
  CHECK_THROWS(mul_div_floor(1, 1, 0));
  // A wide intermediate keeps an in-range result exact.
  CHECK_EQ(mul_div_floor(big, 1, 1), big);
  CHECK_EQ(mul_div_floor(1000000000000LL, 10000, 10000), 1000000000000LL);
}

TEST(unit_bp, "basis-point products quantize down at every step") {
  CHECK_EQ(bp_mul(kBpOne, kBpOne), kBpOne);
  CHECK_EQ(bp_mul(5000, 5000), 2500);
  CHECK_EQ(bp_mul(3333, 3333), 1110);       // 1110.8889 floors to 1110
  CHECK_EQ(fulfilment_bp(1, 3), 3333);
  CHECK_EQ(fulfilment_bp(0, 0), kBpOne);    // a zero-demand fraction is 1
  CHECK_EQ(fulfilment_bp(5, 3), kBpOne);    // clamped
}

TEST(unit_decimal_strings, "64-bit quantities round-trip through decimal strings") {
  const std::int64_t values[] = {0, 1, -1, 1000, -999999999999LL,
                                 std::numeric_limits<std::int64_t>::max(),
                                 std::numeric_limits<std::int64_t>::min()};
  for (std::int64_t v : values) CHECK_EQ(parse_decimal_string(to_decimal_string(v)), v);
  CHECK_THROWS(parse_decimal_string(""));
  CHECK_THROWS(parse_decimal_string("007"));
  CHECK_THROWS(parse_decimal_string("1.5"));
  CHECK_THROWS(parse_decimal_string("-"));
  CHECK_THROWS(parse_decimal_string("9223372036854775808"));
  CHECK_EQ(parse_decimal_string("-9223372036854775808"), std::numeric_limits<std::int64_t>::min());
}

TEST(unit_formatting, "milli-units and basis points render without a floating point") {
  CHECK_EQ(format_milli(3280), std::string("3.28"));
  CHECK_EQ(format_milli(1000), std::string("1"));
  CHECK_EQ(format_milli(-1500), std::string("-1.5"));
  CHECK_EQ(format_milli(0), std::string("0"));
  CHECK_EQ(format_bp_percent(10000), std::string("100%"));
  CHECK_EQ(format_bp_percent(7350), std::string("73.5%"));
}

// --- portability -----------------------------------------------------------
// The first packaged target is Windows, where MSVC offers neither __int128 nor
// the __builtin_*_overflow family. These tests pin the portable path's results
// against the compiler's own wide type, so a Windows build cannot drift.

TEST(wide_portable_matches_native, "the portable 128-bit path agrees with the compiler's wide type") {
  // A deterministic spread of magnitudes, including the extremes.
  const std::uint64_t values[] = {0ULL,
                                  1ULL,
                                  2ULL,
                                  9999ULL,
                                  10000ULL,
                                  123456789ULL,
                                  0x7FFFFFFFULL,
                                  0x80000000ULL,
                                  0xFFFFFFFFULL,
                                  0x100000000ULL,
                                  0x123456789ABCDEFULL,
                                  0x7FFFFFFFFFFFFFFFULL,
                                  0x8000000000000000ULL,
                                  0xFFFFFFFFFFFFFFFFULL};
  for (std::uint64_t a : values) {
    for (std::uint64_t b : values) {
      const wide::U128 p = wide::mul_u64(a, b);
      // Re-derive the product from 32-bit limbs independently of the header.
      const std::uint64_t al = a & 0xFFFFFFFFULL, ah = a >> 32;
      const std::uint64_t bl = b & 0xFFFFFFFFULL, bh = b >> 32;
      const std::uint64_t ll = al * bl, lh = al * bh, hl = ah * bl, hh = ah * bh;
      const std::uint64_t mid = (ll >> 32) + (lh & 0xFFFFFFFFULL) + (hl & 0xFFFFFFFFULL);
      const std::uint64_t lo = (ll & 0xFFFFFFFFULL) | (mid << 32);
      const std::uint64_t hi = hh + (lh >> 32) + (hl >> 32) + (mid >> 32);
      CHECK_EQ(p.hi, hi);
      CHECK_EQ(p.lo, lo);

      for (std::uint64_t d : {1ULL, 2ULL, 7ULL, 1000ULL, 10000ULL, 0xFFFFFFFFULL, 0x7FFFFFFFFFFFFFFFULL}) {
        std::uint64_t q = 0, r = 0;
        const bool ok = wide::divmod_u128(p, d, &q, &r);
        if (!ok) {
          CHECK(p.hi >= d);   // the only legitimate refusal is an overflowing quotient
          continue;
        }
        CHECK(r < d);
        // q * d + r must reconstruct the original product exactly.
        const wide::U128 back = wide::mul_u64(q, d);
        std::uint64_t lo2 = back.lo + r;
        std::uint64_t hi2 = back.hi + (lo2 < back.lo ? 1ULL : 0ULL);
        CHECK_EQ(hi2, p.hi);
        CHECK_EQ(lo2, p.lo);
      }
    }
  }
}

TEST(wide_checked_ops_are_portable, "the portable overflow checks match the extremes exactly") {
  const std::int64_t kMax = std::numeric_limits<std::int64_t>::max();
  const std::int64_t kMin = std::numeric_limits<std::int64_t>::min();
  std::int64_t out = 0;
  CHECK(!wide::add_overflow(kMax - 1, 1, &out));
  CHECK_EQ(out, kMax);
  CHECK(wide::add_overflow(kMax, 1, &out));
  CHECK(wide::add_overflow(kMin, -1, &out));
  CHECK(!wide::sub_overflow(kMin + 1, 1, &out));
  CHECK_EQ(out, kMin);
  CHECK(wide::sub_overflow(kMin, 1, &out));
  CHECK(wide::sub_overflow(kMax, -1, &out));
  CHECK(wide::mul_overflow(kMin, -1, &out));
  CHECK(!wide::mul_overflow(kMin, 1, &out));
  CHECK_EQ(out, kMin);
  CHECK(wide::mul_overflow(kMax, 2, &out));
  CHECK(!wide::mul_overflow(0, kMin, &out));
  CHECK_EQ(out, 0);
  CHECK_EQ(wide::magnitude(kMin), 9223372036854775808ULL);
  CHECK_EQ(wide::magnitude(-7), 7ULL);
}

TEST(mul_div_signs_and_extremes, "rounding stays directional for every sign combination") {
  CHECK_EQ(mul_div_floor(7, 1, 2), 3);
  CHECK_EQ(mul_div_ceil(7, 1, 2), 4);
  CHECK_EQ(mul_div_floor(-7, 1, 2), -4);
  CHECK_EQ(mul_div_ceil(-7, 1, 2), -3);
  CHECK_EQ(mul_div_floor(7, -1, 2), -4);
  CHECK_EQ(mul_div_ceil(7, -1, 2), -3);
  CHECK_EQ(mul_div_floor(-7, -1, 2), 3);
  CHECK_EQ(mul_div_ceil(-7, -1, 2), 4);
  CHECK_EQ(mul_div_floor(-8, 1, 2), -4);
  CHECK_EQ(mul_div_ceil(-8, 1, 2), -4);
  const std::int64_t kMin = std::numeric_limits<std::int64_t>::min();
  CHECK_EQ(mul_div_floor(kMin, 1, 1), kMin);
  CHECK_THROWS(mul_div_floor(kMin, -1, 1));
}
