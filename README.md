# TVK Engine (Terminal Vulkan Engine)

[![Standard](https://img.shields.io/badge/C%2B%2B-17%20%2F%2020-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B17)
[![Architecture](https://img.shields.io/badge/SIMD-AVX2%20%2B%20Scalar%20Fallback-orange.svg)](#core-rendering--engine-technology)
[![Target](https://img.shields.io/badge/Target-60%2B%20FPS%20Terminal-brightgreen.svg)](#building--testing-from-source)
[![License](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)

**TVK Engine** is a high-performance, zero-dependency C++17/C++20 3D/2.5D/2D game engine engineered to run real-time interactive games directly inside modern command-line terminals at a steady **60+ FPS**.

Built around an **L1-cache-bounded 64×64 tiled software rasterizer**, **guarded AVX2/FMA SIMD vectorization with bit-compatible scalar fallbacks**, **multi-blitter 24-bit TrueColor sub-pixel Unicode rendering (`Braille` 2-color FG+BG, `HalfBlock`, `Quadrant`, `Sextant`, `ASCII`)**, and **DECSET 2026 synchronized differential ANSI streaming**, TVK Engine is architected from the ground up as a modular 10-layer terminal game engine.

---

## Documentation & Technical Specifications

### Core Architecture & Roadmap
- **[`ROADMAP.md`](ROADMAP.md)** — Master phased implementation plan (`Phase 0` through `Phase 7`) with quantitative performance and verification gates.
- **[`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md)** — Complete 10-layer engine architecture blueprint, industry benchmark rationale, Left-Handed `+Z` coordinate conventions, `< 16 MB` memory budget, and `-fno-exceptions` contract.
- **[`docs/CODEBASE_AUDIT.md`](docs/CODEBASE_AUDIT.md)** — Multi-pass forensic audit, industry benchmark comparison matrix, and complete catalog of **all 32 defects (`B-01` through `B-32`)** resolved in the baseline foundation.

### Granular Subsystem Technical Specifications (`docs/specs/`)
- **[`docs/specs/01_CORE_AND_PLATFORM.md`](docs/specs/01_CORE_AND_PLATFORM.md)** — Generational `Handle<Tag>`, `Result<T,E>`, crash-safe `TVK_ASSERT`, `FrameLinearArena`, `JobSystem::ParallelFor`, `TimestepAccumulator` ($\alpha$), async-signal-safe `TerminalGuard`, and ANSI CSI / Kitty `\x1b[>1u` / SGR 1006 mouse state machine.
- **[`docs/specs/02_MATH_AND_RASTERIZER.md`](docs/specs/02_MATH_AND_RASTERIZER.md)** — Left-Handed `+Z` `LookAt` & `Orthographic` derivations, `Mat4NormalMatrix`, Duff et al. branchless ONB, AVX2 + scalar `MathSIMD.h`, 64-bit `SortKey`, guard-band clipping, `int64_t` signed area, top-left fill convention, full RGB multi-light shading, and 3D line DDA rasterization.
- **[`docs/specs/03_PRESENTER_AND_CANVAS2D.md`](docs/specs/03_PRESENTER_AND_CANVAS2D.md)** — 16-byte `TerminalCell`, 5 sub-pixel blitters (`Braille` 2×4 2-color luminance clustering, `HalfBlock` 1×2, `Quadrant` 2×2, `Sextant` 2×3, `ASCII` 1×1), 4×4 Bayer dithering, OKLab palette LUT, exact front-buffer diffing, `DECSET 2026` framing, `Canvas2D`, and `DebugOverlay`.
- **[`docs/specs/04_ECS_AND_SCENE.md`](docs/specs/04_ECS_AND_SCENE.md)** — 64-bit generational `Entity`, paged sparse-set `ComponentPool<T>`, smallest-pool lead `View<Ts...>`, deferred `EntityCommandBuffer`, topological `TransformSystem`, and $\alpha$-interpolated `RenderSystem`.
- **[`docs/specs/05_PHYSICS_AND_PICKING.md`](docs/specs/05_PHYSICS_AND_PICKING.md)** — `Sphere`/`AABB`/`OBB`/`Capsule` colliders, arena-allocated `SpatialHashGrid`, analytical narrowphase manifolds, 8-iteration sequential impulse solver with Baumgarte stabilization, `CharacterController`, and `ScreenCellToRay` 3D mouse picking.
- **[`docs/specs/06_ASSETS_UI_AND_AUDIO.md`](docs/specs/06_ASSETS_UI_AND_AUDIO.md)** — `AssetManager`, zero-dependency Wavefront `.obj`/`.mtl` parser, `Texture` sampling & procedural generators, immediate-mode `UIContext` ID stack & layout, and zero-dependency OS audio backends + procedural `SynthPatch` ADSR synthesizer.
- **[`docs/specs/07_TESTING_AND_BENCHMARKS.md`](docs/specs/07_TESTING_AND_BENCHMARKS.md)** — `TestHarness.h`, unit and headless golden-frame test suites, AVX2-vs-Scalar equivalence matrix, and quantitative CPU/memory benchmark budgets.

---

## Repository Structure

```text
TVK-Engine/
├── CMakeLists.txt                     # Cross-platform CMake build (Linux, macOS, Windows; SIMD ON/OFF; CTest)
├── ROADMAP.md                         # Master phased implementation roadmap & verification gates
├── docs/
│   ├── ARCHITECTURE.md                # 10-layer engine architecture blueprint
│   ├── CODEBASE_AUDIT.md              # Multi-pass forensic audit & catalog of 32 resolved defects (B-01..B-32)
│   └── specs/                         # 7 granular subsystem implementation specifications (01..07)
├── include/
│   └── tvk/
│       ├── TVK.h                      # Umbrella convenience header
│       ├── core/                      # Layer 0: Types, Handle, Result, Assert, JobSystem, Stats (+ Arena, Time, EventBus, Log)
│       ├── platform/                  # Layer 1: Terminal PAL, Raw Mode & Signal Recovery (+ TerminalGuard, Input)
│       ├── math/                      # Layer 2: Vec/Mat/Quat/AABB/Frustum/Ray, NormalMatrix, ONB, AVX2/Scalar SIMD
│       ├── renderer/                  # Layer 3: Tiled 3D Software Rasterizer, 3D Line DDA & 5-Blitter Terminal Presenter
│       ├── scene/                     # Layer 4: Entity-Component-System (ECS) & Scene Graph [Phase 4]
│       ├── physics/                   # Layer 5: Colliders, Spatial Hash, Solver & Mouse Raycasting [Phase 5]
│       ├── ui/                        # Layer 6: 2D Terminal Canvas & Immediate-Mode TUI [Phase 3 & 6]
│       ├── assets/                    # Layer 7: .OBJ/.MTL Loader, Textures & AssetManager [Phase 6]
│       ├── audio/                     # Layer 8: Retro Synth & Cross-Platform Audio Mixer [Phase 7]
│       └── app/                       # Layer 9: Engine Loop, Application Host & F1 Debug Overlay [Phase 1 & 3]
├── src/                               # Subsystem implementations mirroring include/tvk/ 1-to-1
├── examples/                          # Showcase executables (Sandbox.cpp, Asteroids_Demo.cpp)
└── tests/                             # CTest automated verification suite (TestHarness.h, test_core, test_math, test_renderer)
```

---

## Core Rendering & Engine Technology

1. **L1 Cache-Bounded Tiled Software Rasterizer**:
   Viewport rendering is partitioned into cell-aligned pixel tiles (`64×64` max sub-pixels: `16 KB` color + `16 KB` Z-buffer = `32 KB` per worker thread stack), fitting entirely inside L1 data cache with zero inter-thread pixel lock contention.
2. **Water-Tight Guard-Band Clipping & Top-Left Fill Convention**:
   Dynamic near-plane clipping (`camera.GetNear()`), Perspective & Orthographic projection, 2D guard-band clamping (`[-16384, +16384]`), `int64_t` signed area evaluation, and D3D/Larrabee `-1` top-left fill bias.
3. **Guarded AVX2/FMA 8-Wide SIMD + Portable Scalar Fallbacks**:
   SoA vertex/normal transformations (with zero-length normal `NaN` protection) and 8-pixel wide scanline stepping with automatic scalar fallback when compiled with `-DTVK_ENABLE_SIMD=OFF` or on `ARM64`.
4. **5 Runtime-Switchable Unicode Blitters & `DECSET 2026` Synchronized Output**:
   Supports `Braille` ($2\times 4$ with 24-bit 2-color FG+BG luminance clustering and zero solid-surface pinholes), `HalfBlock` ($1\times 2$), `Quadrant` ($2\times 2$), `Sextant` ($2\times 3$), and `ASCII` ($1\times 1$), diffed in parallel against the frontbuffer and flushed atomically inside `\x1b[?2026h` ... `\x1b[?2026l`.

---

## Building & Testing from Source

### Prerequisites
- C++17 / C++20 compiler (`GCC 9+`, `Clang 10+`, or `MSVC 2019+`)
- `CMake 3.20+`
- Modern UTF-8 / 24-bit TrueColor terminal (`Kitty`, `WezTerm`, `Alacritty`, `Windows Terminal`, `Ghostty`, or `VS Code Terminal`)

### Build & Run Automated CTest Suite (Linux / macOS / Windows)
```bash
# Configure Release build with AVX2 SIMD and unit tests enabled
cmake -B build -DCMAKE_BUILD_TYPE=Release -DTVK_ENABLE_SIMD=ON -DTVK_BUILD_TESTS=ON
cmake --build build -j

# Run the automated unit & headless rendering test suite
ctest --test-dir build --output-on-failure

# Run interactive demos (press 'q' or ESC to exit cleanly; or pass --frames N for bounded runs)
./build/sandbox
./build/asteroids
```

### Portable Scalar Build (Non-AVX2 / ARM64 / CI Verification)
```bash
cmake -B build_scalar -DCMAKE_BUILD_TYPE=Release -DTVK_ENABLE_SIMD=OFF -DTVK_BUILD_TESTS=ON
cmake --build build_scalar -j
ctest --test-dir build_scalar --output-on-failure
```

---

## License

Licensed under the [MIT License](LICENSE).
