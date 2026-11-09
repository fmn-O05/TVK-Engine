# TVK Engine (Terminal Vulkan Engine)

[![Standard](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B17)
[![Architecture](https://img.shields.io/badge/SIMD-AVX2%20%2F%20FMA-orange.svg)](#architectural-layers)
[![Target](https://img.shields.io/badge/Target-60%2B%20FPS%20Terminal-brightgreen.svg)](#building-from-source)
[![License](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)

**TVK Engine** is a high-performance, zero-dependency C++17 3D/2.5D/2D game engine engineered to run real-time interactive games directly inside modern command-line terminals at a steady **60+ FPS**.

Built around a **64×64 L1-cache-bounded tiled software rasterizer**, **AVX2/FMA SIMD vectorization**, **sub-pixel Unicode blitting**, and **parallel differential TrueColor ANSI streaming**, TVK Engine is architected from the ground up as a modular, full-featured terminal game engine.

---

## Documentation & Blueprints

- **[`ROADMAP.md`](ROADMAP.md)** — Master phased refactoring and implementation plan (`Phase 0` through `Phase 7`).
- **[`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md)** — Complete 10-layer engine architecture blueprint, frame lifecycle, and subsystem specifications.
- **[`docs/CODEBASE_AUDIT.md`](docs/CODEBASE_AUDIT.md)** — Forensic audit of the initial experimental codebase, keep-vs-purge decisions, and catalog of 16 bugs (`B-01`–`B-16`).

---

## Repository Structure

```text
TVK-Engine/
├── CMakeLists.txt                     # Cross-platform CMake build (Linux, macOS, Windows)
├── ROADMAP.md                         # Phased implementation roadmap
├── docs/
│   ├── ARCHITECTURE.md                # 10-layer engine architecture blueprint
│   └── CODEBASE_AUDIT.md              # Forensic audit & bug catalog
├── include/
│   └── tvk/
│       ├── TVK.h                      # Umbrella convenience header
│       ├── core/                      # Layer 0: Types, JobSystem, Memory, Time, Events, Stats
│       ├── platform/                  # Layer 1: Terminal PAL, Raw Mode Guard, Keyboard/Mouse Input
│       ├── math/                      # Layer 2: Vec/Mat/Quat, Frustum, AVX2/Scalar SIMD Kernels
│       ├── renderer/                  # Layer 3: Tiled 3D Software Rasterizer & Terminal Presenter
│       ├── scene/                     # Layer 4: Entity-Component-System (ECS) & Scene Graph
│       ├── physics/                   # Layer 5: Colliders, Spatial Hash, Solver & Mouse Raycasting
│       ├── ui/                        # Layer 6: 2D Terminal Canvas & Immediate-Mode TUI
│       ├── assets/                    # Layer 7: .OBJ Mesh Loader, Textures & Asset Handles
│       ├── audio/                     # Layer 8: Retro Synth & Audio Mixer
│       └── app/                       # Layer 9: Engine Loop, Application Host & F1 Debug Overlay
├── src/                               # Subsystem implementations mirroring include/tvk/
├── examples/                          # Demo & showcase executables (sandbox, asteroids)
└── tests/                             # Automated unit & integration test suite
```

---

## Core Rendering Technology

1. **L1 Cache-Bounded 64×64 Tiled Rasterizer**:
   Viewport rendering is partitioned into 64×64 pixel tiles (`16 KB` color + `16 KB` Z-buffer = `32 KB` per worker thread stack), fitting entirely inside L1 data cache with zero inter-thread pixel lock contention.
2. **Front-to-Back Tile Sorting & Early-Z Rejection**:
   Per-tile triangle lists are sorted by maximum reciprocal depth ($\max(1/z)$) prior to scanline evaluation, rejecting occluded fragments before shading.
3. **AVX2 & FMA 8-Wide SIMD Pipeline**:
   SoA vertex/normal transformations and 8-pixel wide barycentric/analytical gradient scanline stepping with masked vector stores.
4. **Differential Sub-Pixel ANSI Streaming**:
   Sub-pixel buffers are blitted into terminal character cells (`U+2800`–`U+28FF` Braille / Half-Block / Sextant), diffed against the frontbuffer in parallel across worker threads, and flushed in a single OS write call.

---

## Building from Source

### Prerequisites
- C++17 compiler (`GCC 9+`, `Clang 10+`, or `MSVC 2019+`)
- `CMake 3.20+`
- Modern UTF-8 / 24-bit TrueColor terminal (`Kitty`, `WezTerm`, `Alacritty`, `Windows Terminal`, `Ghostty`, or `VS Code Terminal`)

### Linux / macOS
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

# Run the demos:
./build/sandbox
./build/asteroids
```

### Windows (MSVC or MinGW-w64 / Ninja)
```bash
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release

./build/sandbox.exe
./build/asteroids.exe
```

---

## License

Licensed under the [MIT License](LICENSE).
