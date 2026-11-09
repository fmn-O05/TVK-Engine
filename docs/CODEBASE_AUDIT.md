# TVK Engine — Forensic Codebase Audit

This document explains what the original experimental codebase actually was, how its components connected, which subsystems are worth keeping, which parts were dead or broken, and every concrete bug discovered during the audit.

---

## 1. What the Original Codebase Actually Was

Despite the repository name (`TVK-Engine`, short for *Terminal Vulkan*), the legacy codebase was **not a game engine**. It was an experimental **CPU-only 3D software triangle rasterizer** (`tvk_core`) paired with two hardcoded demo loops (`examples/Sandbox.cpp` and `examples/Asteroids_Demo.cpp`).

It contained **zero game-engine runtime infrastructure**:
- No engine loop or application lifecycle (`OnInit`, `OnFixedUpdate`, `OnUpdate`, `OnRender`, `OnUI`, `OnShutdown`).
- No Entity-Component-System (ECS), scene graph, or transform hierarchy.
- No working keyboard/mouse input state system, event bus, or POSIX raw terminal mode.
- No 2D canvas, text renderer, or HUD compositor (demos either couldn't draw text or used raw `printf` that fought with the 3D presenter and caused flickering).
- No collision detection, physics, or raycasting.
- No asset loading (`.obj` loading was declared in `CMakeLists.txt` via `FetchContent(tinyobjloader)` but never referenced anywhere in C++).
- No audio subsystem or automated tests.

---

## 2. How the Legacy Pipeline Connected

```
User main() Loop (Sandbox.cpp / Asteroids_Demo.cpp)
  │
  ├─► Construct tvk::CommandBuffer
  │     └── Record DrawMesh / DrawSphere / DrawCylinder / DrawCapsule / DrawGround
  │
  ├─► tvk::Renderer::BeginFrame(camera)
  │     ├── Reset per-tile linked-list heads (m_GlobalTileTriHeads)
  │     ├── Reset per-thread triangle arena counters
  │     └── Transform up to 4 PointLights into view space
  │
  ├─► tvk::Renderer::Execute(cmdBuffer)
  │     ├── BuildRenderTasks():
  │     │     ├── Resolve primitive meshes (Sphere, Cylinder, Ground) from PrimitiveManager
  │     │     ├── Frustum-cull object AABB against 6 Gribb-Hartmann planes
  │     │     └── Populate m_VertexTasks and m_GeoTasks
  │     │
  │     ├── JobSystem Phase 1 — ExecuteVertexPhase (Parallel across worker threads):
  │     │     ├── Batch up to 1024 vertices into Structure-of-Arrays (SIMDWorkingBuffer)
  │     │     ├── Transform positions & normals via AVX2/FMA (MathSIMD.h)
  │     │     └── Compute per-vertex Blinn-Phong N·L and N·H terms into m_FrameVertices
  │     │
  │     ├── JobSystem Phase 2 — ExecuteGeometryPhase (Parallel across worker threads):
  │     │     ├── Clip triangles against Z >= NEAR_PLANE (Sutherland-Hodgman)
  │     │     ├── Project to screen space, backface cull, snap to pixel grid
  │     │     ├── Precompute screen-space gradients (dz/dx, dz/dy, d(ndotl)/dx, d(spec)/dx)
  │     │     └── Bin triangle into overlapping 64×64 tiles via lock-free CAS linked list
  │     │
  │     ├── JobSystem Phase 3 — ExecuteRasterPhase (Parallel per 64×64 tile):
  │     │     ├── Allocate 64×64 local_fb (16 KB) + local_zb (16 KB) = 32 KB on thread stack (L1 cache)
  │     │     ├── Sort tile's triangles front-to-back by max(1/z)
  │     │     ├── Rasterizer::ProcessTileBand(): 8-wide AVX2 edge test, Early-Z, FMA shade + fog
  │     │     └── TerminalPresenter::SampleTileToBackBuffer(): downsample 2×4 pixels to Braille cell
  │     │
  │     └── JobSystem Phase 4 — TerminalPresenter::BuildOutput (Parallel per terminal row band):
  │           └── Diff m_BackBuffer vs m_FrontBuffer, emit ANSI cursor jumps & 24-bit RGB sequences
  │
  └─► tvk::Renderer::Present()
        └── TerminalPresenter::FlushOutput() -> pal::FastPrint() (single stdout write)
```

---

## 3. Keep vs. Purge Decisions

### Salvaged & Reorganized (High-Value Core)
The following components are genuinely well-conceived and form the foundation of the `math`, `core`, `platform`, and `renderer` layers:
1. **L1-Cache-Bounded 64×64 Tiled Rasterization (`Rasterizer.cpp`, `Renderer.cpp`)**: Keeping the 32 KB tile framebuffer + Z-buffer on each worker thread's stack eliminates DRAM bandwidth bottlenecks and inter-thread lock contention during pixel shading.
2. **AVX2 8-Wide Analytical Scanline Rasterizer (`ProcessTileBandImpl`)**: Screen-space partial derivatives computed once per triangle + 8-pixel SIMD edge evaluation + masked Z-buffer/color stores.
3. **SoA AVX2 Vertex & Normal Transform Kernel (`MathSIMD.h`)**: 8-wide FMA matrix-vector multiplication and Newton-Raphson refined inverse square root.
4. **Lock-Free Tile Binning & Parallel Differential ANSI Presenter (`TerminalPresenter.cpp`)**: 2×4 Unicode Braille sub-pixel mapping and dirty-cell diffing that reduces terminal bandwidth from ~50 MB/s to <200 KB/s.
5. **Lock-Free Atomic Work Queue & Thread Pool (`JobSystem.h/.cpp`)**: Low-overhead fork-join parallel execution for frame phases.

### Purged (Dead / Broken Code Removed in Phase 0 Cleanup)
1. **`TVK_NotcursesPresenter.h/.cpp` (Deleted)**:
   - Did not implement pure virtual methods required by `IPresenter` (`PreFrame`, `SampleTileToBackBuffer`, `BuildOutput`, `FlushOutput`)—could not compile when enabled.
   - Relied on the legacy full-screen `Present(framebuffer, zbuffer, ...)` method, which is never called by the tiled renderer (`m_ActiveRasterCtx.framebuffer` is `nullptr`).
   - Required an untracked local directory (`notcurses-master/notcurses-master`) hardcoded in `CMakeLists.txt`.
   - Called `std::exit(EXIT_SUCCESS)` directly inside `Present()`.
2. **`TVK_Material.h` (Deleted)**:
   - Completely dead header never `#include`d by any file in the repository (`MaterialState` in `CommandBuffer.h` was used instead).
3. **Unwired `tinyobjloader` CMake `FetchContent` (Removed from `CMakeLists.txt`)**:
   - Downloaded a third-party repo during CMake configure and defined `TVK_OBJ_LOADER_AVAILABLE`, which was never referenced anywhere in the codebase.

---

## 4. Catalog of Bugs & Architectural Flaws Discovered (To Fix in Phases 1–3)

### Critical Build & Memory Defects (Fixed in Phase 0 Foundation Setup)
| ID | Location | Issue | Status |
| :--- | :--- | :--- | :--- |
| **B-01** | `CMakeLists.txt` | Hardcoded `-lwinpthread` for all non-MSVC builds (`else()`), breaking compilation on Linux and macOS. | **Fixed in Phase 0** (Scoped to `if(MINGW)`). |
| **B-02** | `Renderer.h` | `RendererDescriptor` defaulted to `MaxTrisPerThread = 2,000,000` (`132 B` each) and `MaxPtsPerThread = 1,000,000` (`20 B` each) *per worker thread*. On a 16-thread CPU, startup immediately allocated **~4.5 GB of RAM** for arenas that held <5,000 triangles. | **Fixed in Phase 0** (Scoped to sane defaults: `65,536` tris/thread). |
| **B-03** | `Sandbox.cpp`, `Asteroids_Demo.cpp` | Unused variables (`panX`, `dt`) and member initializer reorder warnings under `-Wall -Wextra`. | **Fixed in Phase 0**. |

### Critical Rendering, Math & Platform Bugs (Scheduled for Phase 1–3 Refactoring)
| ID | Location | Severity | Detailed Technical Description | Target Phase |
| :--- | :--- | :--- | :--- | :--- |
| **B-04** | `src/renderer/Renderer.cpp` (`BuildRenderTasks` L215, `TransformMeshVertices` L360) | **High** | **Per-Vertex Color Destruction Bug**: `DrawMesh` leaves `cmd.color = {255,255,255,255}`. `TransformMeshVertices` unconditionally overwrites `sv.color = albedo_override`, destroying `Vertex::color`. As a result, all colored meshes in `Sandbox.cpp` (bricks, question blocks, pipes, coins, grass) and `Asteroids_Demo.cpp` render pure white instead of their vertex colors. | **Phase 2** |
| **B-05** | `src/renderer/Renderer.cpp` (`BuildRenderTasks` L224–241) | **High** | **Broken Cylinder & Capsule Orientation & Shape**: `DrawCylinder` computes `Vec3 axis = Normalize(Sub(cmd.b, cmd.a)); (void)axis;` and discards it, always rendering an unrotated vertical cylinder. `DrawCapsule` also draws the exact same flat-capped cylinder without hemisphere caps. | **Phase 2** |
| **B-06** | `src/renderer/Renderer.cpp` (`BuildRenderTasks` L248) | **Medium** | **`CommandType::DrawLine` is a Silent No-Op**: `CommandBuffer::DrawLine` records commands, and `Renderer` allocates `m_GlobalPtArena`, but `BuildRenderTasks` hits `else { continue; }` and ignores all line commands. | **Phase 2** |
| **B-07** | `src/renderer/Renderer.cpp` (`BuildRenderTasks` L267–274) | **Medium** | **Incorrect Normal Matrix under Non-Uniform Scale**: `m_FrameNormalTransforms` copies the upper-left 3×3 of `view * model` directly instead of computing the inverse-transpose $(M_{3\times3}^{-1})^T$. Non-uniformly scaled meshes (such as cylinders, stretched boxes, or ground planes) suffer distorted lighting normals. | **Phase 2** |
| **B-08** | `src/renderer/Rasterizer.cpp` (`RasterizeProjectedTriangle` L118–130) | **High** | **Orthographic Projection Ignored by Rasterizer**: `Camera::GetProjectionMatrix` builds both Perspective and Orthographic matrices, but `RasterizeProjectedTriangle` only reads `proj_matrix.m[0][0]` and `proj_matrix.m[1][1]` and hardcodes perspective division by `z`, breaking orthographic cameras and ignoring `m_Near`/`m_Far` matrix terms. | **Phase 2** |
| **B-09** | `src/renderer/Rasterizer.cpp` (`ClipAgainstNearPlane` L35) | **Medium** | **Hardcoded `NEAR_PLANE = 4.0f`**: Near-plane clipping ignores `Camera::GetNear()` and always clips at `z = 4.0f`. | **Phase 2** |
| **B-10** | `src/renderer/Renderer.cpp` (`TransformMeshVertices` L363–384) | **Medium** | **Monochrome Lighting & Single-Light Specular**: Multi-light diffuse collapses RGB light colors into scalar luminance (`0.2126R + 0.7152G + 0.0722B`), making colored lights impossible. Specular (`nDotH`) only evaluates light `0`. | **Phase 2** |
| **B-11** | `src/renderer/TerminalPresenter.cpp` (`BuildOutput` L163–172) | **High** | **Data Race on Static Lookup Table Initialization**: `static bool init_lut = false;` is checked and populated inside `BuildOutput()`, which is invoked concurrently across all worker threads by `JobSystem::Execute`. | **Phase 1 / 3** |
| **B-12** | `src/renderer/TerminalPresenter.cpp` (`SampleBrailleCell` L99–101, `BuildOutput` L179) | **Medium** | **Harsh 3-Bit Color Quantization & Hollow Braille Cells**: `SampleBrailleCell` masks RGB with `& 0xE0` (reducing 24-bit color to 8 levels per channel) and only emits foreground color against a black background, making solid surfaces appear stippled/transparent instead of solid. | **Phase 3** |
| **B-13** | `src/platform/PAL.cpp` (`SetupTerminal`, `PollKey`) | **High** | **Missing POSIX `termios` Raw Mode & Signal Recovery**: On Linux/macOS, `SetupTerminal()` never disables canonical mode (`ICANON`) or echo (`ECHO`), so `PollKey()` requires pressing Enter and echoes typed keys onto the canvas. Pressing `Ctrl+C` leaves the user's terminal trapped in the alternate screen buffer with the cursor hidden. | **Phase 1** |
| **B-14** | `src/renderer/Renderer.cpp` (`DrawTriangle` L549–624) | **Low** | **Dead Duplicate Code**: `Renderer::DrawTriangle` is an unused 75-line copy of the inner loop of `Renderer::DrawMesh`, and `HybridBarrier` in `JobSystem.h` is unused. | **Phase 1 / 2** |
| **B-15** | `include/tvk/core/Stats.h` | **Low** | **Unpopulated Frame Stats**: `trianglesCulled` and `trianglesRasterized` exist in `FrameStats` but are never incremented anywhere in the pipeline. | **Phase 2** |
| **B-16** | `include/tvk/math/MathSIMD.h` | **High** | **No Scalar Fallback for Non-AVX2 Targets**: `MathSIMD.h` and `Rasterizer.cpp` unconditionally `#include <immintrin.h>` and call AVX2 intrinsics without `#ifdef __AVX2__` guards or a portable scalar/NEON fallback path. | **Phase 2** |
