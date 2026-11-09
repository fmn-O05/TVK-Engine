#pragma once
#include <cstdint>
#include <utility>

namespace tvk {

enum class ErrorCode : uint8_t {
  None = 0,
  InvalidArgument = 1,
  OutOfMemory = 2,
  CapacityExceeded = 3,
  NotFound = 4,
  IoError = 5,
  ParseError = 6,
  PlatformError = 7,
  Unsupported = 8,
  InvalidState = 9
};

template <typename T, typename E = ErrorCode>
class Result {
public:
  static constexpr Result Ok(T value) {
    return Result(std::move(value), E{}, true);
  }

  static constexpr Result Err(E error) {
    return Result(T{}, error, false);
  }

  constexpr bool IsOk() const noexcept { return m_HasValue; }
  constexpr bool IsErr() const noexcept { return !m_HasValue; }
  constexpr explicit operator bool() const noexcept { return m_HasValue; }

  constexpr T &Value() noexcept { return m_Value; }
  constexpr const T &Value() const noexcept { return m_Value; }

  constexpr T ValueOr(T fallback) const {
    return m_HasValue ? m_Value : std::move(fallback);
  }

  constexpr E Error() const noexcept { return m_Error; }

private:
  constexpr Result(T val, E err, bool ok)
      : m_Value(std::move(val)), m_Error(err), m_HasValue(ok) {}

  T m_Value{};
  E m_Error{};
  bool m_HasValue = false;
};

template <typename E>
class Result<void, E> {
public:
  static constexpr Result Ok() noexcept { return Result(E{}, true); }
  static constexpr Result Err(E error) noexcept { return Result(error, false); }

  constexpr bool IsOk() const noexcept { return m_HasValue; }
  constexpr bool IsErr() const noexcept { return !m_HasValue; }
  constexpr explicit operator bool() const noexcept { return m_HasValue; }
  constexpr E Error() const noexcept { return m_Error; }

private:
  constexpr Result(E err, bool ok) noexcept : m_Error(err), m_HasValue(ok) {}

  E m_Error{};
  bool m_HasValue = false;
};

} // namespace tvk
