# `include/tvk/platform` — Layer 1: Platform Abstraction Layer (PAL)

- **Layer**: Layer 1 (`tvk/platform`)
- **Responsibility**: Cross-platform POSIX (`termios`, `ioctl`, `poll`, `sigaction`) and Win32 Console raw mode lifecycle, async-signal-safe terminal restoration (`SIGINT`, `SIGTERM`, `SIGABRT`, `SIGSEGV`, `atexit`), `SIGWINCH` resize detection, and non-blocking keyboard/mouse input state machine.
- **Dependency Rules**: Depends only on Layer 0 (`tvk/core`) and OS system headers.
- **Technical Specification**: [`docs/specs/01_CORE_AND_PLATFORM.md`](../../../docs/specs/01_CORE_AND_PLATFORM.md) | [`docs/ARCHITECTURE.md`](../../../docs/ARCHITECTURE.md)

## Active Baseline Headers (`M1`)
- `PAL.h` — Idempotent `SetupTerminal()`, async-signal-safe `RestoreTerminal()`, `TryGetTerminalSize()`, `ConsumeResizeEvent()`, `FastPrint()`, `PollKey()`, `GetTimeMilliseconds()`, and `SleepMilliseconds()`.

## Planned Phase 1 Headers
- `TerminalGuard.h` — Non-copyable RAII guard managing alternate screen buffer, raw mode, mouse tracking, Kitty keyboard negotiation (`\x1b[>1u`), and `SIGTSTP`/`SIGCONT` suspend/resume.
- `Input.h` — 4 KB buffered non-blocking `stdin` reader with a 7-state finite-state machine for ANSI CSI keys, Kitty key-release events, 180–350 ms key-release decay timer fallback, and SGR 1006 mouse tracking.
