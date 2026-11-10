#pragma once
#include <cstdarg>
#include <cstddef>
#include <cstdint>

namespace tvk {

enum class LogLevel : uint8_t {
    Trace = 0,
    Debug,
    Info,
    Warn,
    Error,
    Fatal
};

struct LogEntry {
    uint64_t timestampNs = 0u;
    uint64_t frame = 0u;
    LogLevel level = LogLevel::Info;
    char message[256] = {};
};

class Log {
public:
    static constexpr size_t kRingCapacity = 256u;

    /// Formatted log print
    static void Print(LogLevel level, const char* fmt, ...) noexcept;
    static void PrintV(LogLevel level, const char* fmt, va_list args) noexcept;

    /// Convenience helpers
    static void Trace(const char* fmt, ...) noexcept;
    static void Debug(const char* fmt, ...) noexcept;
    static void Info(const char* fmt, ...) noexcept;
    static void Warn(const char* fmt, ...) noexcept;
    static void Error(const char* fmt, ...) noexcept;
    static void Fatal(const char* fmt, ...) noexcept;

    /// Sets the active frame index for timestamp tagging
    static void SetCurrentFrame(uint64_t frame) noexcept;

    /// Retrieves count of valid entries currently in the ring buffer (0..256)
    static size_t GetEntryCount() noexcept;

    /// Retrieves an entry pointer by chronological index (0 = oldest, Count - 1 = newest)
    /// Returns nullptr if index is out of bounds.
    static const LogEntry* GetEntry(size_t index) noexcept;

    /// Clears the ring buffer
    static void Clear() noexcept;

    /// Dumps all active ring-buffer entries to a text file
    static bool DumpToFile(const char* filepath) noexcept;

    /// Controls whether logs echo to stdout/stderr when terminal raw mode is INACTIVE
    static void SetEchoToConsole(bool enable) noexcept;

    /// Formats log level enum to uppercase 5-char string
    static const char* LogLevelToString(LogLevel level) noexcept;
};

} // namespace tvk

// Convenience logging macros
#define TVK_LOG_TRACE(fmt, ...) ::tvk::Log::Print(::tvk::LogLevel::Trace, (fmt), ##__VA_ARGS__)
#define TVK_LOG_DEBUG(fmt, ...) ::tvk::Log::Print(::tvk::LogLevel::Debug, (fmt), ##__VA_ARGS__)
#define TVK_LOG_INFO(fmt, ...)  ::tvk::Log::Print(::tvk::LogLevel::Info,  (fmt), ##__VA_ARGS__)
#define TVK_LOG_WARN(fmt, ...)  ::tvk::Log::Print(::tvk::LogLevel::Warn,  (fmt), ##__VA_ARGS__)
#define TVK_LOG_ERROR(fmt, ...) ::tvk::Log::Print(::tvk::LogLevel::Error, (fmt), ##__VA_ARGS__)
#define TVK_LOG_FATAL(fmt, ...) ::tvk::Log::Print(::tvk::LogLevel::Fatal, (fmt), ##__VA_ARGS__)
