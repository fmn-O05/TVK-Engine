# `src/ui` — Layer 6 Implementation (`tvk/ui`)

- **Layer**: Layer 6 (`tvk/ui`)
- **Public Headers**: [`include/tvk/ui/`](../../include/tvk/ui/README.md)
- **Technical Specifications**: [`docs/specs/03_PRESENTER_AND_CANVAS2D.md`](../../docs/specs/03_PRESENTER_AND_CANVAS2D.md) & [`docs/specs/06_ASSETS_UI_AND_AUDIO.md`](../../docs/specs/06_ASSETS_UI_AND_AUDIO.md) | [`ROADMAP.md` (Phase 3 & Phase 6)](../../ROADMAP.md)

## Planned Phase 3 & Phase 6 Sources
- `Canvas2D.cpp` — Cell-space UTF-8 text, Unicode box borders, progress bars, and sparklines composited into `TerminalCell` backbuffer.
- `UIContext.cpp` — Immediate-mode FNV-1a ID stack, box layout engine, and interactive widget implementations (`Button`, `Checkbox`, `SliderFloat`, `ListBox`).
