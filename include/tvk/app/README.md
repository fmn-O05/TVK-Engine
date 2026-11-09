# `include/tvk/app` — Layer 9: Engine Runtime & Application Host

- **Layer**: Layer 9 (`tvk/app`)
- **Responsibility**: Top-level `tvk::Engine` runtime loop, fixed-timestep simulation accumulator with render interpolation $\alpha$, `tvk::Application` game lifecycle interface, `EngineConfig`, and built-in toggleable (`F1`) `DebugOverlay` profiler HUD.
- **Dependency Rules**: Top layer of the 10-layer DAG; orchestrates Layers 0 through 8.
- **Technical Specifications**: [`docs/specs/01_CORE_AND_PLATFORM.md`](../../../docs/specs/01_CORE_AND_PLATFORM.md) & [`docs/specs/03_PRESENTER_AND_CANVAS2D.md`](../../../docs/specs/03_PRESENTER_AND_CANVAS2D.md) | [`ROADMAP.md` (Phase 1 & Phase 3)](../../../ROADMAP.md)

## Planned Phase 1 & Phase 3 Headers
- `EngineConfig.h` — Declarative startup configuration (target FPS, fixed timestep, initial blitter, worker thread count, headless fallback dimensions, frame arena capacity).
- `Application.h` — Base game lifecycle interface (`OnInit`, `OnFixedUpdate(fixedDt)`, `OnUpdate(dt)`, `OnRender(cmd, alpha)`, `OnUI(canvas, ui)`, `OnShutdown`).
- `Engine.h` — Master runtime coordinator owning `TerminalGuard`, `Input`, `JobSystem`, `FrameLinearArena`, `Renderer`, `Scene`, `PhysicsWorld`, `AssetManager`, `UIContext`, `AudioEngine`, and `DebugOverlay`.
- `DebugOverlay.h` — Toggleable `F1` terminal profiler HUD (FPS, 60-frame timing sparkline, per-phase CPU breakdown, triangle/tile/ANSI counters, `F2` blitter hot-swap, and `Log` console).
