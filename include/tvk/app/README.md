# `tvk/app` — Engine Runtime & Application Layer

Planned headers for the application lifecycle and engine host (see [`ROADMAP.md`](../../../ROADMAP.md) — Phase 1 & Phase 4):

- `Engine.h` — Master `tvk::Engine` runtime coordinating PAL, Renderer, ECS World, Physics, UI, Audio, and AssetManager.
- `Application.h` — Base `tvk::Application` / `tvk::IGame` interface (`OnInit`, `OnFixedUpdate`, `OnUpdate`, `OnRender`, `OnUI`, `OnShutdown`).
- `EngineConfig.h` — Startup configuration (target FPS, fixed timestep, blitter mode, terminal fallback size, thread count).
- `DebugOverlay.h` — Built-in toggleable (`F1`) terminal profiler, frame-time graph, and entity inspector.
