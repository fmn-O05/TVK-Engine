#include "tvk/platform/PAL.h"
#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>

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

TerminalConfig s_ActiveConfig{};
char s_ResumeSeq[128]{};
size_t s_ResumeSeqLen = 0;
char s_ResetSeq[128]{};
size_t s_ResetSeqLen = 0;

#ifdef _WIN32
DWORD s_OrigOutMode = 0;
DWORD s_OrigInMode = 0;
UINT s_OrigOutCP = 0;
HANDLE s_SavedOutHandle = INVALID_HANDLE_VALUE;
HANDLE s_SavedInHandle = INVALID_HANDLE_VALUE;
#else
struct termios s_OrigTermios {};
struct termios s_RawTermios {};
volatile sig_atomic_t s_HasOrigTermios = 0;
volatile sig_atomic_t s_HasRawTermios = 0;
volatile sig_atomic_t s_ResizePending = 0;
volatile sig_atomic_t s_Suspended = 0;

void SigwinchHandler(int) noexcept { s_ResizePending = 1; }

void AsyncSignalRestoreHandler(int sig) noexcept {
  const int savedErrno = errno;
  if (s_TerminalActive.exchange(false, std::memory_order_acq_rel)) {
    if (s_HasOrigTermios) {
      tcsetattr(STDIN_FILENO, TCSAFLUSH, &s_OrigTermios);
    }
    if (s_ResetSeqLen > 0) {
      const ssize_t w = ::write(STDOUT_FILENO, s_ResetSeq, s_ResetSeqLen);
      (void)w;
    }
  }

  struct sigaction sa {};
  sa.sa_handler = SIG_DFL;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sigaction(sig, &sa, nullptr);
  errno = savedErrno;
  raise(sig);
}

void SigTstpHandler(int sig) noexcept {
  (void)sig;
  const int savedErrno = errno;
  if (s_TerminalActive.load(std::memory_order_relaxed)) {
    if (s_HasOrigTermios) {
      tcsetattr(STDIN_FILENO, TCSAFLUSH, &s_OrigTermios);
    }
    if (s_ResetSeqLen > 0) {
      const ssize_t w = ::write(STDOUT_FILENO, s_ResetSeq, s_ResetSeqLen);
      (void)w;
      fsync(STDOUT_FILENO);
    }
    s_Suspended = 1;
  }

  struct sigaction sa {};
  sa.sa_handler = SIG_DFL;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sigaction(SIGTSTP, &sa, nullptr);

  sigset_t set;
  sigemptyset(&set);
  sigaddset(&set, SIGTSTP);
  sigprocmask(SIG_UNBLOCK, &set, nullptr);

  errno = savedErrno;
  raise(SIGTSTP);
}

void SigContHandler(int sig) noexcept {
  (void)sig;
  const int savedErrno = errno;
  if (s_Suspended) {
    struct sigaction saTstp {};
    saTstp.sa_handler = SigTstpHandler;
    sigemptyset(&saTstp.sa_mask);
    saTstp.sa_flags = SA_RESTART;
    sigaction(SIGTSTP, &saTstp, nullptr);

    if (s_HasRawTermios) {
      tcsetattr(STDIN_FILENO, TCSAFLUSH, &s_RawTermios);
    }
    if (s_ResumeSeqLen > 0) {
      const ssize_t w = ::write(STDOUT_FILENO, s_ResumeSeq, s_ResumeSeqLen);
      (void)w;
      fsync(STDOUT_FILENO);
    }

    s_Suspended = 0;
    s_ResizePending = 1;
  }
  errno = savedErrno;
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

bool SetupTerminal(const TerminalConfig &config) {
  if (s_TerminalActive.exchange(true, std::memory_order_acq_rel)) {
    return true;
  }

  bool useAltScreen = config.alternateScreen && config.enableAlternateScreen;
  bool useHideCursor = config.hideCursor;
  bool useMouse = config.mouseTracking && config.enableMouseTracking;
  bool useKitty = config.kittyKeyboard && config.enableKittyKeyboard;

  s_ActiveConfig = config;
  s_ActiveConfig.alternateScreen = useAltScreen;
  s_ActiveConfig.enableAlternateScreen = useAltScreen;
  s_ActiveConfig.hideCursor = useHideCursor;
  s_ActiveConfig.mouseTracking = useMouse;
  s_ActiveConfig.enableMouseTracking = useMouse;
  s_ActiveConfig.kittyKeyboard = useKitty;
  s_ActiveConfig.enableKittyKeyboard = useKitty;

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
    s_RawTermios = raw;
    s_HasRawTermios = 1;
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

  struct sigaction saTstp {};
  saTstp.sa_handler = SigTstpHandler;
  sigemptyset(&saTstp.sa_mask);
  saTstp.sa_flags = SA_RESTART;
  sigaction(SIGTSTP, &saTstp, nullptr);

  struct sigaction saCont {};
  saCont.sa_handler = SigContHandler;
  sigemptyset(&saCont.sa_mask);
  saCont.sa_flags = SA_RESTART;
  sigaction(SIGCONT, &saCont, nullptr);
#endif

  // Construct Init & Resume Sequence
  char initBuf[128];
  int len = 0;
  if (useAltScreen) {
    len += std::snprintf(initBuf + len, sizeof(initBuf) - static_cast<size_t>(len),
                         "\x1b[?1049h");
  }
  if (useHideCursor) {
    len += std::snprintf(initBuf + len, sizeof(initBuf) - static_cast<size_t>(len),
                         "\x1b[?25l");
  }
  if (useMouse) {
    len += std::snprintf(initBuf + len, sizeof(initBuf) - static_cast<size_t>(len),
                         "\x1b[?1000h\x1b[?1002h\x1b[?1006h");
  }
  if (useKitty) {
    len += std::snprintf(initBuf + len, sizeof(initBuf) - static_cast<size_t>(len),
                         "\x1b[>1u");
  }
  s_ResumeSeqLen = static_cast<size_t>(len);
  std::memcpy(s_ResumeSeq, initBuf, s_ResumeSeqLen);
  s_ResumeSeq[s_ResumeSeqLen] = '\0';

  // Construct Teardown & Reset Sequence
  char resetBuf[128];
  int rLen = 0;
  if (useMouse) {
    rLen += std::snprintf(resetBuf + rLen, sizeof(resetBuf) - static_cast<size_t>(rLen),
                          "\x1b[?1006l\x1b[?1002l\x1b[?1000l");
  }
  if (useKitty) {
    rLen += std::snprintf(resetBuf + rLen, sizeof(resetBuf) - static_cast<size_t>(rLen),
                          "\x1b[<u");
  }
  rLen += std::snprintf(resetBuf + rLen, sizeof(resetBuf) - static_cast<size_t>(rLen),
                        "\x1b[0m");
  if (useHideCursor) {
    rLen += std::snprintf(resetBuf + rLen, sizeof(resetBuf) - static_cast<size_t>(rLen),
                          "\x1b[?25h");
  }
  rLen += std::snprintf(resetBuf + rLen, sizeof(resetBuf) - static_cast<size_t>(rLen),
                        "\x1b[?2026l");
  if (useAltScreen) {
    rLen += std::snprintf(resetBuf + rLen, sizeof(resetBuf) - static_cast<size_t>(rLen),
                          "\x1b[?1049l");
  }
  s_ResetSeqLen = static_cast<size_t>(rLen);
  std::memcpy(s_ResetSeq, resetBuf, s_ResetSeqLen);
  s_ResetSeq[s_ResetSeqLen] = '\0';

  if (s_ResumeSeqLen > 0) {
    FastPrint(s_ResumeSeq, static_cast<unsigned int>(s_ResumeSeqLen));
  }
  return true;
}

bool SetupTerminal(bool mouseTracking, bool kittyKeyboard) {
  TerminalConfig config;
  config.alternateScreen = true;
  config.hideCursor = true;
  config.mouseTracking = mouseTracking;
  config.kittyKeyboard = kittyKeyboard;
  config.enableAlternateScreen = true;
  config.enableMouseTracking = mouseTracking;
  config.enableKittyKeyboard = kittyKeyboard;
  return SetupTerminal(config);
}

void RestoreTerminal() {
  if (!s_TerminalActive.exchange(false, std::memory_order_acq_rel)) {
    return;
  }

#ifdef _WIN32
  if (s_ResetSeqLen > 0) {
    FastPrint(s_ResetSeq, static_cast<unsigned int>(s_ResetSeqLen));
  }
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
  if (s_ResetSeqLen > 0) {
    const ssize_t w = ::write(STDOUT_FILENO, s_ResetSeq, s_ResetSeqLen);
    (void)w;
  }
  fsync(STDOUT_FILENO);
#endif
}

const TerminalConfig &GetActiveConfig() {
  return s_ActiveConfig;
}

const char *GetActiveResumeSeq() {
  return s_ResumeSeq;
}

const char *GetActiveResetSeq() {
  return s_ResetSeq;
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
 
size_t ReadInput(char *buf, size_t maxLen) {
#ifdef _WIN32
  if (!buf || maxLen == 0) return 0;
  size_t count = 0;
  while (count < maxLen && _kbhit()) {
    buf[count++] = static_cast<char>(_getch());
  }
  return count;
#else
  if (!buf || maxLen == 0) return 0;
  struct pollfd pfd = {STDIN_FILENO, POLLIN, 0};
  int ret = ::poll(&pfd, 1, 0);
  if (ret <= 0 || !(pfd.revents & (POLLIN | POLLHUP))) {
    return 0;
  }
  ssize_t n = ::read(STDIN_FILENO, buf, maxLen);
  return (n > 0) ? static_cast<size_t>(n) : 0;
#endif
}

} // namespace pal
} // namespace tvk

