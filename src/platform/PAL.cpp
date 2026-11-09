#include "tvk/platform/PAL.h"
#include <atomic>
#include <csignal>
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

namespace {

std::atomic<bool> s_TerminalActive{false};
std::atomic<bool> s_AtExitRegistered{false};

constexpr char kInitSeq[] = "\x1b[?1049h\x1b[?25l";
constexpr char kResetSeq[] =
    "\x1b[?1006l\x1b[?1002l\x1b[?1000l\x1b[0m\x1b[?25h\x1b[?2026l\x1b[?1049l";

#ifdef _WIN32
DWORD s_OrigOutMode = 0;
DWORD s_OrigInMode = 0;
UINT s_OrigOutCP = 0;
HANDLE s_SavedOutHandle = INVALID_HANDLE_VALUE;
HANDLE s_SavedInHandle = INVALID_HANDLE_VALUE;
#else
struct termios s_OrigTermios {};
volatile sig_atomic_t s_HasOrigTermios = 0;
volatile sig_atomic_t s_ResizePending = 0;

void SigwinchHandler(int) noexcept { s_ResizePending = 1; }

void AsyncSignalRestoreHandler(int sig) noexcept {
  if (s_TerminalActive.exchange(false, std::memory_order_acq_rel)) {
    if (s_HasOrigTermios) {
      tcsetattr(STDIN_FILENO, TCSAFLUSH, &s_OrigTermios);
    }
    const ssize_t w =
        ::write(STDOUT_FILENO, kResetSeq, sizeof(kResetSeq) - 1);
    (void)w;
  }

  struct sigaction sa {};
  sa.sa_handler = SIG_DFL;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sigaction(sig, &sa, nullptr);
  raise(sig);
}
#endif

} // namespace

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
  struct winsize ws {};
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
  if (s_TerminalActive.exchange(true, std::memory_order_acq_rel)) {
    return true;
  }

  if (!s_AtExitRegistered.exchange(true, std::memory_order_acq_rel)) {
    std::atexit(RestoreTerminal);
  }

#ifdef _WIN32
  s_OrigOutCP = GetConsoleOutputCP();
  HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
  DWORD dwMode = 0;
  if (hOut && hOut != INVALID_HANDLE_VALUE && GetConsoleMode(hOut, &dwMode)) {
    s_SavedOutHandle = hOut;
    s_OrigOutMode = dwMode;
    SetConsoleMode(hOut, (dwMode | 0x0004) & ~0x0002);
  } else {
    HANDLE hErr = GetStdHandle(STD_ERROR_HANDLE);
    if (hErr && hErr != INVALID_HANDLE_VALUE && GetConsoleMode(hErr, &dwMode)) {
      s_SavedOutHandle = hErr;
      s_OrigOutMode = dwMode;
      SetConsoleMode(hErr, (dwMode | 0x0004) & ~0x0002);
    }
  }
  HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
  DWORD inMode = 0;
  if (hIn && hIn != INVALID_HANDLE_VALUE && GetConsoleMode(hIn, &inMode)) {
    s_SavedInHandle = hIn;
    s_OrigInMode = inMode;
    SetConsoleMode(hIn, inMode & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT));
  }
  SetConsoleOutputCP(CP_UTF8);
#else
  if (isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &s_OrigTermios) == 0) {
    s_HasOrigTermios = 1;
    struct termios raw = s_OrigTermios;
    raw.c_iflag &=
        static_cast<tcflag_t>(~(BRKINT | ICRNL | INPCK | ISTRIP | IXON));
    raw.c_cflag |= static_cast<tcflag_t>(CS8);
    raw.c_lflag &= static_cast<tcflag_t>(~(ECHO | ICANON | IEXTEN));
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
  }

  struct sigaction saCrash {};
  saCrash.sa_handler = AsyncSignalRestoreHandler;
  sigemptyset(&saCrash.sa_mask);
  saCrash.sa_flags = SA_RESETHAND;
  sigaction(SIGINT, &saCrash, nullptr);
  sigaction(SIGTERM, &saCrash, nullptr);
  sigaction(SIGABRT, &saCrash, nullptr);
  sigaction(SIGSEGV, &saCrash, nullptr);

  struct sigaction saWinch {};
  saWinch.sa_handler = SigwinchHandler;
  sigemptyset(&saWinch.sa_mask);
  saWinch.sa_flags = SA_RESTART;
  sigaction(SIGWINCH, &saWinch, nullptr);
#endif

  FastPrint(kInitSeq, sizeof(kInitSeq) - 1);
  return true;
}

void RestoreTerminal() {
  if (!s_TerminalActive.exchange(false, std::memory_order_acq_rel)) {
    return;
  }

#ifdef _WIN32
  FastPrint(kResetSeq, sizeof(kResetSeq) - 1);
  if (s_SavedOutHandle != INVALID_HANDLE_VALUE) {
    SetConsoleMode(s_SavedOutHandle, s_OrigOutMode);
    FlushFileBuffers(s_SavedOutHandle);
  }
  if (s_SavedInHandle != INVALID_HANDLE_VALUE) {
    SetConsoleMode(s_SavedInHandle, s_OrigInMode);
  }
  if (s_OrigOutCP != 0) {
    SetConsoleOutputCP(s_OrigOutCP);
  }
#else
  if (s_HasOrigTermios) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &s_OrigTermios);
  }
  const ssize_t w = ::write(STDOUT_FILENO, kResetSeq, sizeof(kResetSeq) - 1);
  (void)w;
  fsync(STDOUT_FILENO);
#endif
}

bool IsTerminalSetup() {
  return s_TerminalActive.load(std::memory_order_acquire);
}

bool ConsumeResizeEvent() {
#ifdef _WIN32
  return false;
#else
  if (s_ResizePending != 0) {
    s_ResizePending = 0;
    return true;
  }
  return false;
#endif
}

void FastPrint(const char *data, unsigned int size) {
#ifdef _WIN32
  HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
  DWORD written = 0;

  const unsigned int chunk_size = 65536;
  unsigned int offset = 0;
  while (offset < size) {
    DWORD to_write = static_cast<DWORD>(
        (size - offset > chunk_size) ? chunk_size : (size - offset));
    WriteFile(hOut, data + offset, to_write, &written, nullptr);
    offset += to_write;
  }
#else
  const unsigned int chunk_size = 1048576;
  unsigned int offset = 0;
  while (offset < size) {
    unsigned int to_write =
        (size - offset > chunk_size) ? chunk_size : (size - offset);
    ssize_t w = ::write(STDOUT_FILENO, data + offset, to_write);
    if (w <= 0)
      break;
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

} // namespace pal
} // namespace tvk
