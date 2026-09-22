// A minimal in-repo test harness. Core unit tests stay in the headless build
// and depend on nothing outside it (TDD 18.4).
#pragma once

#include <functional>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

#include "expansion/catalog.hpp"
#include "expansion/session.hpp"

namespace testing {

struct TestCase {
  std::string id;
  std::string name;
  std::function<void()> fn;
};

std::vector<TestCase>& registry();

struct Registrar {
  Registrar(const char* id, const char* name, std::function<void()> fn);
};

class Failure : public std::runtime_error {
 public:
  explicit Failure(const std::string& what) : std::runtime_error(what) {}
};

void fail(const std::string& what, const char* file, int line);

inline void check(bool condition, const std::string& what, const char* file, int line) {
  if (!condition) fail(what, file, line);
}

// Renders enumerations as their underlying value so any state enum can be
// compared without a bespoke streamer.
template <typename T>
std::string describe(const T& v) {
  std::ostringstream ss;
  if constexpr (std::is_enum_v<T>) {
    ss << static_cast<std::int64_t>(v);
  } else if constexpr (requires(std::ostream& os, const T& x) { os << x; }) {
    ss << v;
  } else {
    ss << "<value>";
  }
  return ss.str();
}

template <typename A, typename B>
void check_eq(const A& a, const B& b, const std::string& what, const char* file, int line) {
  if (!(a == b)) {
    std::ostringstream ss;
    ss << what << "\n      left  = " << describe(a) << "\n      right = " << describe(b);
    fail(ss.str(), file, line);
  }
}

// Shared helpers for building sessions in tests.
const expansion::Catalog& shipped_catalog();
// Returns a pointer: a reference bound to a call taking a temporary string
// trips the compiler's dangling-reference heuristic at every call site.
const expansion::Catalog* fixture_catalog(const char* name);
std::unique_ptr<expansion::Session> new_session(const expansion::Catalog& catalog, const std::string& scenario,
                                                const std::string& faction = "neutral_calibration");
expansion::InstanceId find_facility(const expansion::Session& session, const std::string& planet,
                                    const std::string& recipe_id);
expansion::Milli stock(const expansion::Session& session, const std::string& planet, const char* resource);

}  // namespace testing

#define TEST(test_id, test_name)                                                          \
  static void test_id##_body();                                                           \
  static ::testing::Registrar test_id##_registrar(#test_id, test_name, test_id##_body);   \
  static void test_id##_body()

#define CHECK(cond) ::testing::check((cond), #cond, __FILE__, __LINE__)
#define CHECK_MSG(cond, msg) ::testing::check((cond), std::string(#cond) + ": " + (msg), __FILE__, __LINE__)
#define CHECK_EQ(a, b) ::testing::check_eq((a), (b), #a " == " #b, __FILE__, __LINE__)
#define CHECK_THROWS(expr)                                    \
  do {                                                        \
    bool threw_ = false;                                      \
    try {                                                     \
      expr;                                                   \
    } catch (const expansion::SimError&) {                    \
      threw_ = true;                                          \
    }                                                         \
    ::testing::check(threw_, "expected " #expr " to be rejected", __FILE__, __LINE__); \
  } while (0)
