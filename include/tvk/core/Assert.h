#pragma once
#include <cstdio>
#include <cstdlib>
#include "tvk/platform/PAL.h"

namespace tvk {
namespace detail {

[[noreturn]] inline void AssertionFailed(const char *expr, const char *msg,
                                         const char *file, int line) noexcept {
  tvk::pal::RestoreTerminal();
  std::fprintf(stderr, "[TVK ASSERTION FAILED] (%s) at %s:%d — %s\n", expr,
               file, line, msg ? msg : "");
  std::fflush(stderr);
  std::abort();
}

} // namespace detail
} // namespace tvk

#define TVK_VERIFY(expr, msg)                                                  \
  do {                                                                         \
    if (!(expr)) {                                                             \
      ::tvk::detail::AssertionFailed(#expr, (msg), __FILE__, __LINE__);        \
    }                                                                          \
  } while (false)

#ifndef NDEBUG
#define TVK_ASSERT(expr, msg) TVK_VERIFY(expr, msg)
#else
#define TVK_ASSERT(expr, msg)                                                  \
  do {                                                                         \
    (void)sizeof(expr);                                                        \
    (void)sizeof(msg);                                                         \
  } while (false)
#endif
