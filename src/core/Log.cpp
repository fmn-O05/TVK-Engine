#include "tvk/core/Log.h"
#include <cstdio>
#include <cstring>
#include <mutex>
#include "tvk/platform/PAL.h"

namespace tvk {

namespace {

struct LogRingBuffer {
    std::mutex mutex;
    LogEntry entries[Log::kRingCapacity];
    size_t head = 0u;       // Points to next slot to write
    size_t count = 0u;      // Active entries (max kRingCapacity)
    uint64_t currentFrame = 0u;
    bool echoToConsole = true;
};

LogRingBuffer& GetRing() noexcept {
    static LogRingBuffer s_Ring;
    return s_Ring;
}

} // namespace

const char* Log::LogLevelToString(LogLevel level) noexcept {
    switch (level) {
    case LogLevel::Trace: return "TRACE";
    case LogLevel::Debug: return "DEBUG";
    case LogLevel::Info:  return "INFO ";
    case LogLevel::Warn:  return "WARN ";
    case LogLevel::Error: return "ERROR";
    case LogLevel::Fatal: return "FATAL";
    default:              return "UNKN ";
    }
}

void Log::SetCurrentFrame(uint64_t frame) noexcept {
    LogRingBuffer& ring = GetRing();
    std::lock_guard<std::mutex> lock(ring.mutex);
    ring.currentFrame = frame;
}

void Log::SetEchoToConsole(bool enable) noexcept {
    LogRingBuffer& ring = GetRing();
    std::lock_guard<std::mutex> lock(ring.mutex);
    ring.echoToConsole = enable;
}

void Log::Clear() noexcept {
    LogRingBuffer& ring = GetRing();
    std::lock_guard<std::mutex> lock(ring.mutex);
    ring.head = 0u;
    ring.count = 0u;
}

size_t Log::GetEntryCount() noexcept {
    LogRingBuffer& ring = GetRing();
    std::lock_guard<std::mutex> lock(ring.mutex);
    return ring.count;
}

const LogEntry* Log::GetEntry(size_t index) noexcept {
    LogRingBuffer& ring = GetRing();
    std::lock_guard<std::mutex> lock(ring.mutex);

    if (index >= ring.count) {
        return nullptr;
    }

    size_t startIdx = (ring.head + kRingCapacity - ring.count) % kRingCapacity;
    size_t slot = (startIdx + index) % kRingCapacity;
    return &ring.entries[slot];
}

void Log::PrintV(LogLevel level, const char* fmt, va_list args) noexcept {
    if (!fmt) return;

    LogRingBuffer& ring = GetRing();
    std::lock_guard<std::mutex> lock(ring.mutex);

    LogEntry& entry = ring.entries[ring.head];
    entry.timestampNs = pal::GetTimestampNs();
    entry.frame = ring.currentFrame;
    entry.level = level;

    // Safe formatting with guaranteed null-termination
    vsnprintf(entry.message, sizeof(entry.message), fmt, args);
    entry.message[sizeof(entry.message) - 1] = '\0';

    // Echo to stdout/stderr ONLY if raw terminal mode is NOT active
    if (ring.echoToConsole && !pal::IsTerminalSetup()) {
        FILE* out = (level >= LogLevel::Error) ? stderr : stdout;
        std::fprintf(out, "[%s] [F:%lu] %s\n", LogLevelToString(level),
                     static_cast<unsigned long>(entry.frame), entry.message);
        std::fflush(out);
    }

    ring.head = (ring.head + 1u) % kRingCapacity;
    if (ring.count < kRingCapacity) {
        ++ring.count;
    }
}

void Log::Print(LogLevel level, const char* fmt, ...) noexcept {
    va_list args;
    va_start(args, fmt);
    PrintV(level, fmt, args);
    va_end(args);
}

void Log::Trace(const char* fmt, ...) noexcept {
    va_list args;
    va_start(args, fmt);
    PrintV(LogLevel::Trace, fmt, args);
    va_end(args);
}

void Log::Debug(const char* fmt, ...) noexcept {
    va_list args;
    va_start(args, fmt);
    PrintV(LogLevel::Debug, fmt, args);
    va_end(args);
}

void Log::Info(const char* fmt, ...) noexcept {
    va_list args;
    va_start(args, fmt);
    PrintV(LogLevel::Info, fmt, args);
    va_end(args);
}

void Log::Warn(const char* fmt, ...) noexcept {
    va_list args;
    va_start(args, fmt);
    PrintV(LogLevel::Warn, fmt, args);
    va_end(args);
}

void Log::Error(const char* fmt, ...) noexcept {
    va_list args;
    va_start(args, fmt);
    PrintV(LogLevel::Error, fmt, args);
    va_end(args);
}

void Log::Fatal(const char* fmt, ...) noexcept {
    va_list args;
    va_start(args, fmt);
    PrintV(LogLevel::Fatal, fmt, args);
    va_end(args);
}

bool Log::DumpToFile(const char* filepath) noexcept {
    if (!filepath) return false;

    FILE* fp = std::fopen(filepath, "w");
    if (!fp) return false;

    LogRingBuffer& ring = GetRing();
    std::lock_guard<std::mutex> lock(ring.mutex);

    size_t startIdx = (ring.head + kRingCapacity - ring.count) % kRingCapacity;
    for (size_t i = 0; i < ring.count; ++i) {
        size_t slot = (startIdx + i) % kRingCapacity;
        const LogEntry& e = ring.entries[slot];
        const double sec = static_cast<double>(e.timestampNs) * 1e-9;
        std::fprintf(fp, "[%12.6f] [F:%06lu] [%s] %s\n",
                     sec, static_cast<unsigned long>(e.frame),
                     LogLevelToString(e.level), e.message);
    }

    std::fclose(fp);
    return true;
}

} // namespace tvk
