# `src/platform` — Layer 1 Implementation (`tvk/platform`)

- **Layer**: Layer 1 (`tvk/platform`)
- **Public Headers**: [`include/tvk/platform/`](../../include/tvk/platform/README.md)
- **Technical Specification**: [`docs/specs/01_CORE_AND_PLATFORM.md`](../../docs/specs/01_CORE_AND_PLATFORM.md)

## Active Baseline Sources (`M1`)
- `PAL.cpp` — Cross-platform POSIX (`termios`, `ioctl`, `poll`, `sigaction`) and Win32 Console raw mode setup/restoration (`B-13`, `B-31`), async-signal-safe crash teardown (`SIGINT`, `SIGTERM`, `SIGABRT`, `SIGSEGV`), `SIGWINCH` resize flag tracking, `FastPrint()`, and high-resolution timing.

## Planned Phase 1 Sources
- `Input.cpp` — Non-blocking 4 KB buffered `stdin` reader and 7-state ANSI CSI / Kitty `\x1b[>1u` / SGR 1006 mouse parser.
