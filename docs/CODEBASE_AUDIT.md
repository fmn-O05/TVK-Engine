# TVK Engine — Multi-Pass Forensic Codebase Audit & Industry Benchmark Report

This document records the multi-pass forensic audit of the **TVK Engine (Terminal Vulkan Engine)** repository, benchmarking its design against published, industry-standard game engine and terminal rendering architectures, explaining what the original experimental codebase was, documenting keep-vs-purge decisions, and cataloging all **32 defects (`B-01` through `B-32`)** discovered and resolved in the baseline foundation (`Layers 0–3`, `CMakeLists.txt`, `examples/`, and `tests/`).

---

## 1. Executive Summary & Multi-Pass Audit Methodology

To ensure the TVK Engine foundation is rock-solid prior to Phase 1–7 feature expansion, the codebase underwent a multi-pass forensic audit:

1. **Pass 1 — Legacy Structural & Build Audit (`B-01` through `B-16`)**:
   - Inspected all 32 legacy files (`TVK_*.h/.cpp`), purged dead/broken artifacts (`TVK_NotcursesPresenter`, `TVK_Material.h`, unwired `tinyobjloader` `FetchContent`), reorganized the flat file tree into a strict 10-layer Directed Acyclic Graph (`include/tvk/<layer>/` and `src/<layer>/`), and cataloged 16 initial defects (`B-01`–`B-16`).
2. **Pass 2 — Deep Compiler, Concurrency, Mathematical & Industry-Benchmark Audit (`B-17` through `B-32`)**:
   - Audited every line of C++ source code under strict compiler diagnostics (`-Wall -Wextra -Wpedantic -Wdouble-promotion -Wconversion`), tested cross-configuration builds (`Release`/`Debug`, `-DTVK_ENABLE_SIMD=ON/OFF`), verified thread-safety under the C++ memory model, and traced every mathematical convention from world space through clip space to terminal cell space.
   - Uncovered **16 additional defects (`B-17`–`B-32`)**, including a `std::vector<bool>` multi-threaded bit-packing data race (`B-17`), a `-Z` vs `+Z` forward camera sign inversion in `Camera::LookAt()` that culled 100% of geometry (`B-18`), stale trigonometric caches in `Camera::Orbit()` (`B-19`), inverted orthographic frustum planes (`B-20`), `NaN` generation on zero-length normals in AVX2 (`B-21`), broken `-DTVK_ENABLE_SIMD=OFF` and ARM64 builds (`B-22`), `JobSystem(0)` unsigned underflow (`B-23`), GCC-only `__atomic_*` builtins breaking MSVC (`B-24`), 32-bit signed integer overflow in triangle edge functions (`B-25`), inverted `DepthOp::Always` draw order (`B-26`), persistent front-buffer color drift (`B-27`), coupled camera/blitter aspect ratios (`B-28`), unclamped primitive division-by-zero hazards (`B-29`), headless buffer resize mismatches (`B-30`), double terminal initialization (`B-31`), and missing Layer 0 error/handle primitives under `-fno-exceptions` (`B-32`).
3. **Baseline Resolution & Automated CTest Verification**:
   - Rather than leaving broken math, UB data races, or portability blockers inside the existing Layer 0–3 baseline files, **all 32 defects (`B-01` through `B-32`) have been resolved directly in the baseline foundation codebase** and verified by an automated `CTest` suite (`test_core`, `test_math`, `test_renderer`) across all 4 build configurations (`Release`/`Debug` × `SIMD ON`/`SIMD OFF`).

---

## 2. What the Original Legacy Codebase Actually Was

Despite the repository name (`TVK-Engine`, short for *Terminal Vulkan*), the original experimental codebase was **not a full game engine**. It was an experimental **CPU-only 3D software triangle rasterizer** (`tvk_core`) paired with two hardcoded tech-demo loops (`examples/Sandbox.cpp` and `examples/Asteroids_Demo.cpp`).

It lacked core game-engine runtime infrastructure (now fully specified across [`docs/ARCHITECTURE.md`](ARCHITECTURE.md) and [`docs/specs/01_*.md`–`07_*.md`](specs/01_CORE_AND_PLATFORM.md)):
- **No Engine Loop or Application Host**: No `OnInit`, `OnFixedUpdate(fixedDt)`, `OnUpdate(dt)`, `OnRender(cmd, alpha)`, `OnUI(canvas, ui)`, or `OnShutdown`.
- **No Entity-Component-System (ECS) or Scene Graph**: Demos manually constructed 4×4 matrices every frame.
- **No Structured Input State Machine**: Only a single-byte `PollKey(char&)` call with no ANSI CSI arrow/function key parser, no Kitty key-release protocol or decay timer, and no SGR 1006 mouse tracking.
- **No 2D Canvas or Immediate-Mode TUI**: Demos either could not render text or invoked raw `printf` that fought with `TerminalPresenter` and corrupted differential front-buffer tracking.
- **No Collision Detection, Physics, or 3D Mouse Picking**: No colliders, broadphase spatial hash, impulse solver, or cell-to-world ray unprojection.
- **No Asset Pipeline or Audio Subsystem**: `tinyobjloader` was fetched in `CMakeLists.txt` but never included or called anywhere in C++.

### How the Rendering Pipeline Connects (Refined Layer 0–3 Foundation)

```text
Application / Demo Loop (or tvk::Engine Runtime)
  │
  ├─► Construct tvk::CommandBuffer
  │     └── Record DrawMesh / DrawSphere / DrawCylinder / DrawCapsule / DrawGround / DrawLine
  │
  ├─► tvk::Renderer::BeginFrame(camera)
  │     ├── Check SIGWINCH / Headless dimensions & update cell-aligned tile buffers
  │     ├── Reset per-tile atomic linked-list heads (m_GlobalTileTriHeads, m_GlobalTileLineHeads)
  │     ├── Reset per-thread triangle & line arena counters and m_NodeCount
  │     └── Transform active scene lights into Left-Handed +Z view space
  │
  ├─► tvk::Renderer::Execute(cmdBuffer)
  │     ├── BuildRenderTasks():
  │     │     ├── Validate meshes, orient Cylinder/Capsule via Duff et al. branchless ONB
  │     │     ├── Frustum-cull object AABB against 6 Gribb-Hartmann planes
  │     │     ├── Compute fused MV = View * Model and NormalMatrix = InverseTranspose(MV_3x3)
  │     │     └── Populate m_VertexTasks, m_GeoTasks, and m_LineTasks
  │     │
  │     ├── JobSystem Phase 1 — ExecuteVertexPhase (Parallel across worker threads):
  │     │     ├── Batch up to 1024 vertices into Structure-of-Arrays (SIMDWorkingBuffer)
  │     │     ├── Transform positions & normals via guarded AVX2/FMA or Scalar fallback
  │     │     └── Modulate Vertex::color * cmd.color & accumulate full RGB multi-light shading
  │     │
  │     ├── JobSystem Phase 2 — ExecuteGeometryPhase (Parallel across worker threads):
  │     │     ├── Clip triangles & 3D lines against dynamic z >= camera.GetNear()
  │     │     ├── Project via Perspective (w = z) or Orthographic (w = 1) matrix
  │     │     ├── Guard-band clamp [-16384, +16384], 64-bit signed area backface cull
  │     │     ├── Precompute screen-space gradients (dz/dx, dz/dy, dR/dx, dG/dx, dB/dx)
  │     │     └── Bin triangles & lines into overlapping tiles via lock-free std::atomic CAS
  │     │
  │     ├── JobSystem Phase 3 — ExecuteRasterPhase (Parallel per tile):
  │     │     ├── Allocate 64×64 local_fb (16 KB) + local_zb (16 KB) = 32 KB on thread stack (L1 cache)
  │     │     ├── Sort tile's DepthOp::Less triangles front-to-back; preserve order for DepthOp::Always
  │     │     ├── Rasterizer::ProcessTileBand(): top-left fill bias, AVX2/Scalar scanline + 3D line DDA
  │     │     └── TerminalPresenter::SampleTileToBackBuffer(): Braille 2-color FG+BG / HalfBlock /
  │     │         Quadrant / Sextant / ASCII blitting into 16-byte TerminalCell backbuffer
  │     │
  │     └── JobSystem Phase 4 — TerminalPresenter::BuildOutput (Parallel per terminal row band):
  │           └── Diff m_BackBuffer vs m_FrontBuffer, emit exact 24-bit FG+BG ANSI & UTF-8 sequences
  │
  └─► tvk::Renderer::Present()
        └── TerminalPresenter::FlushOutput() -> DECSET 2026 BSU (\x1b[?2026h) + worker slices
            + ESU (\x1b[?2026l) flushed in a single atomic pal::FastPrint() syscall
```

---

## 3. Keep vs. Purge Decisions

### Salvaged, Hardened & Reorganized (Core Engine Foundation)
1. **L1-Cache-Bounded 64×64 Tiled Software Rasterizer (`Rasterizer.cpp`, `Renderer.cpp`)**: Keeping the `16 KB` color buffer + `16 KB` depth buffer (`32 KB` total) on each worker thread's stack eliminates DRAM bandwidth bottlenecks and pixel-level lock contention.
2. **8-Wide AVX2/FMA Analytical Scanline & SoA Transform Kernels (`MathSIMD.h`, `Rasterizer.cpp`)**: Paired with bit/epsilon-compatible scalar fallbacks so builds succeed regardless of CPU architecture or `-DTVK_ENABLE_SIMD=OFF`.
3. **Lock-Free Tile Binning & Parallel Differential ANSI Presenter (`TerminalPresenter.cpp`)**: Upgraded to full 24-bit TrueColor 2-color FG+BG luminance clustering across 5 runtime-switchable blitters (`Braille`, `HalfBlock`, `Quadrant`, `Sextant`, `ASCII`) with `DECSET 2026` synchronized output.
4. **Lock-Free Atomic Work Queue & Thread Pool (`JobSystem.h/.cpp`)**: Hardened with portable `CpuRelax()`, mutex-synchronized task publication, `numThreads >= 1` clamping, and `ParallelFor`.

### Purged in Phase 0 Cleanup
1. **`TVK_NotcursesPresenter.h/.cpp` (Deleted)**: Failed to implement `IPresenter` pure virtual methods, relied on an untracked local path (`notcurses-master/notcurses-master`), and called `std::exit(EXIT_SUCCESS)` inside `Present()`.
2. **`TVK_Material.h` (Deleted)**: Dead duplicate header never included by any source file.
3. **Unwired `tinyobjloader` CMake `FetchContent` (Removed)**: Downloaded an external repository at configure time without any C++ reference.
4. **Dead `Renderer::DrawTriangle`, `HybridBarrier`, and Point Arenas (Removed in Iteration 2)**: Eliminated 106 lines of dead duplicate code and unused per-thread point arena allocations.

---

## 4. Industry Benchmark Comparison Matrix

TVK Engine's architecture and subsystem specifications were benchmarked against leading open-source and commercial C/C++ engines and terminal libraries:

| Subsystem | Industry Benchmark Reference | Key Architectural Pattern Adopted | TVK Engine Implementation & Spec |
| :--- | :--- | :--- | :--- |
| **Core Handles & Memory** (`tvk/core`) | **Sokol (`sokol_gfx`)** & **Our Machinery** | 32-bit generational handles (`16-bit index + 16-bit generation`); zero-exception `Result<T, E>`; bounded per-frame linear arenas. | Implemented in [`include/tvk/core/Handle.h`](../include/tvk/core/Handle.h), [`Result.h`](../include/tvk/core/Result.h), [`Assert.h`](../include/tvk/core/Assert.h); full spec in [`docs/specs/01_CORE_AND_PLATFORM.md`](specs/01_CORE_AND_PLATFORM.md). |
| **Threading & SIMD** (`tvk/core`, `tvk/math`) | **EnkiTS** & **Google Highway** | Lock-free atomic work stealing/queues with `ParallelFor(count, grain, fn)` and compile-time SIMD dispatch with scalar fallback. | Implemented in [`JobSystem.h`](../include/tvk/core/JobSystem.h) (`ParallelFor`, portable `CpuRelax()`) and [`MathSIMD.h`](../include/tvk/math/MathSIMD.h) (`TVK_SIMD_AVX2` + scalar). |
| **Fixed-Timestep Loop** (`tvk/core`, `tvk/app`) | **Gaffer on Games (*"Fix Your Timestep!"*)** | Fixed $\Delta t$ physics accumulator with spiral-of-death clamp ($\le 0.25\text{ s}$) and render state interpolation factor $\alpha \in [0, 1)$. | Specified in [`docs/specs/01_CORE_AND_PLATFORM.md`](specs/01_CORE_AND_PLATFORM.md) and [`docs/specs/04_ECS_AND_SCENE.md`](specs/04_ECS_AND_SCENE.md). |
| **Terminal PAL & Input** (`tvk/platform`) | **Notcurses**, **Termbox2**, **FTXUI**, **Kitty** | Async-signal-safe `termios` teardown, `DECSET 2026` synchronized output, Kitty `\x1b[>1u` key-release protocol + hold-timer fallback, SGR 1006 mouse. | Signal-safe `PAL.cpp` active in baseline; full CSI/Kitty/SGR state machine specified in [`docs/specs/01_CORE_AND_PLATFORM.md`](specs/01_CORE_AND_PLATFORM.md). |
| **Command Sorting & RHI** (`tvk/renderer`) | **bgfx** | Stateless `CommandBuffer` recording with 64-bit `SortKey` (`[Pass|Layer|Material|Depth|Seq]`) to minimize state changes and maximize Early-Z. | Specified in [`docs/specs/02_MATH_AND_RASTERIZER.md`](specs/02_MATH_AND_RASTERIZER.md). |
| **Tiled Software Rasterizer** (`tvk/renderer`) | **Intel Larrabee** & **Quake** | L1-cache-resident tile bins, 2D guard-band clipping, `int64_t` signed area, D3D/Larrabee top-left fill rule bias (`-1`), $1/w$ perspective interpolation. | Implemented in [`src/renderer/Rasterizer.cpp`](../src/renderer/Rasterizer.cpp); full math in [`docs/specs/02_MATH_AND_RASTERIZER.md`](specs/02_MATH_AND_RASTERIZER.md). |
| **Unicode Sub-Pixel Blitting** (`tvk/renderer`) | **Notcurses** & **Chafa** | 2-color FG+BG luminance clustering per cell (`Braille` 2×4, `Sextant` 2×3, `Quadrant` 2×2, `HalfBlock` 1×2) + 4×4 Bayer dithering + OKLab fallback. | All 5 blitters + 2-color Braille clustering implemented in [`TerminalPresenter.cpp`](../src/renderer/TerminalPresenter.cpp); spec in [`docs/specs/03_PRESENTER_AND_CANVAS2D.md`](specs/03_PRESENTER_AND_CANVAS2D.md). |
| **ECS & Scene Graph** (`tvk/scene`) | **EnTT**, **Flecs**, **Bevy** | Paged sparse-set `ComponentPool<T>`, smallest-pool lead `View<Ts...>`, deferred `EntityCommandBuffer`, topological parent-first transform array. | Specified in [`docs/specs/04_ECS_AND_SCENE.md`](specs/04_ECS_AND_SCENE.md). |
| **3D Physics & Picking** (`tvk/physics`) | **Box2D-lite** (Erin Catto) & **Jolt** | Arena-allocated spatial hash broadphase, analytical narrowphase manifolds, 8-iteration sequential impulse solver with Baumgarte stabilization. | Specified in [`docs/specs/05_PHYSICS_AND_PICKING.md`](specs/05_PHYSICS_AND_PICKING.md). |
| **Immediate-Mode TUI** (`tvk/ui`) | **Dear ImGui** & **Clay** | FNV-1a ID stack (`PushID`/`PopID`), `hotId`/`activeId`/`focusedId` state machine, single-pass box layout composited into `TerminalCell` backbuffer. | Specified in [`docs/specs/06_ASSETS_UI_AND_AUDIO.md`](specs/06_ASSETS_UI_AND_AUDIO.md). |

---

## 5. Complete Catalog of All 32 Defects (`B-01` through `B-32`)

All 32 defects below—comprising both the 16 original audit defects (`B-01`–`B-16`) and the 16 Iteration 2 deep forensic audit defects (`B-17`–`B-32`)—have been **resolved in the baseline Layer 0–3 codebase (`M1`)** and verified via `ctest --output-on-failure`.

### 5.1 Pass 1 Audit Defects (`B-01` through `B-16`)

| ID | Origin File & Line(s) | Severity | Root Cause & Technical Impact | Baseline Resolution & Verification |
| :--- | :--- | :--- | :--- | :--- |
| **B-01** | `CMakeLists.txt:79–85` | **Critical** | **Unconditional `-lwinpthread` on Non-MSVC Builds**: Linked `-lwinpthread` in `else()` for all non-MSVC compilers, causing immediate linker failure on Linux and macOS. | **RESOLVED (M1)**: Scoped `-lwinpthread` strictly inside `if(MINGW)`. Verified on Linux GCC/Clang builds. |
| **B-02** | `include/tvk/renderer/Renderer.h:29–32, 118, 167–168`<br>`src/renderer/Renderer.cpp:32–33, 83` | **Critical** | **4.5 GB Startup Arena Allocation & 40 MB Residual Node Arena**: Defaulted `MaxTrisPerThread = 2,000,000` (`132 B` each) and `MaxPtsPerThread = 1,000,000` per worker thread (~4.5 GB on 16 threads), and hardcoded `m_MaxNodes = 5,000,000` (`40 MB`) while ignoring `desc.MaxNodesPerThread` (`262,144`) and allocating unused point arenas (`m_GlobalPtArena`). | **RESOLVED (M1)**: Lowered `MaxTrisPerThread` to `65,536`, wired `m_MaxNodes = desc.MaxNodesPerThread * numWorkers`, and removed dead point arenas. Verified in `test_renderer`. |
| **B-03** | `examples/Sandbox.cpp`<br>`examples/Asteroids_Demo.cpp:279` | **Low** | **Compiler Warnings under `-Wall -Wextra -Wdouble-promotion`**: Unused variables (`panX`, `dt`), member initializer order mismatches, and implicit `float` to `double` promotion on `currentMs`. | **RESOLVED (M1)**: Removed unused variables, fixed member initialization order, and eliminated `-Wdouble-promotion`. Verified with `-Wall -Wextra -Wpedantic`. |
| **B-04** | `src/renderer/Renderer.cpp:215, 360`<br>`src/renderer/CommandBuffer.cpp:18–26` | **High** | **Per-Vertex Color Destruction**: `CommandBuffer::DrawMesh` set `cmd.color = {255,255,255,255}` and `TransformMeshVertices` executed `sv.color = albedo_override`, overwriting `Vertex::color` and rendering all vertex-colored meshes (`Sandbox.cpp`, `Asteroids_Demo.cpp`) pure white. | **RESOLVED (M1)**: `TransformMeshVertices` modulates `Vertex::color` by `albedo_override` (`(v.color.c * albedo_override.c) / 255`). Verified in `test_renderer`. |
| **B-05** | `src/renderer/Renderer.cpp:224–240`<br>`src/renderer/Primitives.cpp` | **High** | **Unoriented Cylinder & Capsule Primitives**: `DrawCylinder` computed `Vec3 axis = Normalize(Sub(cmd.b, cmd.a)); (void)axis;` and discarded it, always rendering a vertical Y-aligned cylinder; `DrawCapsule` drew the exact same flat cylinder without hemisphere end-caps. | **RESOLVED (M1)**: Uses Duff et al. branchless `BuildOrthonormalBasis(axis, right, forward)` to orient cylinders and capsules along `cmd.b - cmd.a` and adds hemisphere caps for `DrawCapsule`. Verified in `test_math` and `test_renderer`. |
| **B-06** | `src/renderer/Renderer.cpp:248–250`<br>`src/renderer/Rasterizer.cpp` | **Medium** | **`CommandType::DrawLine` Silent No-Op**: `CommandBuffer::DrawLine` recorded commands, but `BuildRenderTasks` fell through to `else { continue; }`, discarding all 3D lines. | **RESOLVED (M1)**: Implemented 3D near-plane line clipping, projection, tile binning, and depth-tested Bresenham/DDA line rasterization into tile buffers. Verified in `test_renderer`. |
| **B-07** | `src/renderer/Renderer.cpp:267–274`<br>`include/tvk/math/Math.h` | **Medium** | **Incorrect Normal Matrix under Non-Uniform Scale**: `m_FrameNormalTransforms` copied the upper-left 3×3 of `view * model` directly instead of computing the inverse-transpose $(M_{3\times3}^{-1})^T$, distorting lighting on scaled meshes. | **RESOLVED (M1)**: Added `Mat4NormalMatrix(const Mat4&)` computing the analytical 3×3 cofactor inverse-transpose with singular determinant fallback. Verified in `test_math`. |
| **B-08** | `src/renderer/Rasterizer.cpp:118–130` | **High** | **Orthographic Projection Ignored by Rasterizer**: `RasterizeProjectedTriangle` only read `proj_matrix.m[0][0]` and `m[1][1]` and hardcoded perspective division `1.0f / z`, completely breaking `ProjectionType::Orthographic`. | **RESOLVED (M1)**: Evaluates full 4×4 `proj_matrix`, supporting both Perspective ($w_{\text{clip}} = z_{\text{view}}$, depth $= 1/z$) and Orthographic ($w_{\text{clip}} = 1$, positive depth metric $\in (0, 1]$). Verified in `test_renderer`. |
| **B-09** | `include/tvk/renderer/Rasterizer.h:10`<br>`src/renderer/Rasterizer.cpp:35` | **Medium** | **Hardcoded `NEAR_PLANE = 4.0f`**: `ClipAgainstNearPlane` ignored `Camera::GetNear()` and hardcoded `4.0f`, clipping geometry prematurely when `camera.SetPerspective(..., nearPlane, ...)` used standard near planes (e.g. `0.1f`–`1.0f`). | **RESOLVED (M1)**: Passes `m_CameraPtr->GetNear()` dynamically into `ClipAgainstNearPlane`. Verified in `test_renderer`. |
| **B-10** | `src/renderer/Renderer.cpp:363–384` | **Medium** | **Monochrome Multi-Light Diffuse & Single-Light Specular**: Collapsed RGB light colors into scalar luminance (`0.2126R + 0.7152G + 0.0722B`) and only computed specular `nDotH` for `m_ViewLights[0]`. | **RESOLVED (M1)**: Accumulates per-channel RGB diffuse and RGB Blinn-Phong specular across all active view-space lights. Verified in `test_renderer`. |
| **B-11** | `src/renderer/TerminalPresenter.cpp:163–172` | **High** | **Data Race on `static bool init_lut` in `BuildOutput()`**: Multi-threaded worker calls to `BuildOutput()` concurrently checked and mutated unsynchronized function-local `static bool init_lut`, `str_num`, and `str_len` (C++ UB). | **RESOLVED (M1)**: Replaced with a thread-safe `constexpr` / constructor-initialized byte-to-decimal lookup table. Verified in `test_renderer`. |
| **B-12** | `src/renderer/TerminalPresenter.cpp:99–101` | **Medium** | **3-Bit Color Truncation (`& 0xE0`) & Hollow Braille Cells**: Masked RGB channels with `& 0xE0` (8 levels/channel), emitted foreground-only Braille dots over black backgrounds (creating black pinholes on solid surfaces), and lacked `HalfBlock`, `Quadrant`, `Sextant`, and `ASCII` blitters. | **RESOLVED (M1)**: Implemented full 24-bit TrueColor 2-color FG+BG luminance clustering for `Braille` (zero pinholes on solid surfaces) plus `HalfBlock`, `Quadrant`, `Sextant`, and `ASCII` blitters. Verified in `test_renderer`. |
| **B-13** | `src/platform/PAL.cpp:62–94, 143–158` | **High** | **Missing POSIX `termios` Raw Mode & Crash/Signal Recovery**: `SetupTerminal()` on Linux/macOS did not disable `ICANON` or `ECHO` and installed no signal handlers (`SIGINT`, `SIGTERM`, `SIGABRT`, `SIGSEGV`), leaving the user's terminal stuck in the alternate buffer on exit or crash; Windows `RestoreTerminal()` did not restore `dwMode`. | **RESOLVED (M1)**: Implemented idempotent `termios` / Win32 console mode save & restore, `std::atexit(RestoreTerminal)`, async-signal-safe `sigaction` handlers writing the raw ANSI reset string, and `SIGWINCH` resize flag tracking. |
| **B-14** | `include/tvk/renderer/Renderer.h:73–75`<br>`src/renderer/Renderer.cpp:549–624`<br>`include/tvk/core/JobSystem.h:11–41` | **Low** | **Dead Duplicate Code**: `Renderer::DrawTriangle` (75 lines duplicating `Renderer::DrawMesh`) and `HybridBarrier` (31 lines in `JobSystem.h`) were unused dead code. | **RESOLVED (M1)**: Removed `Renderer::DrawTriangle` and `HybridBarrier`. |
| **B-15** | `include/tvk/core/Stats.h:17–18`<br>`src/renderer/Renderer.cpp` | **Low** | **Unpopulated `FrameStats` Counters**: `trianglesCulled` and `trianglesRasterized` were declared in `FrameStats` but never updated. | **RESOLVED (M1)**: Populated `trianglesSubmitted`, `trianglesCulled`, `trianglesRasterized`, `activeTiles`, `ansiBytesEmitted`, and `arenaOverflows`. Verified in `test_renderer`. |
| **B-16** | `include/tvk/math/MathSIMD.h:2`<br>`src/renderer/Rasterizer.cpp:5` | **High** | **Missing Scalar Fallback for Non-AVX2 Targets**: Unconditional `#include <immintrin.h>` and `_mm256_*` intrinsics prevented compilation when AVX2 was disabled or unavailable. | **RESOLVED (M1)**: Guarded all AVX2 code with `#if defined(TVK_SIMD_AVX2) && defined(__AVX2__)` and provided scalar fallbacks in `MathSIMD.h` and `Rasterizer.cpp`. Verified under `-DTVK_ENABLE_SIMD=OFF`. |

---

### 5.2 Pass 2 Deep Forensic Audit Defects (`B-17` through `B-32`)

| ID | Origin File & Line(s) | Severity | Root Cause & Technical Impact | Baseline Resolution & Verification |
| :--- | :--- | :--- | :--- | :--- |
| **B-17** | `include/tvk/renderer/Renderer.h:121`<br>`src/renderer/Renderer.cpp:508, 539, 542` | **Critical** | **`std::vector<bool> m_TileWasDirty` Multi-Threaded Bit-Packing Data Race (UB)**: Concurrent worker threads in `ExecuteRasterPhase` read and mutated `m_TileWasDirty[tile_idx]`. Because `std::vector<bool>` packs 8 flags per byte, adjacent tiles modified the same byte without synchronization. | **RESOLVED (M1)**: Replaced `std::vector<bool>` with byte-addressable `std::vector<uint8_t> m_TileWasDirty`. Verified in multi-threaded `test_renderer`. |
| **B-18** | `src/renderer/Camera.cpp:52–78` | **Critical** | **`Camera::LookAt()` `-Z` Sign Inversion Culling 100% of Geometry**: TVK's view/clip pipeline uses a **Left-Handed `+Z` forward** convention (`GetViewMatrix()` orbit sets `v.m[2][3] = +m_Distance`, `Perspective` sets `w_clip = +z_view`, `ClipAgainstNearPlane` keeps `z >= near`). However, `Camera::LookAt` set `v.m[2][0..2] = -f` and `v.m[2][3] = Dot(f, eye)` (OpenGL `-Z`), transforming all points in front of `eye` to negative `z_view < 0` and culling 100% of visible geometry. | **RESOLVED (M1)**: Aligned `Camera::LookAt` to Left-Handed `+Z` forward (`f = Normalize(target - eye)`, `r = Normalize(Cross(up, f))`, `u = Cross(f, r)`, row 2 = `{+f.x, +f.y, +f.z, -Dot(f, eye)}`). Verified in `test_math` and `test_renderer`. |
| **B-19** | `src/renderer/Camera.cpp:16–20` | **High** | **`Camera::Orbit()` Leaves Trigonometric Cache Stale**: `Camera::Orbit(yaw, pitch)` updated `m_Yaw` and `m_Pitch` without calling `UpdateCache()`, leaving `m_CosYaw`, `m_SinYaw`, `m_CosPitch`, `m_SinPitch` stale until another setter was called. | **RESOLVED (M1)**: `Camera::Orbit()` calls `UpdateCache()` immediately. Verified in `test_math`. |
| **B-20** | `src/renderer/Camera.cpp:111–141` | **High** | **Inverted Orthographic Matrix & Missing Division-by-Zero Guards**: `Camera::GetProjectionMatrix(Orthographic)` computed `range_inv = 1.0f / (m_Near - m_Far) < 0`, inverting near/far Gribb-Hartmann frustum planes (`ExtractFrustumPlanes`) for `+Z` view space, and lacked epsilon guards on `m_Far - m_Near`, `m_OrthoSize`, and `aspect_ratio`. | **RESOLVED (M1)**: Uses `range_inv = 1.0f / (m_Far - m_Near) > 0` (`p.m[2][2] = 2.0f * range_inv`, `p.m[2][3] = -(m_Far + m_Near) * range_inv`) with `1e-6f` epsilon guards. Verified in `test_math` and `test_renderer`. |
| **B-21** | `include/tvk/math/MathSIMD.h:9–20, 82–90` | **High** | **`NaN` Generation on Zero-Length Normals in AVX2/SSE**: For `{0, 0, 0}` normals, `dot = 0.0f`, so `_mm_rsqrt_ps(0.0f)` / `_mm256_rsqrt_ps(0.0f)` returned `+Inf`, and Newton-Raphson refinement evaluated `0.0f * Inf = NaN`, poisoning vertex lighting into `INT_MIN`. | **RESOLVED (M1)**: Masked `rlen` with `_mm_cmpgt_ps(dot, 1e-12f)` / `_mm256_cmp_ps(dot, 1e-12f, _CMP_GT_OQ)` so zero-length vectors normalize cleanly to `{0, 0, 0}`. Verified in `test_math`. |
| **B-22** | `CMakeLists.txt:16, 75–77`<br>`include/tvk/core/JobSystem.h:4, 27, 82`<br>`src/renderer/Renderer.cpp:8` | **High** | **Broken `-DTVK_ENABLE_SIMD=OFF`, Unguarded `<emmintrin.h>` on ARM64, and Non-Portable `-march=native`**: `CMakeLists.txt` did not wire `TVK_ENABLE_SIMD` to a preprocessor macro; `JobSystem.h` unconditionally included x86 `<emmintrin.h>` and called `_mm_pause()`; `-march=native` emitted non-portable host instructions. | **RESOLVED (M1)**: Added portable `CpuRelax()` (x86 `_mm_pause()`, ARM64 `yield`, fallback `std::this_thread::yield()`), defined `TVK_SIMD_AVX2=1` only when `TVK_ENABLE_SIMD=ON` on x86_64, and made `TVK_MARCH_NATIVE` optional (`OFF` by default). |
| **B-23** | `src/core/JobSystem.cpp:5–10`<br>`include/tvk/core/JobSystem.h:55–91` | **High** | **`JobSystem(0)` Unsigned Underflow, Unsynchronized Task Setup & Copyability**: Passing `numThreads <= 0` caused `m_Workers.reserve(numThreads - 1)` to wrap to `SIZE_MAX` and abort; `m_TaskContext`/`m_TaskCallback` were written outside `m_WakeMutex`; `JobSystem` was not marked non-copyable and lacked `ParallelFor`. | **RESOLVED (M1)**: Clamped `m_NumThreads >= 1`, deleted copy/move constructors, moved task pointer setup inside `m_WakeMutex`, and added `ParallelFor(count, grainSize, fn)`. Verified in `test_core`. |
| **B-24** | `src/renderer/Renderer.cpp:99, 475–484, 506, 610–619`<br>`src/renderer/Rasterizer.cpp:7–27` | **High** | **Non-Portable GCC `__atomic_*` Builtins Break MSVC & Silent Node Overflow**: `Renderer.cpp` invoked GCC/Clang-only `__atomic_fetch_add`, `__atomic_load_n`, and `__atomic_compare_exchange_n` on plain `uint32_t` / `int32_t` fields, failing on MSVC; `Rasterizer.cpp` contained dead `#ifdef _MSC_VER` shims; node arena overflow was not counted in `FrameStats`. | **RESOLVED (M1)**: Replaced with standard `std::atomic<uint32_t> m_NodeCount` and `std::atomic<int32_t>` tile list heads, removed dead shims, and incremented `arenaOverflows` on overflow. |
| **B-25** | `src/renderer/Rasterizer.cpp:185–192, 285–287, 375–377` | **High** | **32-Bit Signed Integer Overflow in Edge Functions & Missing Top-Left Fill Bias**: Off-axis vertices near $z = \text{near}$ projected to screen coordinates $> 46,340$, overflowing `int32_t` in `(p1x - p0x) * (p2y - p0y)` (UB), and `E >= 0` edge tests lacked a top-left fill bias, double-rasterizing shared triangle edges. | **RESOLVED (M1)**: Clamped projected coordinates to a 2D guard band (`[-16384, +16384]`), evaluated signed triangle area in `int64_t`, and applied the D3D/Larrabee `-1` top-left fill bias. Verified in `test_renderer`. |
| **B-26** | `src/renderer/Renderer.cpp:527–538`<br>`include/tvk/renderer/Rasterizer.h:47`<br>`src/renderer/Rasterizer.cpp:60–63, 222` | **Medium** | **Front-to-Back Tile Sort Breaks `DepthOp::Always` & Flat Vertex-0 Triangle Color**: Sorting all tile triangles front-to-back caused farther `DepthOp::Always` triangles rendered later in the loop to overwrite closer triangles, and `ProjectedTriangle` discarded `view_verts[1..2].color`. | **RESOLVED (M1)**: Preserved command submission order for `DepthOp::Always` triangles and interpolated per-vertex shaded RGB colors across scanlines. Verified in `test_renderer`. |
| **B-27** | `src/renderer/TerminalPresenter.cpp:177–199, 229–242, 248–279` | **High** | **Front-Buffer Color Drift (`< 15` Threshold) & Missing `DECSET 2026` Sync**: `local_append_color` skipped emitting ANSI RGB codes when channel deltas were `< 15` while line 240 still set `front = back`, permanently latching stale colors; `FlushOutput` reallocated `std::string final_out` each frame without `DECSET 2026` framing. | **RESOLVED (M1)**: Enforced exact front-buffer color tracking (`fg` and `bg`), pre-allocated `m_FinalOutput`, and bracketed flushes with `\x1b[?2026h` (BSU) and `\x1b[?2026l` (ESU). Verified in `test_renderer`. |
| **B-28** | `src/renderer/Renderer.cpp:74–75, 122–125`<br>`include/tvk/renderer/TerminalPresenter.h:52–65` | **Medium** | **Coupled Camera Aspect Ratio, `Sextant` Tile Race & Dead `IPresenter` Members**: Hardcoding `m_PixW = m_TermW * 2; m_PixH = m_TermH * 4` coupled camera aspect ratio to Braille mode, and `64` pixel tile height was not divisible by `3` (`Sextant` 2×3 cells straddled tiles across threads); `TerminalPresenter` had dead members and missing `override`s. | **RESOLVED (M1)**: Computed physical camera aspect ratio from cell dimensions (`(m_TermW * 0.5f) / m_TermH`), aligned tile pixel dimensions to exact multiples of the active blitter's sub-pixel cell size, and cleaned `TerminalPresenter`. |
| **B-29** | `src/renderer/Primitives.cpp:11, 41, 101`<br>`src/renderer/Renderer.cpp:12–42, 217–220, 413–420` | **Medium** | **Unclamped `Primitives` Division-by-Zero, Unchecked Mesh Indices & Ignored `desc.Fog`**: Passing `<= 0` subdivisions to `CreateSphere`/`CreateCylinder`/`CreateGround` caused division by zero; `Renderer` did not validate null/empty meshes or out-of-bounds indices; `Renderer(desc)` never copied `desc.Fog` into `m_Fog`. | **RESOLVED (M1)**: Clamped primitive subdivision parameters (`lat >= 2, lon >= 3`, `cyl_len >= 1, cyl_ring >= 3`, `steps >= 1`), added mesh/index bounds validation, and wired `m_Fog = desc.Fog`. Verified in `test_renderer`. |
| **B-30** | `src/renderer/Renderer.cpp:52–67`<br>`src/renderer/TerminalPresenter.cpp:46–64` | **Medium** | **Broken `HeadlessFallback` Resize & Redundant `ioctl(TIOCGWINSZ)` Calls**: `Renderer::UpdateTerminalSize()` called `TryGetTerminalSize()` twice per frame and ignored `m_Headless`, sizing `Renderer` to the host terminal while `TerminalPresenter` sized to `120×40`, risking out-of-bounds writes during headless tests in an active terminal. | **RESOLVED (M1)**: `Renderer::UpdateTerminalSize()` checks `m_Headless` (`nw = m_FallbackW; nh = m_FallbackH;`) and performs a single size query when non-headless. Verified in `test_renderer`. |
| **B-31** | `examples/Sandbox.cpp:214, 216, 234, 239`<br>`examples/Asteroids_Demo.cpp:279` | **Medium** | **Double `SetupTerminal()`, Uninterruptible Loop & Raw `printf` Desync in Examples**: `Sandbox.cpp` called `pal::SetupTerminal()` twice, wrote `perf_log.csv` unconditionally into CWD, and looped infinitely without polling `'q'`/`ESC`; `Asteroids_Demo.cpp` used raw `printf` that corrupted `TerminalPresenter`'s frontbuffer. | **RESOLVED (M1)**: Removed duplicate `SetupTerminal()`, added `'q'`/`ESC` clean exit and `--frames N` CLI support (`--csv` opt-in for `perf_log.csv`), and replaced raw stdout `printf`. |
| **B-32** | `CMakeLists.txt:70–74, 118–120`<br>`include/tvk/core/Types.h`<br>`include/tvk/math/Math.h` | **Medium** | **Unconditional `-O3` in Debug Builds, Missing `-fno-exceptions` Primitives, Fragmented Math Types & Empty `TVK_BUILD_TESTS`**: `-O3` was passed in `Debug` builds; `-fno-exceptions` lacked `Result<T,E>`, `Handle<Tag>`, and crash-safe `TVK_ASSERT`; `Vec2`/`Vec4`/`AABB` were split across 3 layers; `if(TVK_BUILD_TESTS)` registered zero tests. | **RESOLVED (M1)**: Added config-aware `-O0 -g` / `-O3` flags, created `Handle.h`, `Result.h`, and `Assert.h`, unified math types in `Math.h` (re-exported in `Types.h`/`Mesh.h`), and wired `CTest` (`test_core`, `test_math`, `test_renderer`). |

---

## 6. Independent Verification Commands

Every defect fix and baseline invariant documented above can be independently verified by running the following commands from the repository root:

```bash
# 1. Configure and test Release build with AVX2 SIMD enabled
cmake -S . -B /tmp/tvk_build_rel_simd -DCMAKE_BUILD_TYPE=Release -DTVK_ENABLE_SIMD=ON -DTVK_BUILD_TESTS=ON
cmake --build /tmp/tvk_build_rel_simd -j
ctest --test-dir /tmp/tvk_build_rel_simd --output-on-failure

# 2. Configure and test Release build with SIMD disabled (Scalar fallback path)
cmake -S . -B /tmp/tvk_build_rel_scalar -DCMAKE_BUILD_TYPE=Release -DTVK_ENABLE_SIMD=OFF -DTVK_BUILD_TESTS=ON
cmake --build /tmp/tvk_build_rel_scalar -j
ctest --test-dir /tmp/tvk_build_rel_scalar --output-on-failure

# 3. Smoke-test example executables in headless/bounded-frame mode
/tmp/tvk_build_rel_simd/sandbox --frames 5
/tmp/tvk_build_rel_simd/asteroids --frames 5

# 4. Clean up temporary build directories
rm -rf /tmp/tvk_build_rel_simd /tmp/tvk_build_rel_scalar
```
