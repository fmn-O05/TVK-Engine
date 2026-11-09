# `src/` Subsystem Implementation Directories

Implementation `.cpp` files mirror `include/tvk/<subsystem>/`:

- `src/core/` — Threading, JobSystem, frame arena allocators, logging, profiling.
- `src/platform/` — POSIX (`termios`, `ioctl`, `poll`, `SIGWINCH`/`SIGINT`) and Win32 Console PAL, keyboard/mouse input parser.
- `src/math/` — Matrices, quaternions, frustum culling, SIMD kernels.
- `src/renderer/` — Software 3D pipeline, tiled rasterizer, multi-blitter terminal presenters.
- `src/scene/` — ECS registry, scene graph, transform & render systems.
- `src/physics/` — Broadphase, narrowphase collision detection, impulse solver, raycasting.
- `src/ui/` — 2D cell canvas compositor and immediate-mode terminal UI widgets.
- `src/assets/` — Mesh/texture loaders and asset pools.
- `src/audio/` — Audio mixing and procedural sound synthesis.
- `src/app/` — Engine loop, application host, and in-terminal debug overlay.
