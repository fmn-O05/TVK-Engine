#include "tvk/TVK_PAL.h"
#include <cstdio>
#include <cstdlib>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <conio.h>
#include <windows.h>
#else
#include <poll.h>
#include <sys/ioctl.h> // TIOCGWINSZ
#include <termios.h>
#include <time.h>
#include <unistd.h>
#endif

namespace tvk {
namespace pal {

// =========================================================================
// Terminal size
// =========================================================================

bool TryGetTerminalSize(int &out_w, int &out_h) {
#ifdef _WIN32
  HANDLE handles[2] = {GetStdHandle(STD_OUTPUT_HANDLE),
                       GetStdHandle(STD_ERROR_HANDLE)};
  for (HANDLE h : handles) {
    if (!h || h == INVALID_HANDLE_VALUE)
      continue;
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(h, &csbi)) {
      int w = csbi.srWindow.Right - csbi.srWindow.Left + 1;
      int h2 = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
      if (w > 0 && h2 > 0) {
        out_w = w;
        out_h = h2;
        return true;
      }
    }
  }
#else
  // POSIX: query terminal size via ioctl (handles SIGWINCH automatically)
  struct winsize ws{};
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 &&
      ws.ws_row > 0) {
    out_w = static_cast<int>(ws.ws_col);
    out_h = static_cast<int>(ws.ws_row);
    return true;
  }
#endif
  // Fallback: COLUMNS / LINES environment variables
  const char *cols = std::getenv("COLUMNS");
  const char *lines = std::getenv("LINES");
  if (cols && lines) {
    int w = std::atoi(cols), h = std::atoi(lines);
    if (w > 0 && h > 0) {
      out_w = w;
      out_h = h;
      return true;
    }
  }
  return false;
}

// =========================================================================
// Terminal setup / restore
// =========================================================================

bool SetupTerminal() {
#ifdef _WIN32
  // Try to enable VT processing on stdout first, then stderr as fallback.
  // Do NOT return false if this fails — Windows Terminal, VS Code terminal,
  // and ConPTY hosts all support VT sequences even when GetConsoleMode()
  // returns false (they are not classic console handles).
  HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
  DWORD dwMode = 0;
  if (GetConsoleMode(hOut, &dwMode)) {
    // Enable VT and disable Wrap at EOL (0x0002) to prevent bottom-right glitching
    SetConsoleMode(hOut, (dwMode | 0x0004) & ~0x0002);
  } else {
    // Try stderr handle (VS Code / Windows Terminal often attach stderr)
    HANDLE hErr = GetStdHandle(STD_ERROR_HANDLE);
    if (hErr && hErr != INVALID_HANDLE_VALUE && GetConsoleMode(hErr, &dwMode)) {
      SetConsoleMode(hErr, (dwMode | 0x0004) & ~0x0002);
    }
  }
  SetConsoleOutputCP(CP_UTF8);
#endif
  // Alternate screen buffer + hide cursor (ANSI / VT sequences)
  const char seq[] = "\x1b[?1049h\x1b[?25l";
  FastPrint(seq, sizeof(seq) - 1);
  return true; // Always succeed — VT may still work even without GetConsoleMode
}

void RestoreTerminal() {
  const char seq[] = "\x1b[?1049l\x1b[?25h";
  FastPrint(seq, sizeof(seq) - 1);
#ifdef _WIN32
  // Flush the console handle
  FlushFileBuffers(GetStdHandle(STD_OUTPUT_HANDLE));
#else
  fsync(STDOUT_FILENO);
#endif
}

// =========================================================================
// Fast output — bypasses libc buffering entirely
// =========================================================================

void FastPrint(const char *data, unsigned int size) {
#ifdef _WIN32
  HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
  DWORD written = 0;
  
  // Windows Console IPC chokes on extremely large single WriteFile calls (e.g. > 30MB)
  // We chunk the output safely at 64KB blocks to prevent freezing or arbitrary failures.
  const unsigned int chunk_size = 65536;
  unsigned int offset = 0;
  while (offset < size) {
    DWORD to_write = static_cast<DWORD>((size - offset > chunk_size) ? chunk_size : (size - offset));
    WriteFile(hOut, data + offset, to_write, &written, nullptr);
    offset += to_write;
  }
#else
  // POSIX write handles large buffers much better, but we can chunk to be safe
  const unsigned int chunk_size = 1048576; // 1MB chunks on POSIX
  unsigned int offset = 0;
  while (offset < size) {
    unsigned int to_write = (size - offset > chunk_size) ? chunk_size : (size - offset);
    ssize_t w = write(STDOUT_FILENO, data + offset, to_write);
    if (w <= 0) break; // Error or closed pipe
    offset += static_cast<unsigned int>(w);
  }
#endif
}

// =========================================================================
// High-resolution monotonic timer
// =========================================================================

uint64_t GetTimestampNs() {
#ifdef _WIN32
  static LARGE_INTEGER freq = {};
  if (freq.QuadPart == 0)
    QueryPerformanceFrequency(&freq);
  LARGE_INTEGER cnt;
  QueryPerformanceCounter(&cnt);
  // Avoid overflow: split into seconds and remainder
  uint64_t secs = static_cast<uint64_t>(cnt.QuadPart) /
                  static_cast<uint64_t>(freq.QuadPart);
  uint64_t rem = static_cast<uint64_t>(cnt.QuadPart) %
                 static_cast<uint64_t>(freq.QuadPart);
  return secs * 1000000000ULL +
         rem * 1000000000ULL / static_cast<uint64_t>(freq.QuadPart);
#else
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL +
         static_cast<uint64_t>(ts.tv_nsec);
#endif
}

// =========================================================================
// Non-blocking key poll
// =========================================================================

bool PollKey(char &out_char) {
#ifdef _WIN32
  if (_kbhit()) {
    out_char = static_cast<char>(_getch());
    return true;
  }
  return false;
#else
  struct pollfd pfd = {STDIN_FILENO, POLLIN, 0};
  if (::poll(&pfd, 1, 0) > 0) {
    ssize_t n = ::read(STDIN_FILENO, &out_char, 1);
    return n > 0;
  }
  return false;
#endif
}

} // namespace pal
} // namespace tvk
