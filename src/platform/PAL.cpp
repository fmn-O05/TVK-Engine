#include "tvk/platform/PAL.h"
#include <cstdio>
#include <cstdlib>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <conio.h>
#include <windows.h>
#else
#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#endif

namespace tvk {
namespace pal {

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

  struct winsize ws{};
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 &&
      ws.ws_row > 0) {
    out_w = static_cast<int>(ws.ws_col);
    out_h = static_cast<int>(ws.ws_row);
    return true;
  }
#endif

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

bool SetupTerminal() {
#ifdef _WIN32

  HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
  DWORD dwMode = 0;
  if (GetConsoleMode(hOut, &dwMode)) {

    SetConsoleMode(hOut, (dwMode | 0x0004) & ~0x0002);
  } else {

    HANDLE hErr = GetStdHandle(STD_ERROR_HANDLE);
    if (hErr && hErr != INVALID_HANDLE_VALUE && GetConsoleMode(hErr, &dwMode)) {
      SetConsoleMode(hErr, (dwMode | 0x0004) & ~0x0002);
    }
  }
  SetConsoleOutputCP(CP_UTF8);
#endif

  const char seq[] = "\x1b[?1049h\x1b[?25l";
  FastPrint(seq, sizeof(seq) - 1);
  return true;
}

void RestoreTerminal() {
  const char seq[] = "\x1b[?1049l\x1b[?25h";
  FastPrint(seq, sizeof(seq) - 1);
#ifdef _WIN32

  FlushFileBuffers(GetStdHandle(STD_OUTPUT_HANDLE));
#else
  fsync(STDOUT_FILENO);
#endif
}

void FastPrint(const char *data, unsigned int size) {
#ifdef _WIN32
  HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
  DWORD written = 0;

  const unsigned int chunk_size = 65536;
  unsigned int offset = 0;
  while (offset < size) {
    DWORD to_write = static_cast<DWORD>((size - offset > chunk_size) ? chunk_size : (size - offset));
    WriteFile(hOut, data + offset, to_write, &written, nullptr);
    offset += to_write;
  }
#else

  const unsigned int chunk_size = 1048576;
  unsigned int offset = 0;
  while (offset < size) {
    unsigned int to_write = (size - offset > chunk_size) ? chunk_size : (size - offset);
    ssize_t w = write(STDOUT_FILENO, data + offset, to_write);
    if (w <= 0) break;
    offset += static_cast<unsigned int>(w);
  }
#endif
}

uint64_t GetTimestampNs() {
#ifdef _WIN32
  static LARGE_INTEGER freq = {};
  if (freq.QuadPart == 0)
    QueryPerformanceFrequency(&freq);
  LARGE_INTEGER cnt;
  QueryPerformanceCounter(&cnt);

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

}
}
