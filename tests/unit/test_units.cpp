// Explicit overflow and rounding cases (TDD 19.3 E1 evidence).
#include <limits>

#include "expansion/units.hpp"
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
