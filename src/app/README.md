# `src/app` — Layer 9 Implementation (`tvk/app`)

- **Layer**: Layer 9 (`tvk/app`)
- **Public Headers**: [`include/tvk/app/`](../../include/tvk/app/README.md)
- **Technical Specifications**: [`docs/specs/01_CORE_AND_PLATFORM.md`](../../docs/specs/01_CORE_AND_PLATFORM.md) & [`docs/specs/03_PRESENTER_AND_CANVAS2D.md`](../../docs/specs/03_PRESENTER_AND_CANVAS2D.md) | [`ROADMAP.md` (Phase 1 & Phase 3)](../../ROADMAP.md)

## Planned Phase 1 & Phase 3 Sources
- `Engine.cpp` — Master 6-stage engine frame loop, `TimestepAccumulator` fixed-step simulation, and subsystem orchestration.
- `DebugOverlay.cpp` — In-terminal `F1` profiler HUD, frame-time sparkline, and `Log` ring-buffer console.
