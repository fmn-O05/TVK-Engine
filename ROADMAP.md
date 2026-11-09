# TVK Engine — Master Refactoring & Architectural Roadmap

This document defines the step-by-step engineering roadmap to take **TVK Engine (Terminal Vulkan Engine)** from its hardened Layer 0–3 software rendering foundation to a complete, production-ready **Terminal Game Engine**.

- **Forensic Audit & 32-Defect Catalog (`B-01`–`B-32`)**: [`docs/CODEBASE_AUDIT.md`](docs/CODEBASE_AUDIT.md)
- **10-Layer Architecture Blueprint & Mathematical Conventions**: [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md)
- **Granular Subsystem Technical Specifications**: [`docs/specs/01_*.md` through `07_*.md`](docs/specs/01_CORE_AND_PLATFORM.md)

---

## 1. Roadmap Overview & Specification Cross-Reference

| Phase | Milestone Name | Core Focus | Granular Spec Link | Status |
| :--- | :--- | :--- | :--- | :--- |
| **Phase 0** | **Multi-Pass Foundation Audit, Structural Cleanup & Baseline Defect Resolution (`B-01`–`B-32`)** | Reorganize into 10-layer DAG; purge dead code; add `Handle.h`, `Result.h`, `Assert.h`; resolve all 32 baseline defects across `CMakeLists.txt`, `core`, `platform`, `math`, `renderer`, and `examples`; wire `CTest` (`test_core`, `test_math`, `test_renderer`). | [`docs/CODEBASE_AUDIT.md`](docs/CODEBASE_AUDIT.md)<br>[`01_CORE_AND_PLATFORM.md`](docs/specs/01_CORE_AND_PLATFORM.md)<br>[`02_MATH_AND_RASTERIZER.md`](docs/specs/02_MATH_AND_RASTERIZER.md)<br>[`03_PRESENTER_AND_CANVAS2D.md`](docs/specs/03_PRESENTER_AND_CANVAS2D.md)<br>[`07_TESTING_AND_BENCHMARKS.md`](docs/specs/07_TESTING_AND_BENCHMARKS.md) | **COMPLETED** |
| **Phase 1** | **Platform Layer (PAL), Input State Machine, Core Arenas & Engine Loop** | RAII `TerminalGuard`, full ANSI CSI + Kitty `\x1b[>1u` + SGR 1006 Mouse input state machine (`Input`), `FrameLinearArena`, `PoolAllocator`, `TimestepAccumulator` ($\alpha$), `EventBus`, `Log`, `Engine` & `Application` host. | [`01_CORE_AND_PLATFORM.md`](docs/specs/01_CORE_AND_PLATFORM.md)<br>[`07_TESTING_AND_BENCHMARKS.md`](docs/specs/07_TESTING_AND_BENCHMARKS.md) | **READY FOR GREEN LIGHT** |
| **Phase 2** | **Advanced 3D Pipeline: 64-Bit SortKeys, Shading Models & Extended Primitives** | 64-bit `SortKey` command sorting, `ShadingModel::{Unlit, Flat, Gouraud, BlinnPhong, Toon}`, point/spot light attenuation, perspective-correct $1/w$ UV/attribute stepping, `CreateBox`/`CreateCone`/`CreateTorus`/`CreatePlane`, wireframe debug helpers (`DrawWireBox`, `DrawGrid`, `DrawAxes`). | [`02_MATH_AND_RASTERIZER.md`](docs/specs/02_MATH_AND_RASTERIZER.md)<br>[`07_TESTING_AND_BENCHMARKS.md`](docs/specs/07_TESTING_AND_BENCHMARKS.md) | Planned |
| **Phase 3** | **2D Terminal Canvas (`Canvas2D`), Bayer Dithering, OKLab LUT & `F1` Profiler HUD** | 4×4 ordered Bayer dithering, OKLab 256/16-color terminal fallback LUT, cell-space `Canvas2D` (`DrawText` with transparent 3D background preservation, Unicode box borders, progress bars, sparklines), and live `F1` `DebugOverlay`. | [`03_PRESENTER_AND_CANVAS2D.md`](docs/specs/03_PRESENTER_AND_CANVAS2D.md)<br>[`07_TESTING_AND_BENCHMARKS.md`](docs/specs/07_TESTING_AND_BENCHMARKS.md) | Planned |
| **Phase 4** | **Entity-Component-System (ECS) & Hierarchical Scene Graph** | 64-bit generational `Entity`, paged sparse-set `ComponentPool<T>`, smallest-pool lead `View<Ts...>`, deferred `EntityCommandBuffer`, topological parent-first `TransformSystem`, interpolated `RenderSystem` ($\alpha$). | [`04_ECS_AND_SCENE.md`](docs/specs/04_ECS_AND_SCENE.md)<br>[`07_TESTING_AND_BENCHMARKS.md`](docs/specs/07_TESTING_AND_BENCHMARKS.md) | Planned |
| **Phase 5** | **3D/2.5D Physics, Collision Solver & Terminal Mouse Ray Picking** | `Sphere`/`AABB`/`OBB`/`Capsule` colliders, arena-allocated `SpatialHashGrid`, analytical narrowphase manifolds, 8-iteration sequential impulse solver with Baumgarte stabilization, `CharacterController`, `ScreenCellToRay` & `PhysicsWorld::Raycast`. | [`05_PHYSICS_AND_PICKING.md`](docs/specs/05_PHYSICS_AND_PICKING.md)<br>[`07_TESTING_AND_BENCHMARKS.md`](docs/specs/07_TESTING_AND_BENCHMARKS.md) | Planned |
| **Phase 6** | **Asset Pipeline (`.OBJ`/`.MTL`, Textures) & Immediate-Mode TUI (`UIContext`)** | `AssetManager` resource pools, zero-dependency Wavefront `.obj`/`.mtl` parser, `Texture` nearest/bilinear sampling + procedural generators, immediate-mode `UIContext` ID stack (`Button`, `Panel`, `Checkbox`, `SliderFloat`, `ListBox`). | [`06_ASSETS_UI_AND_AUDIO.md`](docs/specs/06_ASSETS_UI_AND_AUDIO.md)<br>[`07_TESTING_AND_BENCHMARKS.md`](docs/specs/07_TESTING_AND_BENCHMARKS.md) | Planned |
| **Phase 7** | **Retro Synth Audio Subsystem, Benchmark Runner & Flagship Terminal Game** | Zero-dependency OS audio backends (`ALSA`/`PulseAudio`/`WinMM`/`AudioQueue`/`Null`), procedural `SynthPatch` ADSR chiptune/SFX synthesizer, `.wav` mixer, `benchmark_runner`, and flagship playable 3D terminal game. | [`06_ASSETS_UI_AND_AUDIO.md`](docs/specs/06_ASSETS_UI_AND_AUDIO.md)<br>[`07_TESTING_AND_BENCHMARKS.md`](docs/specs/07_TESTING_AND_BENCHMARKS.md) | Planned |

---

## 2. Quantitative Per-Phase Verification & Performance Gate Table

Every phase must pass the following quantitative engineering gates before advancing to the next phase:

| Phase | Required CTest Suites | Compiler & SIMD Build Matrix Gate | CPU Frame Budget (`160×50` cells, 10k tris, 8 threads) | Memory Footprint Ceiling |
| :--- | :--- | :--- | :--- | :--- |
| **Phase 0 (Baseline)** | `test_core`, `test_math`, `test_renderer` | 0 warnings (`-Wall -Wextra -Wpedantic`) across `Release`/`Debug` × `TVK_ENABLE_SIMD=ON/OFF` | $\le 5.5\text{ ms}$ ($\ge 180\text{ FPS}$ headless) | $\le 16\text{ MB}$ |
| **Phase 1 (Core & PAL)** | `+ test_input` (CSI, Kitty `\x1b[>1u`, SGR 1006, `FrameLinearArena`, `EventBus`, `Time`) | 100% pass across `SIMD=ON/OFF` + clean `SIGINT`/`ESC` terminal restoration | $\le 5.5\text{ ms}$ (Input poll $\le 0.05\text{ ms}$) | $\le 16\text{ MB}$ (`0` per-frame heap allocs) |
| **Phase 2 (Math & 3D)** | `+ test_rasterizer_extended` (`SortKey`, `ShadingModel`, UV $1/w$, 3D wireframes) | Bit/epsilon parity ($\le 1\text{ LSB}$) between AVX2 and Scalar rasterizer paths | $\le 5.3\text{ ms}$ (Vertex $\le 0.8\text{ ms}$, Geo $\le 1.2\text{ ms}$, Raster $\le 2.5\text{ ms}$) | $\le 16\text{ MB}$ |
| **Phase 3 (Presenter & 2D)** | `+ test_presenter_canvas` (5 blitters, Bayer dither, `Canvas2D` transparent text, `DECSET 2026`) | Zero front-buffer color drift; $\le 200\text{ KB/s}$ differential ANSI bandwidth on static camera | $\le 5.5\text{ ms}$ (2D UI + ANSI diff $\le 0.8\text{ ms}$) | $\le 16\text{ MB}$ |
| **Phase 4 (ECS & Scene)** | `+ test_ecs` (`Entity` recycling, sparse-set $O(1)$ ops, `View<A,B,C>`, hierarchy $\alpha$) | 10,000 entities iterated in $\le 0.25\text{ ms}$; topological transform update $\le 0.30\text{ ms}$ | $\le 6.0\text{ ms}$ | $\le 20\text{ MB}$ |
| **Phase 5 (Physics)** | `+ test_physics` (All collider manifolds, impulse bounce/stacking, `ScreenCellToRay`) | 500 dynamic rigid bodies stepped at $60\text{ Hz}$ in $\le 1.2\text{ ms}$ with 0 heap allocations | $\le 7.2\text{ ms}$ ($\ge 135\text{ FPS}$ headless) | $\le 22\text{ MB}$ |
| **Phase 6 (Assets & TUI)** | `+ test_assets_ui` (`.obj`/`.mtl` parser, UV sampling, `UIContext` ID stack & mouse/key nav) | `.obj` parse $\ge 100\text{k tris/sec}$; zero memory leaks under AddressSanitizer (`ASan`) | $\le 7.8\text{ ms}$ | $\le 32\text{ MB}$ (including loaded meshes/textures) |
| **Phase 7 (Audio & Game)** | `+ test_audio` (ADSR synth waveform generation, mixer clipping) + `benchmark_runner` | Full CI matrix green; flagship game runs at steady $60\text{ FPS}$ ($16.67\text{ ms}$ budget) | $\le 8.5\text{ ms}$ ($> 50\%$ frame headroom at $60\text{ FPS}$) | $\le 32\text{ MB}$ |

---

## 3. Phase 0: Multi-Pass Foundation Audit, Cleanup & Baseline Refinement (Completed)

### What Was Accomplished
1. **Multi-Pass Forensic Codebase Audit ([`docs/CODEBASE_AUDIT.md`](docs/CODEBASE_AUDIT.md))**:
   - Conducted two comprehensive audit passes across every file in the repository, benchmarking against Sokol, `bgfx`, Larrabee, Notcurses, Gaffer on Games, EnTT, Box2D-lite, and Clay, and cataloging **all 32 defects (`B-01` through `B-32`)**.
2. **Purged Dead / Broken Legacy Files**:
   - Removed `TVK_NotcursesPresenter.h/.cpp`, `TVK_Material.h`, unwired `FetchContent(tinyobjloader)`, dead `Renderer::DrawTriangle`, unused `HybridBarrier`, and unused point arenas (`m_GlobalPtArena`).
3. **Reorganized Repository into Strict 10-Layer Architecture**:
   - Structured all headers and sources under `include/tvk/{core,platform,math,renderer,scene,physics,ui,assets,audio,app}` and `src/{core,platform,math,renderer,scene,physics,ui,assets,audio,app}`, added standardized `README.md` files in every module directory, and authored 7 granular specification documents in `docs/specs/`.
4. **Added Layer 0 Core Primitives & Unified Layer 2 Math (`include/tvk/core/`, `include/tvk/math/`)**:
   - Added [`include/tvk/core/Handle.h`](include/tvk/core/Handle.h) (`Handle<Tag>`, `HandlePool<T, Tag>`), [`include/tvk/core/Result.h`](include/tvk/core/Result.h) (`ErrorCode`, `Result<T, E>`), and [`include/tvk/core/Assert.h`](include/tvk/core/Assert.h) (`TVK_ASSERT` with automatic terminal restoration).
   - Unified `Vec2`, `Vec3`, `Vec4`, `Quat`, `Mat3`, `Mat4`, `AABB`, `Plane`, `Frustum`, `Ray`, `Mat4NormalMatrix`, and Duff et al. branchless `BuildOrthonormalBasis` in [`include/tvk/math/Math.h`](include/tvk/math/Math.h).
5. **Resolved All 32 Baseline Defects (`B-01` through `B-32`) in Code**:
   - **Build & Portability (`B-01`, `B-03`, `B-16`, `B-21`–`B-24`, `B-32`)**: Config-aware `-O0 -g` / `-O3`, MinGW-only `-lwinpthread`, `#if defined(TVK_SIMD_AVX2)` guards + scalar fallbacks in `MathSIMD.h` and `Rasterizer.cpp`, zero-length normal masking (`dot > 1e-12f`), portable `CpuRelax()`, standard C++ `std::atomic`, and clamped `JobSystem(numThreads >= 1)` with `ParallelFor`.
   - **Camera, Math & 3D Pipeline (`B-02`, `B-04`–`B-10`, `B-14`, `B-15`, `B-17`–`B-20`, `B-25`, `B-26`, `B-29`, `B-30`)**: Left-handed `+Z` forward `Camera::LookAt` and `Orthographic` projection, `Camera::Orbit` cache update, dynamic `camera.GetNear()` clipping, 2D guard-band clamping (`[-16384, +16384]`), `int64_t` signed area, top-left fill bias (`-1`), per-vertex color modulation & interpolation, analytical 3×3 inverse-transpose normal matrix, full RGB multi-light diffuse + specular shading, oriented cylinders & capsules with hemisphere caps, depth-tested 3D line rasterization, `DepthOp::Always` draw order preservation, `std::vector<uint8_t> m_TileWasDirty`, and accurate `FrameStats` & headless sizing.
   - **Terminal Presenter, PAL & Examples (`B-11`–`B-13`, `B-27`, `B-28`, `B-31`)**: Thread-safe byte-to-decimal LUT, 24-bit TrueColor 2-color FG+BG Braille luminance clustering (zero pinholes on solid surfaces) + `HalfBlock`, `Quadrant`, `Sextant`, and `ASCII` blitters, exact front-buffer color tracking, `DECSET 2026` synchronized output (`\x1b[?2026h`..`\x1b[?2026l`), POSIX `termios` raw mode + async-signal-safe crash/exit restoration, and clean `'q'`/`ESC`/`--frames N` exit in `Sandbox` and `Asteroids_Demo`.
6. **Automated CTest Verification Suite (`tests/`)**:
   - Created `tests/TestHarness.h`, `tests/test_core.cpp`, `tests/test_math.cpp`, and `tests/test_renderer.cpp`, wired into `CMakeLists.txt` (`ctest --output-on-failure`).

---

## 4. Phase 1: Platform Layer (PAL), Input State Machine, Core Arenas & Engine Loop
> **Granular Technical Specification**: [`docs/specs/01_CORE_AND_PLATFORM.md`](docs/specs/01_CORE_AND_PLATFORM.md)

### Objectives
Elevate the baseline PAL raw mode and core primitives into a complete, ergonomic platform and application host with an ANSI CSI / Kitty `\x1b[>1u` / SGR 1006 mouse input state machine, `FrameLinearArena`, `TimestepAccumulator` with render interpolation $\alpha$, `EventBus`, `Log`, and `tvk::Engine`.

### Deliverables & Files to Implement
1. **RAII `TerminalGuard` (`include/tvk/platform/TerminalGuard.h`, `src/platform/PAL.cpp`)**:
   - Wrap idempotent `pal::SetupTerminal()` / `pal::RestoreTerminal()` in a non-copyable RAII guard with `SIGTSTP`/`SIGCONT` job-control suspend/resume support and Kitty Keyboard Protocol negotiation (`\x1b[>1u` on enter, `\x1b[<u` on exit).
2. **Full Keyboard & SGR 1006 Mouse Input System (`include/tvk/platform/Input.h`, `src/platform/Input.cpp`)**:
   - 4 KB single-syscall non-blocking `stdin` buffer drain feeding the 7-state parser (`Ground`, `EscSeen`, `CSI_Param`, `CSI_Intermediate`, `SS3`, `SGR_Mouse`, `UTF8_MultiByte`).
   - Standalone `ESC` disambiguation, `F1`–`F12`, arrows, navigation keys, modifier bitmasks (`Shift`, `Alt`, `Ctrl`), Kitty key-up events (`\x1b[<codepoint>;<mod>:3u`), and a configurable `180–350 ms` key-release decay timer for standard terminals.
   - SGR 1006 mouse tracking (`\x1b[<Cb;Cx;CyM` / `m`) for left/middle/right buttons, drag motion, scroll wheel, cell coordinates, and sub-pixel coordinates.
   - Action binding API (`BindAction(StringId, Key)`).
3. **Core Runtime Primitives (`include/tvk/core/{Memory.h,Time.h,EventBus.h,Log.h}`)**:
   - `FrameLinearArena` (64-byte aligned bump allocator with high-water mark telemetry) and generational `PoolAllocator<T>`.
   - `Clock` and `TimestepAccumulator` (spiral-of-death clamp at `0.25f` and render interpolation factor `alpha = accumulator / fixedDt`).
   - `EventBus` (typed synchronous/queued event channels) and thread-safe 256-entry ring-buffer `Log`.
4. **Engine & Application Host (`include/tvk/app/{Engine.h,Application.h,EngineConfig.h}`, `src/app/Engine.cpp`)**:
   - Deterministic 6-stage frame lifecycle (`OnInit`, `OnFixedUpdate(fixedDt)`, `OnUpdate(dt)`, `OnRender(cmd, alpha)`, `OnUI(canvas, ui)`, `OnShutdown`).

---

## 5. Phase 2: Advanced 3D Pipeline — SortKeys, Shading Models & Extended Primitives
> **Granular Technical Specification**: [`docs/specs/02_MATH_AND_RASTERIZER.md`](docs/specs/02_MATH_AND_RASTERIZER.md)

### Objectives
Expand the hardened baseline 3D software rasterizer with 64-bit command `SortKey` ordering, configurable shading models, point/spot light distance/cone attenuation, perspective-correct $1/w$ UV/attribute scanline stepping, additional procedural primitives, and 3D wireframe helpers.

### Deliverables & Files to Implement
1. **64-Bit `SortKey` Command Sorting (`include/tvk/renderer/CommandBuffer.h`)**:
   - Sort recorded commands by `[Pass (4b) | Layer (4b) | DepthOp (4b) | Material/Texture (20b) | Quantized Depth (16b) | Sequence (16b)]`.
2. **Shading Models & Light Attenuation (`include/tvk/renderer/CommandBuffer.h`, `src/renderer/Renderer.cpp`)**:
   - Support `ShadingModel::{Unlit, Flat, Gouraud, BlinnPhong, Toon}` and `LightType::{Directional, Point, Spot}` with quadratic distance attenuation and spot cone falloff.
3. **Perspective-Correct $1/w$ Attribute & UV Interpolation (`src/renderer/Rasterizer.cpp`)**:
   - Interpolate $(u/w, v/w)$ across scanlines and recover perspective-correct $(u, v)$ for texture sampling.
4. **Extended Primitives & 3D Wireframe Helpers (`include/tvk/renderer/Primitives.h`, `CommandBuffer.h`)**:
   - Add `CreateBox`, `CreateCone`, `CreateTorus`, `CreatePlane`, and `CommandBuffer` helpers (`DrawWireBox`, `DrawWireSphere`, `DrawGrid`, `DrawAxes`, `DrawFrustum`).

---

## 6. Phase 3: 2D Terminal Canvas (`Canvas2D`), Bayer Dithering, OKLab LUT & `F1` Profiler HUD
> **Granular Technical Specification**: [`docs/specs/03_PRESENTER_AND_CANVAS2D.md`](docs/specs/03_PRESENTER_AND_CANVAS2D.md)

### Objectives
Add ordered Bayer dithering and OKLab 256/16-color terminal fallback LUTs to `TerminalPresenter`, build the cell-space `Canvas2D` overlay compositor, and provide the toggleable `F1` `DebugOverlay` profiler HUD.

### Deliverables & Files to Implement
1. **Bayer Dithering & OKLab Palette Fallback (`src/renderer/TerminalPresenter.cpp`)**:
   - Optional 4×4 ordered Bayer dither matrix before cell clustering, plus OKLab perceptual quantization LUT for 256-color (`xterm-256color`) and 16-color terminals.
2. **2D Terminal Canvas Compositor (`include/tvk/ui/Canvas2D.h`, `src/ui/Canvas2D.cpp`)**:
   - `DrawText(x, y, text, fg, bg, BgMode)` (`BgMode::Transparent` preserves the underlying 3D cell's background color), `DrawBox` (`Single`, `Double`, `Rounded`, `Heavy`, `ASCII`), `DrawProgressBar`, `DrawSparkline`, `DrawLine2D`.
3. **In-Terminal `F1` Debug & Profiler Overlay (`include/tvk/app/DebugOverlay.h`, `src/app/DebugOverlay.cpp`)**:
   - Real-time FPS, 60-frame timing sparkline, phase breakdown (`Vertex`, `Geo/Bin`, `Tile Raster`, `2D/UI`, `ANSI Diff/Flush`), triangle/tile/ANSI byte counters, active blitter (`F2` hot-swap), and `Log` viewer.

---

## 7. Phase 4: Entity-Component-System (ECS) & Hierarchical Scene Graph
> **Granular Technical Specification**: [`docs/specs/04_ECS_AND_SCENE.md`](docs/specs/04_ECS_AND_SCENE.md)

### Objectives
Provide a cache-friendly sparse-set ECS and hierarchical Scene Graph with fixed-timestep transform interpolation ($\alpha$).

### Deliverables & Files to Implement
1. **Core ECS (`include/tvk/scene/{Entity.h,ComponentPool.h,Registry.h,EntityCommandBuffer.h}`)**:
   - 64-bit generational `Entity`, paged sparse-set `ComponentPool<T>`, smallest-pool lead `View<Ts...>`, and deferred `EntityCommandBuffer`.
2. **Built-In Components & Scene Systems (`include/tvk/scene/{Components.h,Scene.h}`, `src/scene/Scene.cpp`)**:
   - `TagComponent`, `TransformComponent` (`prevWorldMatrix`, `worldMatrix`), `HierarchyComponent` (`depth`), `MeshRendererComponent`, `PrimitiveRendererComponent`, `CameraComponent`, `LightComponent`.
   - `TransformSystem` (topological depth-sorted parent-before-child update) and `RenderSystem` ($\alpha$-interpolated command extraction).

---

## 8. Phase 5: 3D/2.5D Physics, Collision Solver & Terminal Mouse Ray Picking
> **Granular Technical Specification**: [`docs/specs/05_PHYSICS_AND_PICKING.md`](docs/specs/05_PHYSICS_AND_PICKING.md)

### Objectives
Enable interactive 3D/2.5D rigid-body physics, character movement, and 3D entity selection via terminal mouse clicks.

### Deliverables & Files to Implement
1. **Colliders, RigidBody & Broadphase (`include/tvk/physics/{Colliders.h,RigidBody.h,SpatialHash.h}`)**:
   - `SphereCollider`, `AABBCollider`, `OBBCollider`, `CapsuleCollider`, 16-bit layer/mask filtering, `RigidBodyComponent`, and `FrameLinearArena`-backed `SpatialHashGrid`.
2. **Narrowphase, Impulse Solver & Mouse Ray Picking (`include/tvk/physics/{Narrowphase.h,PhysicsWorld.h,Raycast.h}`)**:
   - Analytical contact manifolds, 8-iteration sequential impulse solver with Baumgarte stabilization ($\beta = 0.2$), `CharacterController`, and `ScreenCellToRay` + `PhysicsWorld::Raycast`.

---

## 9. Phase 6: Asset Pipeline (`.OBJ`/`.MTL`, Textures) & Immediate-Mode TUI (`UIContext`)
> **Granular Technical Specification**: [`docs/specs/06_ASSETS_UI_AND_AUDIO.md`](docs/specs/06_ASSETS_UI_AND_AUDIO.md)

### Objectives
Enable external `.obj`/`.mtl` mesh loading, 2D texture mapping, and immediate-mode terminal UI menus and widgets.

### Deliverables & Files to Implement
1. **Asset Pipeline (`include/tvk/assets/{AssetManager.h,ObjLoader.h,Texture.h}`, `src/assets/`)**:
   - `AssetManager` backed by `HandlePool<T, Tag>`, zero-dependency Wavefront `.obj`/`.mtl` parser, and `Texture` (`Nearest`/`Bilinear` sampling + procedural `Checkerboard`, `Brick`, `Noise`, `Gradient`).
2. **Immediate-Mode Terminal UI (`include/tvk/ui/{UIContext.h,Theme.h}`, `src/ui/UIContext.cpp`)**:
   - FNV-1a ID stack (`PushID`/`PopID`), `hotId`/`activeId`/`focusedId` state machine, vertical/horizontal box layout, and widgets (`BeginPanel`, `Label`, `Button`, `Checkbox`, `SliderFloat`, `ProgressBar`, `ListBox`).

---

## 10. Phase 7: Retro Synth Audio Subsystem, Benchmark Runner & Flagship Terminal Game
> **Granular Technical Specifications**: [`docs/specs/06_ASSETS_UI_AND_AUDIO.md`](docs/specs/06_ASSETS_UI_AND_AUDIO.md) & [`docs/specs/07_TESTING_AND_BENCHMARKS.md`](docs/specs/07_TESTING_AND_BENCHMARKS.md)

### Objectives
Complete the engine stack with procedural retro chiptune/SFX audio synthesis, automated performance benchmarking, and a flagship playable 3D terminal game showcasing all 10 layers.

### Deliverables & Files to Implement
1. **Audio Subsystem (`include/tvk/audio/{AudioEngine.h,Sound.h}`, `src/audio/AudioEngine.cpp`)**:
   - Zero-dependency OS audio backends (`ALSA`/`PulseAudio`, `WinMM`, `AudioQueue`, `NullAudioBackend`), procedural `SynthPatch` oscillator + `ADSR` envelope generator, `.wav` loader, and 3D spatial attenuation/panning.
2. **Headless Benchmark Runner (`tests/benchmark_runner.cpp`)**:
   - Automated multi-resolution (`80×24`, `160×50`, `240×80`) and multi-blitter performance benchmark verifying the $\le 5.3\text{ ms}$ frame budget.
3. **Flagship Playable 3D Terminal Game (`examples/`)**:
   - Complete playable game combining ECS (`tvk/scene`), 3D physics & mouse picking (`tvk/physics`), `.obj` meshes & procedural textures (`tvk/assets`), immediate-mode TUI menus & HUD (`tvk/ui`), procedural synth audio (`tvk/audio`), and live `F1`/`F2` engine diagnostics (`tvk/app`).
