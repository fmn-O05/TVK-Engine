#pragma once
#include <cstdint>

namespace tvk {
namespace pal {

/// Query current terminal dimensions via OS APIs or COLUMNS/LINES env vars.
bool TryGetTerminalSize(int &out_w, int &out_h);

/// Enable ANSI/VT processing, alternate screen buffer, hide cursor.
/// Returns false if the terminal does not support VT sequences.
bool SetupTerminal();

/// Restore terminal to default state (show cursor, leave alternate screen).
void RestoreTerminal();

/// Write a raw byte buffer to stdout as efficiently as possible.
/// Uses WriteConsoleA on Windows, write() on POSIX — no libc buffering.
void FastPrint(const char *data, unsigned int size);

/// High-resolution monotonic timestamp in nanoseconds.
/// On Windows uses QueryPerformanceCounter; on POSIX uses CLOCK_MONOTONIC.
uint64_t GetTimestampNs();

/// Non-blocking single-character read from stdin.
/// Returns true and sets out_char if a key was pending; false otherwise.
bool PollKey(char &out_char);

} // namespace pal
} // namespace tvk
