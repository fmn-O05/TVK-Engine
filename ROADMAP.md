# TVK Engine — Master Refactoring & Architectural Roadmap

This document defines the step-by-step engineering roadmap to transform **TVK Engine** from an experimental 3D software rasterizer into a complete, production-ready **Terminal Game Engine**.

For the forensic analysis of the legacy code and bug catalog, see [`docs/CODEBASE_AUDIT.md`](docs/CODEBASE_AUDIT.md).  
For the full 10-layer system architecture blueprint, see [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md).

---

## Roadmap Overview

| Phase | Milestone Name | Core Focus | Status |
| :--- | :--- | :--- | :--- |
| **Phase 0** | **Foundation, Audit & Structural Cleanup** | Audit legacy code, purge dead/broken files, reorganize into 10 modular layers, fix Linux/macOS CMake build & 4.5 GB startup RAM bug. | **COMPLETED** |
| **Phase 1** | **Platform Layer (PAL), Input, Terminal Safety & Core Runtime** | POSIX `termios` raw mode, RAII crash-safe `TerminalGuard`, CSI Keyboard + SGR Mouse input parser, `FrameLinearArena`, `EventBus`, `Log`, `Engine` loop, CTest harness. | **READY FOR GREEN LIGHT** |
| **Phase 2** | **3D Software Rasterizer & Math Pipeline Overhaul** | Fix vertex color bug (`B-04`), normal matrix (`B-07`), ortho projection (`B-08`), oriented Cylinder/Capsule/Box/Cone primitives (`B-05`), 3D line rasterization (`B-06`), RGB lighting (`B-10`), scalar SIMD fallback (`B-16`). | Planned |
| **Phase 3** | **Multi-Blitter Presenter, 2D Canvas & Debug Profiler HUD** | Unified `TerminalCell` (FG+BG+UTF8), Solid FG/BG Braille + HalfBlock + Sextant + ASCII blitters, `Canvas2D` text/box/HUD compositor, toggleable `F1` Profiler Overlay. | Planned |
| **Phase 4** | **Entity-Component-System (ECS) & Scene Graph** | Generational `Entity`, sparse-set `ComponentPool<T>`, `Registry`, hierarchical `TransformComponent`, `Scene` lifecycle & automatic render extraction. | Planned |
| **Phase 5** | **Physics, Collision Detection & 3D Terminal Mouse Picking** | Sphere/AABB/OBB/Capsule colliders, Spatial Hash broadphase, analytical narrowphase + impulse solver, `CharacterController`, `Raycast` & terminal mouse picking. | Planned |
| **Phase 6** | **Asset Pipeline, Textures, Materials & Immediate-Mode TUI** | Zero-dependency `.obj`/`.mtl` loader, UV texture mapping & procedural textures, `AssetManager` handles, immediate-mode TUI widgets (`Button`, `Panel`, `Slider`, `Menu`). | Planned |
| **Phase 7** | **Audio Subsystem, Polish & Flagship Terminal Game Showcase** | Procedural retro chiptune/SFX synthesizer + audio mixer, scene serialization, benchmark suite, and complete playable terminal game built on the full engine stack. | Planned |

---

## Phase 0: Foundation, Audit & Structural Cleanup (Completed)

### What Was Done
1. **Forensic Codebase Audit ([`docs/CODEBASE_AUDIT.md`](docs/CODEBASE_AUDIT.md))**:
   - Audited all 32 files in the repository and documented 16 concrete bugs, memory defects, data races, and dead stubs (`B-01` through `B-16`).
2. **Purged Dead / Broken Legacy Files**:
   - Removed `TVK_NotcursesPresenter.h/.cpp` (did not implement pure virtual `IPresenter` methods, called `std::exit()`, relied on untracked local directory).
   - Removed `TVK_Material.h` (unused duplicate header never `#include`d anywhere).
   - Removed unwired `FetchContent(tinyobjloader)` from `CMakeLists.txt`.
3. **Reorganized Repository into Clean 10-Layer Architecture**:
   - Moved flat `include/tvk/TVK_*.h` and `src/tvk/TVK_*.cpp` into modular subsystem directories:
     - `include/tvk/core/` & `src/core/`
     - `include/tvk/platform/` & `src/platform/`
     - `include/tvk/math/` & `src/math/`
     - `include/tvk/renderer/` & `src/renderer/`
   - Scaffolded directories for all upcoming engine layers (`scene/`, `physics/`, `ui/`, `assets/`, `audio/`, `app/`, `tests/`, `docs/`) and created the umbrella header [`include/tvk/TVK.h`](include/tvk/TVK.h).
4. **Fixed Critical Build & Memory Defects (`B-01`, `B-02`, `B-03`)**:
   - Fixed `CMakeLists.txt` so `-lwinpthread` is only linked under `MINGW`, enabling clean Linux, macOS, and Windows builds.
   - Reduced `RendererDescriptor` default per-thread triangle arena from `2,000,000` to `65,536` (dropping startup memory allocation on 16-thread CPUs from **~4.5 GB** down to **~140 MB**, ahead of Phase 1's `FrameLinearArena`).
   - Eliminated all compiler warnings under `-Wall -Wextra`.

---

## Phase 1: Platform Layer (PAL), Input System, Terminal Safety & Core Runtime

### Objectives
Make the terminal environment rock-solid on Linux, macOS, and Windows, introduce real-time non-blocking keyboard + mouse input, eliminate heap/arena bloat, and establish the core `tvk::Engine` game loop and unit test suite.

### Deliverables & Files to Implement
1. **Crash-Safe Terminal Lifecycle (`include/tvk/platform/TerminalGuard.h`, `src/platform/PAL.cpp`)**:
   - Fix `B-13`: Implement POSIX `termios` raw mode (`~(ICANON | ECHO | ISIG | IXON)`, `VMIN = 0`, `VTIME = 0`) and Win32 Console input/output mode restoration.
   - Register signal handlers (`SIGINT`, `SIGTERM`, `SIGABRT`, `SIGSEGV`, `SIGWINCH`) and `std::atexit` hook so the alternate screen buffer (`\x1b[?1049l`), cursor visibility (`\x1b[?25h`), mouse tracking reset (`\x1b[?1000l\x1b[?1002l\x1b[?1006l`), and terminal attributes are **always** restored on exit or crash.
2. **Full Keyboard & SGR Mouse Input System (`include/tvk/platform/Input.h`, `src/platform/Input.cpp`)**:
   - Non-blocking input buffer reader with ANSI CSI state-machine parser:
     - Standard keys (`A-Z`, `0-9`, `Space`, `Enter`, `Tab`, `Backspace`, `Escape`).
     - Special keys (`Up`, `Down`, `Left`, `Right`, `Home`, `End`, `PageUp`, `PageDown`, `Delete`, `F1`–`F12`).
     - Modifiers (`Shift`, `Ctrl`, `Alt`).
     - SGR 1006 Mouse protocol (`\x1b[<btn;x;yM` / `m`): left/middle/right button states, cell coordinates, sub-pixel coordinates, drag delta, and scroll wheel.
   - Query API: `Input::IsKeyDown(Key)`, `Input::IsKeyPressed(Key)`, `Input::IsKeyReleased(Key)`, `Input::GetMousePosition()`, `Input::GetMouseScrollDelta()`.
   - Action mapping API: `Input::BindAction("Jump", Key::Space)` / `Input::IsActionPressed("Jump")`.
3. **Core Runtime Primitives (`include/tvk/core/`)**:
   - `Memory.h`: `FrameLinearArena` (bounded per-frame bump allocator) and generational `PoolAllocator`.
   - `Time.h`: High-resolution `Clock`, frame delta smoothing, FPS tracker, and fixed-timestep accumulator (`TimestepAccumulator`).
   - `EventBus.h`: Zero-overhead typed event bus (`WindowResizeEvent`, `KeyEvent`, `MouseButtonEvent`, `MouseMoveEvent`).
   - `Log.h`: Thread-safe ring-buffer logger (never writes to `stdout` during active terminal rendering).
4. **Engine & Application Host (`include/tvk/app/Engine.h`, `include/tvk/app/Application.h`, `src/app/Engine.cpp`)**:
   - Standard game lifecycle: `OnInit()`, `OnFixedUpdate(float fixedDt)`, `OnUpdate(float dt)`, `OnRender(CommandBuffer&)`, `OnUI(Canvas2D&)`, `OnShutdown()`.
   - Built-in `ESC` / `Ctrl+C` clean exit handling and automatic terminal resize handling.
5. **Automated Test Suite Setup (`tests/`)**:
   - Lightweight zero-dependency test runner integrated with CMake `ctest` (`TVK_BUILD_TESTS=ON`).
   - Unit tests for `FrameLinearArena`, `EventBus`, ANSI CSI input parser, and `Time`.

### Phase 1 Verification Criteria
- Pressing keys (WASD, arrows, F-keys, Space, Escape) and clicking/moving the mouse in the terminal are detected instantaneously without pressing Enter or echoing characters.
- Pressing `Ctrl+C` or `ESC` cleanly exits and restores the user's shell prompt and cursor.
- `ctest --output-on-failure` passes 100% of Phase 1 unit tests.

---

## Phase 2: 3D Software Rasterizer & Math Pipeline Overhaul

### Objectives
Fix every mathematical, lighting, projection, and primitive-generation bug in the 3D rendering pipeline (`B-04` through `B-10`, `B-14` through `B-16`) and add 3D line/wireframe rendering and complete geometric primitives.

### Deliverables & Files to Implement
1. **Math & SIMD Portability (`include/tvk/math/Math.h`, `include/tvk/math/MathSIMD.h`)**:
   - Add ergonomic C++ operator overloads (`+`, `-`, `*`, `/`) for `Vec2`, `Vec3`, `Vec4`, `Mat4`, `Quat`.
   - Add `Mat3` and `Mat3 InverseTranspose(const Mat4&)` to fix `B-07` (normal distortion under non-uniform scaling).
   - Add `#if defined(__AVX2__)` guards with a portable scalar fallback in `MathSIMD.h` and `Rasterizer.cpp` (`B-16`).
2. **Vertex Color & Multi-Light RGB Shading (`src/renderer/Renderer.cpp`, `src/renderer/Rasterizer.cpp`)**:
   - Fix `B-04`: Multiply per-vertex `Vertex::color` with `RenderCommand::color` (material tint) instead of overwriting vertex colors with white.
   - Fix `B-10`: Accumulate full RGB diffuse and RGB specular lighting across all active lights (Directional, Point, and Spot lights with distance attenuation).
   - Support `ShadingModel::{Unlit, Flat, Gouraud, BlinnPhong, Toon}` in `PipelineState`.
3. **Unified Projection & Frustum Clipping (`src/renderer/Rasterizer.cpp`, `src/renderer/Camera.cpp`)**:
   - Fix `B-08` & `B-09`: Clip against `camera.GetNear()` (not hardcoded `4.0f`) and transform through the full 4×4 `proj_matrix` so both **Perspective** and **Orthographic** cameras render accurately.
4. **Complete & Properly Oriented Geometric Primitives (`include/tvk/renderer/Primitives.h`, `src/renderer/Primitives.cpp`)**:
   - Fix `B-05`: Construct an orthonormal basis from `normalize(b - a)` so `DrawCylinder(a, b, r)` and `DrawCapsule(a, b, r)` orient accurately along any 3D segment $\vec{a} \to \vec{b}$.
   - Build a true `CreateCapsule` mesh (cylinder body + 2 hemisphere end-caps) and add `CreateBox`, `CreateCone`, `CreateTorus`, and `CreatePlane`.
5. **3D Line & Debug Wireframe Rasterizer (`src/renderer/Rasterizer.cpp`)**:
   - Fix `B-06`: Implement near-plane line clipping and tile-binned depth-tested 3D line rasterization (`DrawLine`, `DrawWireBox`, `DrawWireSphere`, `DrawGrid`, `DrawAxes`).
6. **Accurate Telemetry (`include/tvk/core/Stats.h`)**:
   - Fix `B-14` & `B-15`: Remove dead `Renderer::DrawTriangle` duplicate and populate `trianglesCulled`, `trianglesRasterized`, and `linesRasterized`.

### Phase 2 Verification Criteria
- `Sandbox` and `Asteroids_Demo` render with their true vertex colors, accurate non-uniformly scaled normals, properly oriented capsules/cylinders, colored lights, and 3D debug lines/grids.
- Both Perspective and Orthographic camera projections pass headless golden-buffer unit tests.

---

## Phase 3: Multi-Blitter Presenter, 2D Canvas & Debug Profiler HUD

### Objectives
Upgrade terminal visual quality dramatically via dual-color (FG+BG) cell blitting, add a flicker-free 2D text/HUD canvas composited directly into the terminal cell buffer, and build an in-engine `F1` profiler overlay.

### Deliverables & Files to Implement
1. **Upgraded `TerminalPresenter` & Multi-Blitter Architecture (`include/tvk/renderer/TerminalPresenter.h`, `src/renderer/TerminalPresenter.cpp`)**:
   - Fix `B-11` (thread-safe `constexpr` byte-to-ASCII lookup table initialized at compile time, eliminating the static init data race).
   - Fix `B-12`: Upgrade `TerminalCell` to store `char utf8[4]`, `Color fg`, and `Color bg` (`\x1b[38;2;R;G;B;48;2;R;G;Bm`).
   - Implement runtime-switchable blitters (`F2` hotkey):
     - **`BlitterType::Braille` (2×4 sub-pixels)**: 2-color FG/BG clustering per 2×4 block so solid surfaces look completely filled (no black pinholes between dots) while preserving 2×4 sub-pixel silhouette resolution.
     - **`BlitterType::HalfBlock` (`▀`, 1×2 sub-pixels)**: True 24-bit per-subpixel color accuracy.
     - **`BlitterType::Quadrant` / `Sextant` (2×2 / 2×3 sub-pixels)**: Block-graphic blitters.
     - **`BlitterType::ASCII` (1×1 cell)**: Retro ASCII ramp blitter.
   - Add optional 4×4 ordered Bayer dithering to eliminate color banding in terminal gradients.
2. **2D Terminal Canvas Compositor (`include/tvk/ui/Canvas2D.h`, `src/ui/Canvas2D.cpp`)**:
   - Cell-space 2D drawing API executed after 3D tile blitting and before differential ANSI generation:
     - `DrawText(x, y, text, fg, bg_mode)` (supports transparent background that preserves the 3D scene's background color under the text!).
     - `DrawRect` / `DrawBox` (Unicode box-drawing characters: Single `┌─┐`, Double `╔═╗`, Rounded `╭─╮`, Heavy `┏━┓`).
     - `DrawProgressBar`, `DrawSparkline`, `DrawCrosshair`.
3. **In-Terminal Debug & Profiler Overlay (`include/tvk/app/DebugOverlay.h`, `src/app/DebugOverlay.cpp`)**:
   - Toggleable with `F1`: displays real-time FPS, frame time sparkline, phase breakdown (`Vertex`, `Geometry/Bin`, `Tile Raster`, `2D/UI`, `ANSI Diff/Flush`), triangle/tile counts, active blitter mode, terminal dimensions, and recent `Log` messages.

### Phase 3 Verification Criteria
- Zero HUD flickering (no raw `printf` calls outside the presenter).
- Runtime switching between Braille (FG+BG), Half-Block, Quadrant/Sextant, and ASCII blitters works seamlessly via `F2`.
- `F1` toggles the live profiler overlay cleanly over any 3D scene.

---

## Phase 4: Entity-Component-System (ECS) & Scene Graph

### Objectives
Provide a clean, cache-friendly ECS and hierarchical Scene Graph so developers build games by composing entities and components rather than manually recording low-level `CommandBuffer` matrices.

### Deliverables & Files to Implement
1. **Core ECS (`include/tvk/scene/Entity.h`, `ComponentPool.h`, `Registry.h`)**:
   - Generational `Entity` handle (`uint32_t index`, `uint32_t generation`).
   - Contiguous sparse-set `ComponentPool<T>` supporting $O(1)$ insertion, removal, lookup, and cache-friendly `View<Components...>::Each(...)` iteration.
2. **Built-In Engine Components (`include/tvk/scene/Components.h`)**:
   - `TagComponent`, `TransformComponent` (local `position`, `rotation` Quat, `scale`, cached `worldMatrix`), `HierarchyComponent` (parent/children links).
   - `MeshRendererComponent`, `PrimitiveRendererComponent`, `CameraComponent`, `LightComponent`.
3. **Scene & Built-In Systems (`include/tvk/scene/Scene.h`, `src/scene/Scene.cpp`)**:
   - `TransformSystem`: Topological parent-to-child world matrix propagation.
   - `RenderSystem`: Iterates visible render components and populates `CommandBuffer` + lights + active camera automatically.

### Phase 4 Verification Criteria
- Unit tests verify $O(1)$ entity creation/destruction, generational handle invalidation, sparse-set views, and multi-level parent-child transform propagation.
- Both `Sandbox` and `Asteroids_Demo` are refactored to run cleanly on `tvk::Application` + `tvk::Scene`.

---

## Phase 5: Physics, Collision Detection & 3D Terminal Mouse Picking

### Objectives
Enable interactive gameplay with 3D/2.5D rigid-body dynamics, collision events, character movement, and 3D raycasting from terminal mouse clicks.

### Deliverables & Files to Implement
1. **Colliders & RigidBody Components (`include/tvk/physics/Colliders.h`, `RigidBody.h`)**:
   - `SphereCollider`, `AABBCollider`, `OBBCollider`, `CapsuleCollider`.
   - `RigidBodyComponent` (`BodyType::{Static, Kinematic, Dynamic}`, mass, linear/angular velocity, restitution, friction, gravity scale).
2. **Broadphase & Narrowphase Collision (`include/tvk/physics/SpatialHash.h`, `Narrowphase.h`)**:
   - Uniform Spatial Hash Grid broadphase.
   - Analytical narrowphase contact manifold generation (Sphere-Sphere, Sphere-AABB, AABB-AABB, Capsule-AABB, Capsule-Sphere).
3. **Impulse Solver, Character Controller & Raycasting (`include/tvk/physics/PhysicsWorld.h`, `Raycast.h`)**:
   - Fixed-timestep sequential impulse solver + positional penetration correction.
   - `CharacterController` (ground detection, slope sliding, step climbing, jumping).
   - `PhysicsWorld::Raycast(Ray, maxDist, RaycastHit&)` + `Camera::ScreenCellToRay(cell_x, cell_y)` for clicking on 3D entities in the terminal with the mouse.

### Phase 5 Verification Criteria
- Unit tests cover all collider pair intersections, restitution bounce, and raycast hit distances/normals.
- Clicking a 3D object in the terminal highlights/selects the entity via `ScreenCellToRay` + `PhysicsWorld::Raycast`.

---

## Phase 6: Asset Pipeline, Textures, Materials & Immediate-Mode TUI

### Objectives
Allow games to load external 3D `.obj` models and 2D textures, render textured/PBR-lite materials in the terminal, and build interactive game menus and inventories with an immediate-mode TUI toolkit.

### Deliverables & Files to Implement
1. **Resource Handles & AssetManager (`include/tvk/assets/Handle.h`, `AssetManager.h`)**:
   - Generational `MeshHandle`, `TextureHandle`, `MaterialHandle` with centralized lifetime management.
2. **Wavefront `.obj` / `.mtl` Loader (`include/tvk/assets/ObjLoader.h`, `src/assets/ObjLoader.cpp`)**:
   - Fast, zero-dependency parser supporting vertex positions, normals, UVs, vertex colors, and arbitrary polygon fan triangulation.
3. **2D Texture Sampling & Procedural Textures (`include/tvk/assets/Texture.h`, `src/assets/Texture.cpp`)**:
   - RGBA texture storage + perspective-correct UV interpolation inside `Rasterizer::ProcessTileBandImpl`.
   - Built-in procedural texture generators (checkerboard, brick, starfield, Perlin/value noise) so games can have rich surface detail even without external files.
4. **Immediate-Mode Terminal UI (`include/tvk/ui/UIContext.h`, `Theme.h`, `src/ui/UIContext.cpp`)**:
   - `BeginPanel`, `EndPanel`, `Label`, `Button`, `Checkbox`, `SliderFloat`, `ProgressBar`, `ListBox`.
   - Full keyboard (`Tab`/`Arrow`/`Enter`) and SGR mouse hover/click interaction.

### Phase 6 Verification Criteria
- External `.obj` meshes and procedural/image textures render with perspective-correct UV mapping at 60 FPS.
- Interactive TUI pause menus, settings sliders, and HUD panels respond to both mouse clicks and keyboard navigation.

---

## Phase 7: Audio Subsystem, Polish & Flagship Terminal Game Showcase

### Objectives
Complete the engine with retro sound synthesis/audio playback, scene serialization, benchmark automation, and a flagship playable game demonstrating every engine subsystem working together.

### Deliverables & Files to Implement
1. **Audio Subsystem (`include/tvk/audio/AudioEngine.h`, `Sound.h`, `src/audio/AudioEngine.cpp`)**:
   - Built-in procedural retro waveform synthesizer (square, triangle, saw, noise oscillators with pitch sweep and ADSR envelopes) + WAV playback and 3D distance panning/attenuation.
2. **Headless Benchmark & Golden-Frame CI Runner (`tests/benchmark_runner.cpp`)**:
   - Automated headless benchmark measuring Vertex, Geometry, Raster, and ANSI Diff timings across resolutions (`80×24`, `160×50`, `240×80`).
3. **Flagship Playable Terminal Game (`examples/`)**:
   - Replace passive tech-demo loops with a complete, playable 3D terminal game featuring player controls (keyboard + mouse), physics & collisions, 3D models/textures, particle effects, HUD & pause menu (`tvk/ui`), sound effects (`tvk/audio`), and `F1`/`F2` live engine diagnostics.
