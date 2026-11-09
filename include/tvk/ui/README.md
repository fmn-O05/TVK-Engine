# `tvk/ui` — 2D Terminal Canvas & Immediate-Mode TUI

Planned headers for 2D overlay rendering and terminal UI widgets (see [`ROADMAP.md`](../../../ROADMAP.md) — Phase 3 & Phase 6):

- `Canvas2D.h` — Cell-space and sub-pixel 2D drawing API (UTF-8 text, boxes, borders, progress bars, lines, 2D sprites) composited cleanly over the 3D backbuffer before ANSI diffing.
- `UIContext.h` — Immediate-mode terminal UI (`BeginPanel`, `Label`, `Button`, `ProgressBar`, `Slider`, `ListBox`) with keyboard navigation and SGR mouse click support.
- `Theme.h` — Box-drawing styles (`Single`, `Double`, `Rounded`, `Heavy`, `ASCII`) and color palettes.
