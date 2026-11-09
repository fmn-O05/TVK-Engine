#pragma once
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace tvk {
namespace test {

struct TestCase {
  const char *name;
  void (*fn)();
};

inline std::vector<TestCase> &Registry() {
  static std::vector<TestCase> s_Tests;
  return s_Tests;
}

inline int &FailureCount() {
  static int s_Failures = 0;
  return s_Failures;
}

inline int &CurrentTestFailures() {
  static int s_Current = 0;
  return s_Current;
}

struct Registrar {
  Registrar(const char *name, void (*fn)()) {
    Registry().push_back({name, fn});
  }
};

inline int RunAllTests() {
  int passed = 0;
  int failed = 0;
  std::printf("============================================================\n");
  std::printf("Running %zu TVK test cases...\n", Registry().size());
  std::printf("============================================================\n");

  for (const auto &tc : Registry()) {
    CurrentTestFailures() = 0;
    tc.fn();
    if (CurrentTestFailures() == 0) {
      std::printf("  [ PASS ] %s\n", tc.name);
      ++passed;
    } else {
      std::printf("  [ FAIL ] %s (%d assertion failures)\n", tc.name,
                  CurrentTestFailures());
      ++failed;
    }
  }

  std::printf("============================================================\n");
  std::printf("Summary: %d passed, %d failed (total assertion failures: %d)\n",
              passed, failed, FailureCount());
  std::printf("============================================================\n");
  return (failed == 0 && FailureCount() == 0) ? 0 : 1;
}

} // namespace test
} // namespace tvk

#define TVK_TEST(name)                                                         \
  static void tvk_test_##name();                                               \
  static ::tvk::test::Registrar tvk_reg_##name(#name, &tvk_test_##name);       \
  static void tvk_test_##name()

#define TVK_EXPECT_TRUE(expr)                                                  \
  do {                                                                         \
    if (!(expr)) {                                                             \
      std::fprintf(stderr, "    EXPECT_TRUE failed at %s:%d: %s\n", __FILE__,  \
                   __LINE__, #expr);                                           \
      ++::tvk::test::FailureCount();                                           \
      ++::tvk::test::CurrentTestFailures();                                    \
    }                                                                          \
  } while (false)

#define TVK_EXPECT_FALSE(expr) TVK_EXPECT_TRUE(!(expr))

#define TVK_EXPECT_EQ(a, b)                                                    \
  do {                                                                         \
    const auto _a = (a);                                                       \
    const auto _b = (b);                                                       \
    if (!(_a == _b)) {                                                         \
      std::fprintf(stderr, "    EXPECT_EQ failed at %s:%d: %s != %s\n",        \
                   __FILE__, __LINE__, #a, #b);                                \
      ++::tvk::test::FailureCount();                                           \
      ++::tvk::test::CurrentTestFailures();                                    \
    }                                                                          \
  } while (false)

#define TVK_EXPECT_NEAR(a, b, eps)                                             \
  do {                                                                         \
    const double _a = static_cast<double>(a);                                  \
    const double _b = static_cast<double>(b);                                  \
    const double _e = static_cast<double>(eps);                                \
    if (std::isnan(_a) || std::isnan(_b) || std::abs(_a - _b) > _e) {          \
      std::fprintf(stderr,                                                     \
                   "    EXPECT_NEAR failed at %s:%d: %s (%f) vs %s (%f), "     \
                   "eps=%f\n",                                                 \
                   __FILE__, __LINE__, #a, _a, #b, _b, _e);                    \
      ++::tvk::test::FailureCount();                                           \
      ++::tvk::test::CurrentTestFailures();                                    \
    }                                                                          \
  } while (false)

#define TVK_RUN_ALL_TESTS() ::tvk::test::RunAllTests()
