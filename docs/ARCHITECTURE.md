# TVK Engine — Core Architecture Blueprint

**TVK Engine (Terminal Vulkan Engine)** is a high-performance, zero-dependency C++17 game engine designed to build real-time 3D, 2.5D, and 2D games that run directly inside modern command-line terminals at 60+ FPS.

---

## 1. Architectural Philosophy & Design Principles

1. **Data-Oriented & Cache-Bounded by Default**:
   - Framebuffers and Z-buffers are processed inside **64×64 pixel tiles** (16 KB color + 16 KB depth = **32 KB per worker thread**), fitting entirely inside CPU L1 data cache.
   - Entity-Component-System (ECS) storage uses contiguous sparse-set arrays (`ComponentPool<T>`) for linear cache-line iteration.
2. **Strict Layered Modularity**:
   - Subsystems are organized into 10 strictly ordered architectural layers (`Layer 0` through `Layer 9`). Lower layers never depend on higher layers.
3. **Zero-Allocation Hot Paths**:
   - Per-frame transient allocations (transformed vertices, projected triangles, tile bin nodes, UI draw commands) use pre-sized, bounded per-frame linear arenas reset in $O(1)$ at `BeginFrame()`.
4. **Terminal-First Compositing (3D + 2D + TUI Unified Pipeline)**:
   - Unlike naive terminal renderers where `printf` HUD calls fight with 3D ANSI output and cause flickering, TVK composites the 3D rasterized sub-pixel buffer, 2D sprites/shapes, and crisp native UTF-8 text/UI widgets into a unified `TerminalCell` backbuffer *before* performing a single parallel differential ANSI encode and atomic `write()`/`WriteFile()` syscall.
5. **Cross-Platform & Graceful Degradation**:
   - Native POSIX (`termios`, `ioctl`, `poll`, `SIGWINCH`, `SIGINT`) and Win32 Console (`WriteFile`, Virtual Terminal Processing, Console Input Events) backends.
   - SIMD kernels (`AVX2`/`FMA`) are paired with portable scalar/NEON fallbacks so the engine compiles and runs on any CPU architecture (`x86_64`, `ARM64` / Apple Silicon).

---

## 2. Layered Architecture & Dependency Graph

```
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
│          Types, JobSystem, FrameArena, EventBus, Time, Stats, Assert      │
└───────────────────────────────────────────────────────────────────────────┘
```

### Dependency Rules
- `tvk/core` depends only on the C++17 standard library.
- `tvk/platform` and `tvk/math` depend only on `tvk/core`.
- `tvk/renderer` depends on `tvk/core`, `tvk/platform`, and `tvk/math`.
- `tvk/scene`, `tvk/physics`, `tvk/ui`, `tvk/assets`, and `tvk/audio` build on `tvk/core`, `tvk/math`, `tvk/platform`, and `tvk/renderer`.
- `tvk/app` orchestrates all lower layers and exposes the unified game developer API.

---

## 3. Repository Directory Structure

```text
TVK-Engine/
├── CMakeLists.txt                     # Cross-platform build configuration (Linux/macOS/Windows)
├── LICENSE                            # MIT License
├── README.md                          # Project overview & quickstart guide
├── ROADMAP.md                         # Master phased refactoring & implementation plan
├── docs/
│   ├── ARCHITECTURE.md                # Full system architecture blueprint (this document)
│   └── CODEBASE_AUDIT.md              # Forensic audit of legacy code & bug catalog
├── include/
│   └── tvk/
│       ├── TVK.h                      # Umbrella convenience header
│       ├── core/                      # Layer 0: Core Foundation
│       │   ├── Types.h                # Fundamental types (Color, Pixel, Vec2, Vec4, BlitterType)
│       │   ├── Stats.h                # Frame telemetry & profiling counters
│       │   ├── JobSystem.h            # Lock-free atomic work queues & thread pool
│       │   ├── Memory.h               # [Phase 1] FrameLinearArena, PoolAllocator
│       │   ├── Time.h                 # [Phase 1] Clock, TimestepAccumulator
│       │   ├── EventBus.h             # [Phase 1] Typed synchronous/queued event dispatcher
│       │   └── Log.h                  # [Phase 1] Ring-buffer logger (never pollutes stdout!)
│       ├── platform/                  # Layer 1: Platform Abstraction Layer (PAL)
│       │   ├── PAL.h                  # Low-level TTY setup/restore, size query, FastPrint, timing
│       │   ├── TerminalGuard.h        # [Phase 1] RAII terminal mode guard & signal crash recovery
│       │   └── Input.h                # [Phase 1] Keyboard & SGR Mouse state machine & CSI parser
│       ├── math/                      # Layer 2: Linear Algebra & SIMD Kernels
│       │   ├── Math.h                 # Vec2, Vec3, Vec4, Mat3, Mat4, Quat, AABB, Frustum, Ray
│       │   └── MathSIMD.h             # AVX2/FMA 8-wide kernels + portable scalar fallback
│       ├── renderer/                  # Layer 3: 3D Software Rasterizer & Terminal Presentation
│       │   ├── Camera.h               # Perspective & Orthographic cameras, Frustum & Ray unproject
│       │   ├── CommandBuffer.h        # Render command recording, PipelineState, Material
│       │   ├── Fog.h                  # Depth fog descriptor
│       │   ├── Mesh.h                 # Vertex, Mesh, AABB caching
│       │   ├── Primitives.h           # Procedural Sphere, Cylinder, Capsule, Box, Cone, Torus, Plane
│       │   ├── Rasterizer.h           # Near-plane clipping, projection, tiled AVX2 scanline + line raster
│       │   ├── Renderer.h             # Multi-threaded 4-phase rendering pipeline coordinator
│       │   └── TerminalPresenter.h    # Multi-blitter (Braille FG/BG, HalfBlock, Sextant, ASCII) + Diff ANSI
│       ├── scene/                     # Layer 4: Entity-Component-System & Scene Graph
│       │   ├── Entity.h               # [Phase 4] Generational Entity ID
│       │   ├── ComponentPool.h        # [Phase 4] Sparse-set contiguous component storage
│       │   ├── Registry.h             # [Phase 4] ECS Registry & multi-component View<T...>
│       │   ├── Components.h           # [Phase 4] Transform, Hierarchy, MeshRenderer, Camera, Light
│       │   └── Scene.h                # [Phase 4] Scene container & built-in ECS systems
│       ├── physics/                   # Layer 5: 3D/2.5D Collision & Physics
│       │   ├── Colliders.h            # [Phase 5] Sphere, AABB, OBB, Capsule colliders
│       │   ├── RigidBody.h            # [Phase 5] RigidBody dynamics state
│       │   ├── SpatialHash.h          # [Phase 5] Broadphase spatial hash grid
│       │   ├── Narrowphase.h          # [Phase 5] Analytical & GJK/EPA intersection manifolds
│       │   ├── PhysicsWorld.h         # [Phase 5] Impulse solver & character controller
│       │   └── Raycast.h              # [Phase 5] Ray queries & terminal mouse-to-world picking
│       ├── ui/                        # Layer 6: 2D Terminal Canvas & Immediate-Mode UI
│       │   ├── Canvas2D.h             # [Phase 3] UTF-8 text, boxes, bars, lines, 2D sprites on cell grid
│       │   ├── UIContext.h            # [Phase 6] Immediate-mode TUI widgets (Panel, Button, Slider)
│       │   └── Theme.h                # [Phase 6] Unicode box-drawing themes & color schemes
│       ├── assets/                    # Layer 7: Asset Pipeline & Resource Management
│       │   ├── Handle.h               # [Phase 6] Generational resource handles
│       │   ├── AssetManager.h         # [Phase 6] Resource registry & caching
│       │   ├── ObjLoader.h            # [Phase 6] Zero-dependency .obj/.mtl loader
│       │   └── Texture.h              # [Phase 6] 2D textures, UV sampling, procedural textures
│       ├── audio/                     # Layer 8: Audio Subsystem
│       │   ├── AudioEngine.h          # [Phase 7] Cross-platform audio mixer
│       │   └── Sound.h                # [Phase 7] WAV loader & procedural retro chiptune synth
│       └── app/                       # Layer 9: Engine Runtime & Application Host
│           ├── Engine.h               # [Phase 1/4] Master engine loop & subsystem coordinator
│           ├── Application.h          # [Phase 1/4] Base game class interface
│           ├── EngineConfig.h         # [Phase 1] Engine startup configuration
│           └── DebugOverlay.h         # [Phase 3] Toggleable F1 profiler & inspector HUD
├── src/                               # Mirrors include/tvk/ 1-to-1
│   ├── core/
│   ├── platform/
│   ├── math/
│   ├── renderer/
│   ├── scene/
│   ├── physics/
│   ├── ui/
│   ├── assets/
│   ├── audio/
│   └── app/
├── examples/                          # Showcase demos & sample games
│   ├── Sandbox.cpp
│   └── Asteroids_Demo.cpp
└── tests/                             # CTest unit, SIMD & headless golden-frame tests
```

---

## 4. End-to-End Engine Frame Lifecycle

Every frame inside `tvk::Engine::Run()` executes a deterministic, non-blocking pipeline:

```
┌────────────────────────────────────────────────────────────────────────────┐
│ 1. Platform & Input Poll (tvk::platform)                                   │
│    • Poll SIGWINCH / Console buffer resize -> emit WindowResizeEvent       │
│    • Drain non-blocking stdin -> parse ANSI CSI keys & SGR mouse events    │
│    • Update Input state (KeyDown, KeyPressed, KeyReleased, MousePos/Btn)   │
└────────────────────────────────────┬───────────────────────────────────────┘
                                     ▼
┌────────────────────────────────────────────────────────────────────────────┐
│ 2. Fixed-Timestep Simulation Loop (while accumulator >= fixed_dt)          │
│    • app.OnFixedUpdate(fixed_dt)                                           │
│    • PhysicsWorld::Step(fixed_dt):                                         │
│        Integrate velocities -> Broadphase -> Narrowphase -> Solve contacts │
│    • Emit CollisionEvents to EventBus                                      │
└────────────────────────────────────┬───────────────────────────────────────┘
                                     ▼
┌────────────────────────────────────────────────────────────────────────────┐
│ 3. Variable-Timestep Gameplay & Scene Update                               │
│    • app.OnUpdate(dt)                                                      │
│    • Scene::UpdateHierarchy() — propagate local-to-world Mat4 transforms   │
│    • Scene::ExtractRenderCommands(cmdBuffer) + app.OnRender(cmdBuffer)     │
└────────────────────────────────────┬───────────────────────────────────────┘
                                     ▼
┌────────────────────────────────────────────────────────────────────────────┐
│ 4. Parallel 3D Software Rendering Pipeline (tvk::Renderer)                 │
│    • Task Setup: Frustum-cull AABBs, compute fused MV & Normal (M^-1)^T    │
│    • Phase 1 (Workers): SoA AVX2 Vertex & Normal transform + RGB lighting  │
│    • Phase 2 (Workers): Near-plane clip, project, backface cull, tile bin  │
│    • Phase 3 (Workers): 64×64 L1 tile sort, AVX2 scanline + 3D line raster │
│                         -> Blit 64×64 tile into TerminalCell BackBuffer    │
└────────────────────────────────────┬───────────────────────────────────────┘
                                     ▼
┌────────────────────────────────────────────────────────────────────────────┐
│ 5. 2D Canvas & Immediate-Mode UI Compositing (tvk::ui)                     │
│    • app.OnUI(canvas2D, uiContext)                                         │
│    • DebugOverlay::Render(canvas2D, frameStats) [if F1 active]             │
│    • Composite 2D text, UTF-8 box borders, HUD bars & sprites directly     │
│      into TerminalCell BackBuffer (preserving 3D background where needed)  │
└────────────────────────────────────┬───────────────────────────────────────┘
                                     ▼
┌────────────────────────────────────────────────────────────────────────────┐
│ 6. Parallel Differential ANSI Encoding & Atomic Present                    │
│    • Phase 4 (Workers): Diff BackBuffer vs FrontBuffer per row slice       │
│                         Emit cursor jumps, FG+BG 24-bit RGB & UTF-8 glyphs │
│    • Present(): Concatenate worker slices + single pal::FastPrint() write  │
│    • Frame Pacer: Hybrid sleep + spin-wait to hit target frame budget      │
└────────────────────────────────────────────────────────────────────────────┘
```

---

## 5. Detailed Subsystem Specifications

### 5.1 Layer 0: Core Foundation (`tvk/core`)
- **`FrameLinearArena`**: Pre-allocated contiguous buffer reset every frame (`offset = 0`), replacing dynamic `std::vector` resizing on hot paths and capping total engine memory usage to a predictable budget (<64 MB total).
- **`JobSystem`**: Fixed worker pool (`std::thread::hardware_concurrency()`). Supports `ParallelFor(count, grain_size, fn)` and phase barriers with spin-then-wait backoff.
- **`Log`**: In-memory ring-buffer logger (`TVK_LOG_INFO`, `TVK_LOG_WARN`, `TVK_LOG_ERROR`) viewable inside the in-terminal `F1` debug console or optionally mirrored to a log file—**never writing to `stdout`/`stderr` while the alternate screen buffer is active**.

### 5.2 Layer 1: Platform & Terminal Abstraction (`tvk/platform`)
- **`TerminalGuard` (RAII + Signal Safety)**:
  - On construction: saves original `termios` (POSIX) or `ConsoleMode` (Win32), enables raw input (`~(ECHO | ICANON | ISIG | IXON)`), enables SGR Mouse tracking (`\x1b[?1000h\x1b[?1002h\x1b[?1006h`), switches to the alternate screen buffer (`\x1b[?1049h`), and hides the cursor (`\x1b[?25l`).
  - Installs signal handlers for `SIGINT`, `SIGTERM`, `SIGSEGV`, `SIGABRT`, and `std::atexit` that immediately restore terminal state so a crash or `Ctrl+C` never leaves the user's shell unusable.
- **`Input` & Escape Sequence Parser**:
  - Non-blocking read of all available bytes per frame from `STDIN_FILENO` / Win32 Console Input.
  - Finite-state machine that distinguishes standalone `ESC` (`\x1b` with 0 follow-up bytes) from CSI sequences (`\x1b[A` arrows, `\x1b[1;5C` modified keys, `\x1b[<btn;x;yM` / `m` SGR mouse press/drag/release/wheel).
  - Tracks per-frame transitions: `IsKeyDown(Key)`, `IsKeyPressed(Key)`, `IsKeyReleased(Key)`, `GetMouseCellPos()`, `GetMouseSubPixelPos()`, `GetMouseDelta()`, `GetMouseWheel()`.

### 5.3 Layer 2: Math & SIMD Kernels (`tvk/math`)
- Complete operator overloads for `Vec2`, `Vec3`, `Vec4`, `Mat3`, `Mat4`, and `Quat`.
- `Mat3 InverseTranspose(const Mat4& modelView)` for mathematically accurate normal transformation under non-uniform scaling.
- `Ray ScreenPointToRay(const Camera&, Vec2 normalizedScreenPos, float aspect)` for 3D mouse picking in the terminal.
- Dual-path `MathSIMD.h`: `#if defined(__AVX2__)` 8-wide FMA kernels alongside a clean scalar/auto-vectorizable fallback loop.

### 5.4 Layer 3: 3D Software Renderer & Multi-Blitter Presenter (`tvk/renderer`)
- **Unified `TerminalCell` Representation**:
  ```cpp
  struct TerminalCell {
      char     utf8[4];   // Up to 4-byte UTF-8 glyph (Braille, Half-block, Sextant, or UI text)
      uint8_t  utf8_len;  // 1..4 bytes
      Color    fg;        // 24-bit foreground RGB
      Color    bg;        // 24-bit background RGB
      uint8_t  flags;     // Bold, underline, dirty mask
  };
  ```
- **Multi-Blitter Modes** (hot-swappable at runtime via `F2`):
  1. **`BlitterType::Braille` (2×4 sub-pixels/cell)**: Uses 2-means / min-max luminance clustering across the 8 sub-pixels in a cell to assign **both `fg` (dots = 1) and `bg` (dots = 0)** colors. Solid 3D surfaces appear completely filled and vibrant while retaining 2×4 edge sharpness!
  2. **`BlitterType::HalfBlock` (`▀`, 1×2 sub-pixels/cell)**: Exact 24-bit RGB color for every top (`fg`) and bottom (`bg`) sub-pixel with zero color averaging.
  3. **`BlitterType::Quadrant` / `Sextant` (2×2 / 2×3 sub-pixels/cell)**: Optimal 2-color block partition for terminals supporting Unicode Symbols for Legacy Computing (`U+1FB00`..`U+1FB3B`).
  4. **`BlitterType::ASCII` (1×1 cell)**: Luminance + surface-normal ramp (` .:-=+*#%@`) for classic retro aesthetics.
- **Rasterizer Enhancements**:
  - Preserves per-vertex colors multiplied by material albedo tint (`vertex.color * material.albedo`).
  - Full RGB lighting accumulation (colored point, directional, and spot lights).
  - Full support for both Perspective and Orthographic projection matrices.
  - Perspective-correct UV interpolation and texture sampling (`Texture*` bound in `Material`).
  - Tile-binned 3D line rasterizer (`DrawLine`, `DrawWireBox`, `DrawGrid`, `DrawFrustum`).
  - Ordered 4×4 Bayer dithering option before terminal color emission.

### 5.5 Layer 4: Scene & Entity-Component-System (`tvk/scene`)
- **Generational `Entity` ID**: 32-bit index + 32-bit generation counter preventing dangling entity references.
- **Sparse-Set `ComponentPool<T>`**: $O(1)$ add, remove, lookup, and contiguous dense-array iteration.
- **Core Components**:
  - `TransformComponent` (`position`, `rotation` Quat, `scale`, dirty flag, cached `worldMatrix`).
  - `HierarchyComponent` (`parent`, `firstChild`, `nextSibling`, `prevSibling`).
  - `MeshRendererComponent` (`MeshHandle`, `Material`, `visible`, `castShadows`).
  - `CameraComponent` (`Camera`, `isPrimary`).
  - `LightComponent` (`LightType::{Directional, Point, Spot}`, `color`, `intensity`, `range`).
  - `Canvas2DComponent` / `TagComponent`.

### 5.6 Layer 5: Physics & Collision (`tvk/physics`)
- **Colliders**: `SphereCollider`, `AABBCollider`, `OBBCollider`, `CapsuleCollider`.
- **Broadphase**: Spatial Hash Grid (ideal for uniform/procedural worlds) + AABB sweep.
- **Narrowphase & Solver**: Analytical intersection tests producing contact manifolds (`normal`, `penetrationDepth`, `contactPoint`), resolved via sequential impulse with restitution and friction.
- **Gameplay Queries**: `PhysicsWorld::Raycast(Ray, maxDist, OutHit)` and `OverlapSphere` for shooting, line-of-sight, and terminal mouse-click object selection.

### 5.7 Layer 6: 2D Canvas & Immediate-Mode Terminal UI (`tvk/ui`)
- **`Canvas2D`**: Writes directly into the `TerminalCell` backbuffer after 3D tile blitting:
  - `DrawText(x, y, utf8_str, fg, bg_optional)` (supports transparent background that preserves the underlying 3D rendered cell's average color!).
  - `DrawBox(x, y, w, h, BorderStyle, title, fg, bg)`
  - `DrawProgressBar(x, y, w, fraction, fg_fill, fg_empty)`
  - `DrawLine2D` / `DrawSprite2D`
- **`UIContext`**: Immediate-mode widgets (`BeginWindow`, `Button`, `Checkbox`, `SliderFloat`, `Label`) driven by keyboard focus (`Tab`/`Enter`) and SGR mouse clicks.

### 5.8 Layer 7: Asset Pipeline (`tvk/assets`)
- Zero-dependency Wavefront `.obj` + `.mtl` parser supporting positions, normals, UVs, vertex colors, and face triangulation.
- Procedural & image texture loaders (`.ppm`, `.bmp`, and procedural noise/checkerboard generators).
- Generational `Handle<T>` asset pool managed by `AssetManager`.

### 5.9 Layer 8: Audio Subsystem (`tvk/audio`)
- Optional lightweight audio backend + built-in procedural retro waveform synthesizer (square, pulse, triangle, noise channels with ADSR envelopes) so terminal games can play sound effects and music with zero external asset files required.
