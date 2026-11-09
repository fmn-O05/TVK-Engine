# TVK Engine — Core Architecture Blueprint

**TVK Engine (Terminal Vulkan Engine)** is a high-performance, zero-dependency C++17/C++20 game engine designed to build real-time 3D, 2.5D, and 2D games that run directly inside modern command-line terminals at 60+ FPS.

For the multi-pass forensic audit and catalog of all 32 baseline defects (`B-01`–`B-32`), see [`docs/CODEBASE_AUDIT.md`](CODEBASE_AUDIT.md).  
For the phased implementation plan, see [`ROADMAP.md`](../ROADMAP.md).  
For granular bit-level, mathematical, and state-machine blueprints, see [Section 8: Index of Granular Technical Specifications](#8-index-of-granular-technical-specifications-docsspecs).

---

## 1. Architectural Philosophy & Industry Benchmark Rationale

### 1.1 Five Core Design Principles
1. **Data-Oriented & L1-Cache-Bounded by Default**:
   - Framebuffers and Z-buffers are processed inside **cell-aligned pixel tiles** (`64×64` max sub-pixels: `16 KB` color + `16 KB` depth = **`32 KB` per worker thread**), fitting entirely inside CPU L1 data cache with zero inter-thread pixel lock contention.
   - Entity-Component-System (ECS) storage uses contiguous sparse-set arrays (`ComponentPool<T>`) for linear cache-line iteration.
2. **Strict 10-Layer Directed Acyclic Graph (DAG)**:
   - Subsystems are organized into 10 strictly ordered architectural layers (`Layer 0` through `Layer 9`). Lower layers never `#include` or link against higher layers.
3. **Zero-Allocation Hot Paths & Deterministic Memory**:
   - Per-frame transient allocations (transformed vertices, projected triangles, tile bin nodes, UI draw commands, spatial hash cells) use pre-sized, bounded per-frame linear arenas reset in $O(1)$ at `BeginFrame()`, keeping default total engine footprint **$< 16\text{ MB}$**.
4. **Terminal-First Compositing (3D + 2D + TUI Unified Pipeline)**:
   - Unlike naive terminal renderers where raw `printf` HUD calls fight with 3D ANSI output and cause tearing/flickering, TVK composites the 3D rasterized sub-pixel buffer, 2D shapes, and crisp native UTF-8 text/UI widgets into a unified 16-byte `TerminalCell` backbuffer *before* performing a single parallel differential ANSI encode bracketed by **DECSET 2026 Synchronized Output (`\x1b[?2026h` ... `\x1b[?2026l`)** and a single atomic `write()`/`WriteFile()` syscall.
5. **Cross-Platform, Crash-Safe & Gracefully Degrading**:
   - Native POSIX (`termios`, `ioctl`, `poll`, async-signal-safe `SIGINT`/`SIGTERM`/`SIGABRT`/`SIGSEGV` terminal restoration, `SIGWINCH`) and Win32 Console (`WriteFile`, Virtual Terminal Processing, Console Input Events) backends.
   - SIMD kernels (`AVX2`/`FMA`) are guarded by `#if defined(TVK_SIMD_AVX2) && defined(__AVX2__)` and paired with bit/epsilon-equivalent scalar fallbacks so the engine compiles and passes tests on any CPU (`x86_64`, `ARM64` / Apple Silicon, or `-DTVK_ENABLE_SIMD=OFF`).

### 1.2 Industry Architecture Benchmark & Design Rationale

Every architectural subsystem in TVK Engine is benchmarked against proven, published industry standards:

- **Sokol (`sokol_gfx`) & Our Machinery — Generational Resource Handles (`tvk/core/Handle.h`)**:
  - Raw pointers across frames risk use-after-free bugs when meshes, textures, or entities are destroyed. TVK adopts Sokol's 32-bit `Handle<Tag>` (`16-bit index` + `16-bit generation`) and `HandlePool<T, Tag>`, validating slot generations in $O(1)$ without heap indirection or reference-counting overhead.
- **`bgfx` — Stateless Command Recording & 64-Bit Sort Keys (`tvk/renderer/CommandBuffer.h`)**:
  - Draw submissions record stateless commands paired with a 64-bit `SortKey` encoding `[Pass (4b) | Layer (4b) | DepthOp/Blend (4b) | Material/Texture (20b) | Quantized Depth (16b) | Submission Seq (16b)]`, ensuring deterministic front-to-back opaque sorting (maximizing Early-Z rejection) and back-to-front transparent/UI ordering (`DepthOp::Always`).
- **Intel Larrabee & Quake Software Rasterizers — Guard-Band Clipping, 64-Bit Area & Top-Left Fill Rule (`tvk/renderer/Rasterizer.cpp`)**:
  - Triangles are clipped against `camera.GetNear()`, projected through either Perspective ($w_{\text{clip}} = z_{\text{view}}$) or Orthographic ($w_{\text{clip}} = 1$) matrices, clamped to a 2D guard band (`[-16384, +16384]`) with `int64_t` signed area evaluation (preventing 32-bit integer overflow `B-25`), and rasterized using 8-wide AVX2 half-space edge functions with a `-1` bias on non-top-left edges to guarantee water-tight shared triangle edges without double-shading.
- **Notcurses, Termbox2, FTXUI & Kitty — Synchronized Terminal Presentation & Hybrid Input (`tvk/platform`, `tvk/renderer`)**:
  - Terminal emulators read PTY streams asynchronously; wrapping frame flushes in `DECSET 2026` Begin/End Synchronized Update (`\x1b[?2026h` ... `\x1b[?2026l`) guarantees tear-free atomic presentation.
  - Because legacy VT100/ANSI terminals do not emit key-up events, TVK combines **Kitty Keyboard Protocol (`\x1b[>1u`)** negotiation (for native press/repeat/release events on modern terminals) with a configurable **180–350 ms key-release decay timer** for standard xterm/VT100 terminals.
- **Gaffer on Games (*"Fix Your Timestep!"*) — Accumulator with Render Alpha Interpolation (`tvk/core`, `tvk/app`)**:
  - Physics steps run at a deterministic fixed $\Delta t$ (e.g. $1/60\text{ s}$) with a spiral-of-death clamp ($\le 0.25\text{ s}$), while rendering interpolates between `prevTransform` and `currTransform` using $\alpha = \text{accumulator} / \Delta t_{\text{fixed}} \in [0, 1)$ to eliminate visual micro-stutter.
- **EnTT, Flecs & Bevy — Paged Sparse-Set ECS & Lead-Pool Views (`tvk/scene`)**:
  - `ComponentPool<T>` pairs a paged sparse array ($O(1)$ entity-to-dense lookup without multi-megabyte flat arrays) with packed dense entity/component vectors, and `View<Ts...>` always iterates the *smallest* component pool first.
- **Box2D-lite (Erin Catto) — Sequential Impulse Solver with Warm-Starting & Baumgarte Stabilization (`tvk/physics`)**:
  - Analytical contact manifolds are solved via an 8-iteration sequential impulse loop clamping accumulated normal impulses ($j_n \ge 0$) and Coulomb tangent impulses ($|j_t| \le \mu j_n$).
- **Dear ImGui & Clay — Immediate-Mode ID Stack & Single-Pass Box Layout (`tvk/ui`)**:
  - Widgets are identified by an FNV-1a hash stack (`PushID`/`PopID`) driving a `hotId`/`activeId`/`focusedId` state machine rendered directly into the `TerminalCell` backbuffer.

---

## 2. Layered Architecture, Dependency Graph & Mathematical Conventions

### 2.1 10-Layer Architecture Diagram

```text
┌───────────────────────────────────────────────────────────────────────────┐
│ Layer 9: Engine Runtime & Application Host (tvk/app)                      │
│          Engine, Application, EngineConfig, DebugOverlay                  │
├─────────────────┬─────────────────┬───────────────────┬───────────────────┤
│ Layer 8: Audio  │ Layer 7: Assets │ Layer 6: 2D & TUI │ Layer 5: Physics  │
│ (tvk/audio)     │ (tvk/assets)    │ (tvk/ui)          │ (tvk/physics)     │
├─────────────────┴─────────────────┴───────────────────┴───────────────────┤
│ Layer 4: Scene & Entity-Component-System (tvk/scene)                      │
│          Entity, ComponentPool, Registry, Scene, Transform Hierarchy      │
├───────────────────────────────────────────────────────────────────────────┤
│ Layer 3: 3D Software Renderer & Terminal Presentation (tvk/renderer)      │
│          Renderer, Rasterizer, CommandBuffer, Camera, Mesh, Presenter     │
├───────────────────────────────────┬───────────────────────────────────────┤
│ Layer 2: Math & SIMD (tvk/math)   │ Layer 1: Platform PAL (tvk/platform)  │
│          Math, MathSIMD, Geometry │          TTY Raw Mode, Input, Signals │
├───────────────────────────────────┴───────────────────────────────────────┤
│ Layer 0: Core Foundation (tvk/core)                                       │
│          Types, Handle, Result, Assert, JobSystem, Memory, Time, Stats    │
└───────────────────────────────────────────────────────────────────────────┘
```

### 2.2 Strict Dependency Rules
- **Layer 0 (`tvk/core`)** depends only on the C++ standard library (`<cstdint>`, `<atomic>`, `<thread>`, `<mutex>`, etc.). Note: `Assert.h` invokes a forward-declared emergency terminal restore hook (`tvk::pal::RestoreTerminal()`) resolved at link time.
- **Layer 1 (`tvk/platform`)** and **Layer 2 (`tvk/math`)** depend only on `tvk/core`.
- **Layer 3 (`tvk/renderer`)** depends on `tvk/core`, `tvk/platform`, and `tvk/math`.
- **Layers 4–8 (`scene`, `physics`, `ui`, `assets`, `audio`)** depend on `tvk/core`, `tvk/platform`, `tvk/math`, and `tvk/renderer` (with `physics` and `ui` optionally integrating with `scene` components).
- **Layer 9 (`tvk/app`)** orchestrates all lower layers (`0` through `8`) and exposes the unified game developer API.

### 2.3 Mathematical & Coordinate Conventions (Engine-Wide Invariant)

Every subsystem across Layers 2–9 strictly obeys the following mathematical invariants (resolving legacy defects `B-07`, `B-08`, `B-18`, `B-20`, and `B-28`):

1. **World Space & View Space Handedness (Left-Handed `+Z` Forward Pipeline)**:
   - **Axes**: **`+X` Right, `+Y` Up, `+Z` Forward (into the screen)**.
   - **Camera `LookAt(eye, target, up)` Basis**:
     $$\vec{f} = \operatorname{normalize}(\text{target} - \text{eye}), \quad \vec{r} = \operatorname{normalize}(\vec{u}_{\text{up}} \times \vec{f}), \quad \vec{u} = \vec{f} \times \vec{r}$$
     Any point in front of the camera has strictly positive view-space depth $z_{\text{view}} = \vec{f} \cdot (\vec{p} - \text{eye}) > 0$.
   - **Near-Plane Clipping**: Clips polygons and 3D lines in view space against $z_{\text{view}} \ge z_{\text{near}} > 0$ (`camera.GetNear()`).

2. **Matrix Storage & Vector Multiplication Convention**:
   - **`Mat4` Layout**: Row-major `float m[4][4]` (`m[row][col]`), where basis vectors occupy rows `0..2` of view matrices (or columns `0..2` of model matrices) and **translation resides in column 3 (`m[0][3], m[1][3], m[2][3]`)**.
   - **Multiplication Order**: Column-vector post-multiplication:
     $$\vec{v}_{\text{clip}} = P \cdot V \cdot M \cdot \begin{bmatrix} x \\ y \\ z \\ 1 \end{bmatrix}$$
   - **Normal Matrix**: Surface normals transform by the $3\times 3$ inverse-transpose of the Model-View matrix (`Mat4NormalMatrix(MV)`):
     $$N_{3\times 3} = \left((V \cdot M)_{3\times 3}^{-1}\right)^T$$

3. **Clip Space, NDC & Depth Metric Convention**:
   - **Perspective Projection**: $w_{\text{clip}} = +z_{\text{view}}$. The rasterizer interpolates reciprocal depth $1 / z_{\text{view}} > 0$ in `local_zb` (larger value = closer to camera; `local_zb == 0.0f` = empty background).
   - **Orthographic Projection**: $w_{\text{clip}} = 1.0$, $z_{\text{ndc}} = \frac{2 z_{\text{view}} - (f + n)}{f - n} \in [-1, +1]$. The rasterizer maps visible orthographic depth to $\frac{f - z_{\text{view}}}{f - n} \in (0, 1]$ (or $1 / z_{\text{view}} > 0$) so that `local_zb > 0.0f` and `z_new > z_old` (`DepthOp::Less`) hold identically for both Perspective and Orthographic cameras.

4. **Terminal Cell Space, Sub-Pixel Space & Physical Aspect Ratio Formula**:
   - **Terminal Cell Grid**: Origin `(0, 0)` at the **top-left character cell** (`ANSI \x1b[1;1H`), `cell_x` $\in [0, \text{term\_w} - 1]$ (`+X` right), `cell_y` $\in [0, \text{term\_h} - 1]$ (`+Y` down).
   - **Sub-Pixel Resolution per Blitter**: Given sub-pixel multiplier $(S_x, S_y)$ for the active `BlitterType`:
     - `Braille`: $(2, 4)$
     - `HalfBlock`: $(1, 2)$
     - `Quadrant`: $(2, 2)$
     - `Sextant`: $(2, 3)$
     - `ASCII`: $(1, 1)$
     The sub-pixel framebuffer resolution is $\text{PixW} = \text{term\_w} \times S_x$ and $\text{PixH} = \text{term\_h} \times S_y$.
   - **Physical Camera Aspect Ratio (Decoupled from Blitter Sub-Pixel Density)**:
     Standard monospace terminal font cells have a physical pixel width-to-height ratio of $\alpha_{\text{cell}} = W_{\text{cell}} / H_{\text{cell}} \approx 0.5$ (`1:2`). Therefore, the true physical viewport aspect ratio is **always**:
     $$\text{Aspect} = \frac{\text{term\_w} \times 0.5}{\text{term\_h}} = \frac{\text{term\_w}}{2.0 \times \text{term\_h}}$$
     Because $\text{Aspect}$ depends only on physical cell dimensions (`term_w`, `term_h`) and **not** on $(S_x, S_y)$, switching at runtime between `Braille` ($2\times 4$), `HalfBlock` ($1\times 2$), `Quadrant` ($2\times 2$), `Sextant` ($2\times 3$), and `ASCII` ($1\times 1$) never stretches or squashes 3D geometry.

---

## 3. Repository Directory Structure

```text
TVK-Engine/
├── CMakeLists.txt                     # Cross-platform CMake build (Linux, macOS, Windows; SIMD ON/OFF; CTest)
├── LICENSE                            # MIT License
├── README.md                          # Project overview, architecture highlights & quickstart
├── ROADMAP.md                         # Master phased refactoring & implementation plan (Phases 0–7)
├── docs/
│   ├── ARCHITECTURE.md                # Full 10-layer system architecture blueprint (this document)
│   ├── CODEBASE_AUDIT.md              # Multi-pass forensic audit & catalog of 32 defects (B-01..B-32)
│   └── specs/                         # Granular subsystem implementation specifications
│       ├── 01_CORE_AND_PLATFORM.md    # Spec 01: Core primitives, JobSystem, PAL, TerminalGuard, Input
│       ├── 02_MATH_AND_RASTERIZER.md  # Spec 02: Math, SIMD, Camera, SortKey, Clipper & Tiled Rasterizer
│       ├── 03_PRESENTER_AND_CANVAS2D.md # Spec 03: TerminalCell, 5 Blitters, Bayer Dither, ANSI Diff, Canvas2D
│       ├── 04_ECS_AND_SCENE.md        # Spec 04: Generational Entity, Sparse-Set Pool, View, Scene Graph
│       ├── 05_PHYSICS_AND_PICKING.md  # Spec 05: Colliders, Spatial Hash, Impulse Solver, Mouse RayPicking
│       ├── 06_ASSETS_UI_AND_AUDIO.md  # Spec 06: AssetManager, .OBJ/.MTL, Textures, Immediate TUI, Synth Audio
│       └── 07_TESTING_AND_BENCHMARKS.md # Spec 07: TestHarness, Unit/Golden Tests, SIMD Matrix & Benchmarks
├── include/
│   └── tvk/
│       ├── TVK.h                      # Umbrella convenience header
│       ├── core/                      # Layer 0: Core Foundation
│       │   ├── Types.h                # Fundamental types (Color, QualityPreset, BlitterType, Vec2/Vec4)
│       │   ├── Handle.h               # Generational Handle<Tag> (16b idx + 16b gen) & HandlePool<T, Tag>
│       │   ├── Result.h               # Zero-exception ErrorCode & Result<T, E>
│       │   ├── Assert.h               # Crash-safe TVK_ASSERT / TVK_VERIFY (restores terminal before abort)
│       │   ├── Stats.h                # Frame telemetry & profiling counters (FrameStats)
│       │   ├── JobSystem.h            # Lock-free atomic work queues, thread pool & ParallelFor
│       │   ├── Memory.h               # [Phase 1 Spec] FrameLinearArena, PoolAllocator
│       │   ├── Time.h                 # [Phase 1 Spec] Clock, TimestepAccumulator (with alpha)
│       │   ├── EventBus.h             # [Phase 1 Spec] Typed synchronous/queued event dispatcher
│       │   └── Log.h                  # [Phase 1 Spec] Ring-buffer logger (never pollutes stdout!)
│       ├── platform/                  # Layer 1: Platform Abstraction Layer (PAL)
│       │   ├── PAL.h                  # TTY raw mode setup/restore, SIGWINCH, FastPrint, timing
│       │   ├── TerminalGuard.h        # [Phase 1 Spec] RAII terminal mode guard & signal crash recovery
│       │   └── Input.h                # [Phase 1 Spec] Keyboard (CSI / Kitty) & SGR 1006 Mouse state machine
│       ├── math/                      # Layer 2: Linear Algebra, Volumes & SIMD Kernels
│       │   ├── Math.h                 # Vec2, Vec3, Vec4, Mat3, Mat4, Quat, AABB, Plane, Frustum, Ray, ONB
│       │   └── MathSIMD.h             # Guarded AVX2/FMA 8-wide kernels + portable scalar fallback
│       ├── renderer/                  # Layer 3: 3D Software Rasterizer & Terminal Presentation
│       │   ├── Camera.h               # Left-handed +Z Perspective & Orthographic cameras, Orbit & LookAt
│       │   ├── CommandBuffer.h        # Render command recording, PipelineState, Light, PointLight
│       │   ├── Fog.h                  # Depth fog descriptor
│       │   ├── Mesh.h                 # Vertex, Mesh, local-space AABB caching
│       │   ├── Primitives.h           # Procedural Sphere, Cylinder, Capsule, Ground (+ Box/Cone/Torus)
│       │   ├── Rasterizer.h           # Near-plane clipping, guard-band, AVX2/Scalar scanline + 3D line DDA
│       │   ├── Renderer.h             # Multi-threaded 4-phase rendering pipeline coordinator
│       │   └── TerminalPresenter.h    # 5 Blitters (Braille 2-color, HalfBlock, Quadrant, Sextant, ASCII) + DECSET 2026
│       ├── scene/                     # Layer 4: Entity-Component-System & Scene Graph [Phase 4 Spec]
│       ├── physics/                   # Layer 5: 3D/2.5D Collision, Physics & Picking [Phase 5 Spec]
│       ├── ui/                        # Layer 6: 2D Terminal Canvas & Immediate-Mode TUI [Phase 3/6 Spec]
│       ├── assets/                    # Layer 7: Asset Pipeline & Resource Management [Phase 6 Spec]
│       ├── audio/                     # Layer 8: Retro Synth & Audio Mixer [Phase 7 Spec]
│       └── app/                       # Layer 9: Engine Runtime & Application Host [Phase 1/3/4 Spec]
├── src/                               # Subsystem implementations mirroring include/tvk/ 1-to-1
├── examples/                          # Demo & showcase executables (Sandbox.cpp, Asteroids_Demo.cpp)
└── tests/                             # CTest automated verification suite (TestHarness.h, test_core, test_math, test_renderer)
```

---

## 4. End-to-End Engine Frame Lifecycle

Every frame inside `tvk::Engine::Run()` executes a deterministic, non-blocking 6-stage pipeline:

```text
┌────────────────────────────────────────────────────────────────────────────┐
│ 1. Platform & Input Poll (tvk::platform)                                   │
│    • Poll SIGWINCH / Console buffer resize -> emit WindowResizeEvent       │
│    • Drain non-blocking stdin (4 KB buffer) -> parse CSI, Kitty & SGR 1006 │
│    • Update Input state (KeyDown, KeyPressed, KeyReleased, MousePos/Wheel) │
└────────────────────────────────────┬───────────────────────────────────────┘
                                     ▼
┌────────────────────────────────────────────────────────────────────────────┐
│ 2. Fixed-Timestep Simulation Loop (Gaffer on Games Accumulator)            │
│    • Clamp frameDt <= 0.25s (max 8 substeps to prevent spiral of death)    │
│    • while (accumulator >= fixed_dt):                                      │
│        - Snapshot prevTransform = currTransform                            │
│        - app.OnFixedUpdate(fixed_dt)                                       │
│        - PhysicsWorld::Step(fixed_dt) (Broadphase -> Narrowphase -> Solve) │
│        - Flush deferred EntityCommandBuffer & emit CollisionEvents         │
│    • Compute interpolation factor: alpha = accumulator / fixed_dt          │
└────────────────────────────────────┬───────────────────────────────────────┘
                                     ▼
┌────────────────────────────────────────────────────────────────────────────┐
│ 3. Variable-Timestep Gameplay & Scene Update                               │
│    • app.OnUpdate(dt)                                                      │
│    • Scene::UpdateHierarchy() — topological parent-first world transforms  │
│    • Scene::ExtractRenderCommands(cmdBuffer, alpha) + app.OnRender(cmd)    │
└────────────────────────────────────┬───────────────────────────────────────┘
                                     ▼
┌────────────────────────────────────────────────────────────────────────────┐
│ 4. Parallel 3D Software Rendering Pipeline (tvk::Renderer)                 │
│    • Task Setup: Sort commands, frustum-cull AABBs, compute MV & NormalMat │
│    • Phase 1 (Workers): SoA AVX2/Scalar Vertex & Normal transform + RGB    │
│                         multi-light diffuse & Blinn-Phong specular         │
│    • Phase 2 (Workers): Dynamic near-plane clip, Perspective/Ortho project,│
│                         guard-band clamp, 64-bit area cull, atomic tile bin│
│    • Phase 3 (Workers): L1 tile sort (DepthOp-aware), top-left biased      │
│                         AVX2/Scalar scanline + 3D line DDA rasterization   │
│                         -> Blit tile into 16B TerminalCell BackBuffer      │
└────────────────────────────────────┬───────────────────────────────────────┘
                                     ▼
┌────────────────────────────────────────────────────────────────────────────┐
│ 5. 2D Canvas & Immediate-Mode UI Compositing (tvk::ui)                     │
│    • app.OnUI(canvas2D, uiContext)                                         │
│    • DebugOverlay::Render(canvas2D, frameStats) [if F1 active]             │
│    • Composite UTF-8 text, box borders, progress bars & widgets directly   │
│      into TerminalCell BackBuffer (preserving 3D background in transparent │
│      text cells)                                                           │
└────────────────────────────────────┬───────────────────────────────────────┘
                                     ▼
┌────────────────────────────────────────────────────────────────────────────┐
│ 6. Parallel Differential ANSI Encoding & Synchronized Atomic Present       │
│    • Phase 4 (Workers): Diff BackBuffer vs FrontBuffer per row slice       │
│                         Emit exact 24-bit FG+BG RGB & UTF-8 glyphs         │
│    • FlushOutput(): Frame with DECSET 2026 (\x1b[?2026h ... \x1b[?2026l)   │
│                     and flush in a single pal::FastPrint() syscall         │
└────────────────────────────────────────────────────────────────────────────┘
```

---

## 5. Detailed Subsystem Specifications (`Layer 0` through `Layer 9`)

### 5.1 Layer 0: Core Foundation (`tvk/core`)
- **`Types.h`**: Defines `Color` (RGBA8 with modulation/clamping helpers), `QualityPreset` (`Low`, `Medium`, `High`), `BlitterType` (`Braille`, `HalfBlock`, `Quadrant`, `Sextant`, `ASCII` + legacy aliases), and portable `TVK_CPU_PAUSE()`.
- **`Handle.h`**: Generational `Handle<Tag>` (`16-bit index` + `16-bit generation`) and `HandlePool<T, Tag>` providing $O(1)$ slot allocation, deallocation, and stale-handle detection across all engine resources.
- **`Result.h` & `Assert.h`**: Zero-exception `Result<T, ErrorCode>` (`[[nodiscard]]`) and crash-safe `TVK_ASSERT(expr, msg)` that calls `tvk::pal::RestoreTerminal()` before logging file/line diagnostics and aborting.
- **`JobSystem.h`**: Fixed worker thread pool (`std::max(1, numThreads)`), portable `CpuRelax()`, mutex-protected task context publication, `ExecuteGrid`, and `ParallelFor(count, grainSize, fn)`.
- **`Memory.h`, `Time.h`, `EventBus.h`, `Log.h` (Phase 1)**: 64-byte aligned `FrameLinearArena`, `TimestepAccumulator` with render interpolation `alpha`, typed `EventBus`, and 256-entry ring-buffer `Log`.
- *Full specification*: [`docs/specs/01_CORE_AND_PLATFORM.md`](specs/01_CORE_AND_PLATFORM.md).

### 5.2 Layer 1: Platform Abstraction Layer (`tvk/platform`)
- **`PAL.h` & `TerminalGuard.h`**:
  - Idempotent POSIX `termios` raw mode (`~(ICANON | ECHO | IEXTEN | ISIG)`, `VMIN = 0, VTIME = 0`) and Win32 Console mode save/restore.
  - Async-signal-safe `sigaction` handlers (`SIGINT`, `SIGTERM`, `SIGABRT`, `SIGSEGV`) that restore `termios` and write the raw ANSI reset string (`"\x1b[?1006l\x1b[?1002l\x1b[?1000l\x1b[0m\x1b[?25h\x1b[?2026l\x1b[?1049l"`) via `::write(STDOUT_FILENO, ...)`, plus `SIGWINCH` resize flag tracking (`pal::ConsumeResizeEvent()`).
- **`Input.h` (Phase 1)**:
  - 4 KB buffered non-blocking `stdin` reader feeding a deterministic finite-state machine for ANSI CSI sequences, Kitty `\x1b[>1u` key-release events, 180–350 ms key-release decay timer fallback, and SGR 1006 mouse tracking.
- *Full specification*: [`docs/specs/01_CORE_AND_PLATFORM.md`](specs/01_CORE_AND_PLATFORM.md).

### 5.3 Layer 2: Math, Geometry & SIMD Kernels (`tvk/math`)
- **`Math.h`**: Unified `Vec2` (`x, y` and `u, v`), `Vec3`, `Vec4`, `Mat3`, `Mat4`, `Quat`, `AABB`, `Plane`, `Frustum`, `Ray`, `Mat4NormalMatrix(const Mat4&)` (analytical 3×3 cofactor inverse-transpose), and `BuildOrthonormalBasis(Vec3 axis, Vec3& outRight, Vec3& outForward)` (Duff et al. branchless ONB).
- **`MathSIMD.h`**: Guarded `#if defined(TVK_SIMD_AVX2) && defined(__AVX2__)` 8-wide FMA kernels (`NormalizeFast`, `TransformVerticesAVX2`, `TransformNormalsAVX2`) with zero-length normal masking (`dot > 1e-12f`) and bit-compatible scalar fallbacks.
- *Full specification*: [`docs/specs/02_MATH_AND_RASTERIZER.md`](specs/02_MATH_AND_RASTERIZER.md).

### 5.4 Layer 3: 3D Software Renderer & Multi-Blitter Presenter (`tvk/renderer`)
- **`Camera`**: Left-handed `+Z` forward `LookAt`, `Orbit` (with immediate `UpdateCache()`), Perspective & Orthographic projection matrices, and physical cell aspect ratio $\text{Aspect} = \frac{\text{term\_w}}{2.0 \times \text{term\_h}}$.
- **`Renderer` & `Rasterizer`**:
  - Per-vertex color modulation (`Vertex::color * cmd.color`), 3×3 normal matrix transformation, and full RGB multi-light diffuse + Blinn-Phong specular accumulation.
  - Oriented `DrawCylinder` and `DrawCapsule` (with hemisphere end-caps) and tile-binned depth-tested 3D line rasterization (`CommandType::DrawLine`).
  - Dynamic `camera.GetNear()` clipping, 2D guard-band clamping (`[-16384, +16384]`), `int64_t` signed area backface culling, top-left fill convention bias (`-1`), and `DepthOp::Always` order preservation.
- **`TerminalPresenter`**:
  - Thread-safe `constexpr` byte-to-decimal LUT, 5 runtime-switchable blitters (`Braille` 24-bit 2-color FG+BG luminance clustering without black pinholes, `HalfBlock`, `Quadrant`, `Sextant`, `ASCII`), exact front-buffer `fg`/`bg` diffing, and `DECSET 2026` synchronized output framing.
- *Full specifications*: [`docs/specs/02_MATH_AND_RASTERIZER.md`](specs/02_MATH_AND_RASTERIZER.md) and [`docs/specs/03_PRESENTER_AND_CANVAS2D.md`](specs/03_PRESENTER_AND_CANVAS2D.md).

### 5.5 Layer 4: Scene & Entity-Component-System (`tvk/scene`)
- **64-Bit Generational `Entity`**: 32-bit dense/sparse index + 32-bit generation counter (`kNullEntity = 0`).
- **Paged Sparse-Set `ComponentPool<T>`**: 1024-element sparse pages allocated on demand + packed `m_DenseEntities` and `m_DenseComponents` arrays supporting $O(1)$ `Emplace`, swap-and-pop `Remove`, and `Get`.
- **Lead-Pool `View<Ts...>` & `EntityCommandBuffer`**: Multi-component views iterate the smallest component pool first; structural mutations during iteration are recorded into a deferred command buffer.
- **`TransformSystem` & `RenderSystem`**: Topological parent-before-child world transform propagation and render command extraction with fixed-timestep interpolation factor $\alpha$.
- *Full specification*: [`docs/specs/04_ECS_AND_SCENE.md`](specs/04_ECS_AND_SCENE.md).

### 5.6 Layer 5: Physics, Collision & 3D Terminal Mouse Picking (`tvk/physics`)
- **Colliders & RigidBody**: `SphereCollider`, `AABBCollider`, `OBBCollider`, `CapsuleCollider`, 16-bit collision layer/mask filtering, and `RigidBodyComponent` (`Static`, `Kinematic`, `Dynamic`).
- **Arena-Allocated `SpatialHashGrid` Broadphase & Analytical Narrowphase**: Zero-heap-churn broadphase hashing and analytical contact manifold generation (`Sphere-Sphere`, `AABB-AABB`, `Sphere-AABB`, `Capsule-Sphere`, `Capsule-AABB`, `OBB-OBB`).
- **Sequential Impulse Solver & `ScreenCellToRay`**: 8-iteration impulse solver with Baumgarte stabilization ($\beta = 0.2$), `CharacterController` (slide & step-up), and terminal mouse cell-to-world ray unprojection (`PhysicsWorld::Raycast`).
- *Full specification*: [`docs/specs/05_PHYSICS_AND_PICKING.md`](specs/05_PHYSICS_AND_PICKING.md).

### 5.7 Layer 6: 2D Terminal Canvas & Immediate-Mode TUI (`tvk/ui`)
- **`Canvas2D`**: Cell-space compositor writing UTF-8 text (`BgMode::Transparent` preserves underlying 3D cell background color), Unicode box borders (`Single`, `Double`, `Rounded`, `Heavy`, `ASCII`), progress bars, and sparklines directly into `m_BackBuffer`.
- **`UIContext` & `Theme`**: Immediate-mode widget toolkit (`BeginPanel`, `Label`, `Button`, `Checkbox`, `SliderFloat`, `ProgressBar`, `ListBox`) driven by an FNV-1a ID stack (`PushID`/`PopID`), `hotId`/`activeId`/`focusedId` state machine, SGR 1006 mouse hit-testing, and keyboard `Tab`/`Arrow`/`Enter` navigation.
- *Full specifications*: [`docs/specs/03_PRESENTER_AND_CANVAS2D.md`](specs/03_PRESENTER_AND_CANVAS2D.md) and [`docs/specs/06_ASSETS_UI_AND_AUDIO.md`](specs/06_ASSETS_UI_AND_AUDIO.md).

### 5.8 Layer 7: Asset Pipeline & Resource Management (`tvk/assets`)
- **`AssetManager`**: Centralized lifetime registry backed by `HandlePool<Mesh, MeshTag>`, `HandlePool<Texture, TextureTag>`, and `HandlePool<Material, MaterialTag>`.
- **Zero-Dependency Wavefront `.obj` / `.mtl` Parser (`ObjLoader`)**: Supports `v`, `vt`, `vn`, vertex colors, relative/negative face indices, $N$-gon fan triangulation, and automatic flat/smooth normal generation.
- **`Texture` & Procedural Generators**: RGBA8 storage with `Nearest`/`Bilinear` UV sampling, perspective-correct $(u/w, v/w)$ scanline interpolation, and built-in procedural generators (`CreateCheckerboard`, `CreateBrick`, `CreateNoise`, `CreateGradient`).
- *Full specification*: [`docs/specs/06_ASSETS_UI_AND_AUDIO.md`](specs/06_ASSETS_UI_AND_AUDIO.md).

### 5.9 Layer 8: Retro Synth & Audio Subsystem (`tvk/audio`)
- **Zero-Dependency OS Audio Backend**: Dynamic `dlopen("libasound.so.2")` (ALSA) / PulseAudio on Linux, `WinMM` (`waveOutWrite`) on Windows, `AudioQueue` on macOS, and automatic `NullAudioBackend` fallback on headless/SSH terminals.
- **Procedural Chiptune/SFX Synthesizer (`SynthPatch`)**: Real-time oscillator mixer (`Square`, `Pulse25`, `Triangle`, `Sawtooth`, `WhiteNoise`, `LFSRNoise` with frequency slide, vibrato, and 4-stage `ADSR` envelope) plus 16-bit PCM `.wav` playback and 3D distance attenuation/stereo panning.
- *Full specification*: [`docs/specs/06_ASSETS_UI_AND_AUDIO.md`](specs/06_ASSETS_UI_AND_AUDIO.md).

### 5.10 Layer 9: Engine Runtime & Application Host (`tvk/app`)
- **`EngineConfig`**: Declarative startup configuration (`targetFps = 60`, `fixedTimestep = 1.0f / 60.0f`, `blitter = BlitterType::Braille`, `numWorkers = 0` auto, `headless = false`, `fallbackWidth = 120`, `fallbackHeight = 40`, `frameArenaBytes = 4 * 1024 * 1024`).
- **`Application`**: Base game interface providing `OnInit(Engine&)`, `OnFixedUpdate(float fixedDt)`, `OnUpdate(float dt)`, `OnRender(CommandBuffer&, float alpha)`, `OnUI(Canvas2D&, UIContext&)`, and `OnShutdown()`.
- **`Engine`**: Owns and coordinates `TerminalGuard`, `Input`, `JobSystem`, `FrameLinearArena`, `Renderer`, `Scene`, `PhysicsWorld`, `AssetManager`, `UIContext`, `AudioEngine`, and `DebugOverlay`.
- **`DebugOverlay`**: Built-in `F1`-toggleable profiler HUD displaying FPS, 60-frame timing sparkline, per-phase CPU breakdown, triangle/tile/ANSI byte counters, active blitter (`F2` hot-swap), and recent `Log` ring-buffer entries.
- *Full specifications*: [`docs/specs/01_CORE_AND_PLATFORM.md`](specs/01_CORE_AND_PLATFORM.md) and [`docs/specs/03_PRESENTER_AND_CANVAS2D.md`](specs/03_PRESENTER_AND_CANVAS2D.md).

---

## 6. Memory Budget & Cache Alignment Architecture

### 6.1 Default Engine Memory Footprint ($< 16\text{ MB}$ Total)

Whereas the original legacy codebase allocated **~4.5 GB** at startup on a 16-thread CPU (`B-02`), the refined TVK Engine architecture bounds its default memory footprint to **$< 16\text{ MB}$** (assuming an 8-worker thread pool and `160×50` terminal):

| Subsystem / Arena | Sizing Formula (Default Config) | Footprint | Allocation Policy |
| :--- | :--- | :--- | :--- |
| **Per-Worker Tile Stack Buffers** (`local_fb` + `local_zb`) | $8\text{ threads} \times (64\times 64 \times 4\text{ B} + 64\times 64 \times 4\text{ B})$ | `256 KB` | Thread stack (L1 cache resident: `32 KB`/worker) |
| **Per-Worker `SIMDWorkingBuffer`** | $8\text{ threads} \times \text{sizeof(SIMDWorkingBuffer)}$ (`~36 KB`) | `288 KB` | Pre-allocated once in `Renderer::ResizeBuffers` |
| **Transformed Vertex Arena** (`m_FrameVertices`) | $65,536\text{ verts} \times 32\text{ B}$ (`ShadeVertex`) | `2.00 MB` | Pre-allocated contiguous vector, reset per frame |
| **Projected Triangle Arenas** (`m_ThreadTriArenas`) | $4\text{–}8\text{ threads} \times 16,384\text{–}65,536 \times \text{sizeof(ProjectedTriangle)}$ | `4.00–8.00 MB` | Pre-allocated per-worker arenas, reset in $O(1)$ |
| **Global Tile Bin Node Arena** (`m_GlobalNodeArena`) | `desc.MaxNodesPerThread` (`262,144`) $\times 8\text{ B}$ (`TriNode`) | `2.00 MB` | Pre-allocated contiguous vector, atomic bump counter |
| **Terminal Cell Buffers** (`m_FrontBuffer` + `m_BackBuffer`) | $2 \times (160 \times 50\text{ cells}) \times 16\text{ B}$ (`TerminalCell`) | `256 KB` | Resized only on `SIGWINCH` terminal resize |
| **Pre-Allocated ANSI Output Buffers** (`m_ThreadOutputs` + `m_FinalOutput`) | $8 \times 64\text{ KB} + 512\text{ KB}$ | `1.00 MB` | Reused every frame (`clear()` without deallocating) |
| **Core `FrameLinearArena`** (ECS / Physics / UI transient data) | `EngineConfig::frameArenaBytes` | `2.00 MB` | Single 64-byte aligned bump arena reset per frame |
| **Total Default Engine Memory Footprint** | — | **~11.8 MB ($< 16\text{ MB}$)** | **Zero hot-path heap allocations during frame execution** |

### 6.2 Cache-Line (`64 B`) & SIMD (`32 B`) Alignment Rules

| Structure | Header Location | `sizeof` | `alignof` | Rationale |
| :--- | :--- | :--- | :--- | :--- |
| `AtomicQueue` | `tvk/core/JobSystem.h` | `64 B` | `alignas(64)` | Prevents false sharing between atomic work index and adjacent fields across cores. |
| `SIMDWorkingBuffer` | `tvk/renderer/Renderer.h` | `36,864 B` | `alignas(32)` | Enables aligned 256-bit `_mm256_load_ps` / `_mm256_store_ps` AVX2 vector operations. |
| `TerminalCell` | `tvk/renderer/TerminalPresenter.h` | `16 B` | `alignas(16)` | Four cells pack exactly into one 64-byte CPU cache line; enables 128-bit equality diffing. |
| `Handle<Tag>` | `tvk/core/Handle.h` | `4 B` | `alignas(4)` | Fits in a single 32-bit register (`16b index | 16b generation`). |
| `Entity` | `tvk/scene/Entity.h` | `8 B` | `alignas(8)` | Fits in a single 64-bit register (`32b index | 32b generation`). |

---

## 7. Error Handling, Assertions & `-fno-exceptions` Contract

TVK Engine compiles with C++ exceptions disabled (`-fno-exceptions` on GCC/Clang, `/EHs-c- /D_HAS_EXCEPTIONS=0` on MSVC) to eliminate unwind table bloat and ensure deterministic frame times.

1. **Recoverable Runtime Errors (`tvk::Result<T, ErrorCode>` in `include/tvk/core/Result.h`)**:
   - All fallible I/O, parsing, and initialization operations (such as `.obj` file loading, texture decoding, or terminal capability queries) return `[[nodiscard]] Result<T, ErrorCode>` (`ErrorCode::{Ok, InvalidArgument, FileNotFound, ParseError, OutOfMemory, TerminalError, Unsupported}`).
   - Callers inspect `.IsOk()` / `.IsErr()` and retrieve values via `.Value()` or `.ValueOr(fallback)` without `try`/`catch`.
2. **Unrecoverable Invariant Violations (`TVK_ASSERT` / `TVK_VERIFY` in `include/tvk/core/Assert.h`)**:
   - If an internal engine invariant fails while the terminal is in raw mode and alternate screen buffer (`\x1b[?1049h`), calling `std::abort()` directly would leave the user's shell corrupted (hidden cursor, disabled echo, mouse escape codes spewing on mouse movement).
   - `TVK_ASSERT(expr, msg)` **always calls `tvk::pal::RestoreTerminal()` first** (restoring `termios` and emitting `\x1b[?1006l\x1b[?1002l\x1b[?1000l\x1b[0m\x1b[?25h\x1b[?2026l\x1b[?1049l`), then prints the formatted assertion failure (`expr`, message, `__FILE__`, `__LINE__`) to `stderr`, and calls `std::abort()`.

---

## 8. Index of Granular Technical Specifications (`docs/specs/`)

Each phase of the roadmap is backed by a self-contained, implementation-ready technical specification in `docs/specs/`:

| Spec # | Document Path | Covered Layers & Subsystems | Target Roadmap Phase |
| :--- | :--- | :--- | :--- |
| **Spec 01** | [`docs/specs/01_CORE_AND_PLATFORM.md`](specs/01_CORE_AND_PLATFORM.md) | `tvk/core` (`Handle`, `Result`, `Assert`, `FrameLinearArena`, `PoolAllocator`, `JobSystem::ParallelFor`, `TimestepAccumulator`, `EventBus`, `Log`), `tvk/platform` (`TerminalGuard` async-signal-safe teardown, ANSI CSI / Kitty `\x1b[>1u` / SGR 1006 mouse state machine), `tvk/app` (`Engine`, `Application`). | **Baseline (M1) & Phase 1** |
| **Spec 02** | [`docs/specs/02_MATH_AND_RASTERIZER.md`](specs/02_MATH_AND_RASTERIZER.md) | `tvk/math` (`Math.h` Left-Handed `+Z` `LookAt` & `Orthographic`, `Mat4NormalMatrix`, Duff et al. ONB, `MathSIMD.h` AVX2 + scalar fallback), `tvk/renderer` (`SortKey`, dynamic near-plane & guard-band clipping, `int64_t` area, top-left fill bias, $1/w$ interpolation, full RGB multi-light shading, 3D line DDA). | **Baseline (M1) & Phase 2** |
| **Spec 03** | [`docs/specs/03_PRESENTER_AND_CANVAS2D.md`](specs/03_PRESENTER_AND_CANVAS2D.md) | `tvk/renderer` (16-byte `TerminalCell`, `Braille` 2×4 2-color FG+BG clustering, `HalfBlock` 1×2, `Quadrant` 2×2, `Sextant` 2×3, `ASCII` 1×1, 4×4 Bayer dithering, OKLab/256-color LUT, exact frontbuffer diffing, `DECSET 2026`), `tvk/ui` (`Canvas2D`), `tvk/app` (`DebugOverlay`). | **Baseline (M1) & Phase 3** |
| **Spec 04** | [`docs/specs/04_ECS_AND_SCENE.md`](specs/04_ECS_AND_SCENE.md) | `tvk/scene` (64-bit generational `Entity`, paged sparse-set `ComponentPool<T>`, smallest-pool lead `View<Ts...>`, deferred `EntityCommandBuffer`, topological parent-before-child `TransformSystem`, interpolated `RenderSystem` with $\alpha$). | **Phase 4** |
| **Spec 05** | [`docs/specs/05_PHYSICS_AND_PICKING.md`](specs/05_PHYSICS_AND_PICKING.md) | `tvk/physics` (`Collider` types, `RigidBodyComponent`, arena-allocated `SpatialHashGrid`, analytical narrowphase manifolds for Sphere/AABB/Capsule/OBB, 8-iteration sequential impulse solver with Baumgarte stabilization, `CharacterController`, `ScreenCellToRay` & `PhysicsWorld::Raycast`). | **Phase 5** |
| **Spec 06** | [`docs/specs/06_ASSETS_UI_AND_AUDIO.md`](specs/06_ASSETS_UI_AND_AUDIO.md) | `tvk/assets` (`AssetManager` with `Handle<Tag>`, zero-dependency `.obj`/`.mtl` parser grammar, `Texture` nearest/bilinear sampling & procedural generators), `tvk/ui` (immediate-mode `UIContext` ID stack & box layout), `tvk/audio` (OS audio backends + procedural `SynthPatch` ADSR synthesizer). | **Phases 6 & 7** |
| **Spec 07** | [`docs/specs/07_TESTING_AND_BENCHMARKS.md`](specs/07_TESTING_AND_BENCHMARKS.md) | `tests/` (`TestHarness.h`, unit & golden-frame test suites across all layers, `-DTVK_ENABLE_SIMD=ON/OFF` equivalence verification, and quantitative frame-time & memory benchmark budgets). | **Baseline (M1) & Phases 1–7** |
