# `include/tvk/ui` — Layer 6: 2D Terminal Canvas & Immediate-Mode TUI

- **Layer**: Layer 6 (`tvk/ui`)
- **Responsibility**: Cell-space 2D overlay compositing (`Canvas2D`) with transparent 3D background preservation, Unicode box-drawing borders, progress bars, sparklines, and immediate-mode terminal UI widgets (`UIContext`) driven by an FNV-1a ID stack, keyboard navigation, and SGR 1006 mouse interaction.
- **Dependency Rules**: Depends on Layer 0 (`tvk/core`), Layer 1 (`tvk/platform`), Layer 2 (`tvk/math`), and Layer 3 (`tvk/renderer/TerminalPresenter.h`).
- **Technical Specifications**: [`docs/specs/03_PRESENTER_AND_CANVAS2D.md`](../../../docs/specs/03_PRESENTER_AND_CANVAS2D.md) & [`docs/specs/06_ASSETS_UI_AND_AUDIO.md`](../../../docs/specs/06_ASSETS_UI_AND_AUDIO.md) | [`ROADMAP.md` (Phase 3 & Phase 6)](../../../ROADMAP.md)

## Planned Phase 3 & Phase 6 Headers
- `Canvas2D.h` — Cell-space 2D compositor (`DrawText` with `BgMode::Transparent`, `DrawBox`, `FillRect`, `DrawProgressBar`, `DrawSparkline`).
- `UIContext.h` — Immediate-mode TUI state machine (`PushID`/`PopID`, `BeginPanel`/`EndPanel`, `Label`, `Button`, `Checkbox`, `SliderFloat`, `ProgressBar`, `ListBox`).
- `Theme.h` — Unicode box-drawing styles (`Single`, `Double`, `Rounded`, `Heavy`, `ASCII`) and color themes (`UITheme`).
