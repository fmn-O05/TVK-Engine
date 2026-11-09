# TVK Engine (Terminal Vulkan)

[![Standard](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B17)
[![Architecture](https://img.shields.io/badge/SIMD-AVX2%20%2F%20FMA-orange.svg)](#key-architectural-features)
[![Performance](https://img.shields.io/badge/FPS-60%20Steady-brightgreen.svg)](#performance-benchmarks)
[![License](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)

**TVK Engine** is an ultra-high-performance, CPU-only 3D software rendering engine that presents directly into modern command-line terminals at a rock-solid **60 FPS**.

Architected around modern low-level graphics API concepts (inspired by **Vulkan** and **tile-based mobile GPUs**), TVK bypasses traditional software-rendering bottlenecks through **L1 cache-bounded tiled rasterization**, full **AVX2 / FMA vectorization**, **sub-pixel Braille blitting**, and **differential TrueColor ANSI streaming**.

---

## Visual Presentation

- **Sub-Pixel Resolution via Unicode Braille**: Each terminal cell is subdivided into a **2×4 sub-pixel matrix** (8 dots per character cell, Unicode range `0x2800`–`0x28FF`), effectively quadrupling vertical and doubling horizontal rendering resolution.
- **24-Bit TrueColor RGB**: Smooth gradients and ambient/diffuse/specular shading output directly via standard ANSI 24-bit color escape sequences (`\x1b[38;2;R;G;Bm`).
- **Differential Screen Streaming**: By comparing the current backbuffer against the frontbuffer, only modified terminal cells are emitted with direct cursor addressing (`\x1b[row;colH`), eliminating terminal flickering and slashing output bandwidth from ~50 MB/s down to < 200 KB/s.

---

## Key Architectural Features

### 1. L1 Cache-Bounded Tiled Immediate-Mode Rasterizer
Traditional software renderers suffer massive cache thrashing by writing to screen-sized framebuffers in DRAM. TVK partitions the viewport into **64×64 pixel tiles**:
- Each tile's local framebuffer (16 KB) and Z-buffer (16 KB) total **32 KB**, fitting entirely within the CPU's **L1 Data Cache**.
- Tiles are processed independently in parallel across worker threads with zero memory bus contention.

### 2. Front-to-Back Sorting & Early-Z Rejection
Within each tile, intersecting triangles are extracted into thread-local arrays and sorted front-to-back using maximum reciprocal depth ($\max(1/z)$). This triggers early-Z depth rejection immediately, eliminating pixel-shading costs on occluded surfaces.

### 3. Screen-Space Analytical Gradients (Barycentric Collapse)
Instead of executing expensive floating-point cross products and divisions per pixel:
- Linear screen-space gradients ($\frac{\partial z}{\partial x}$, $\frac{\partial z}{\partial y}$, $\frac{\partial (N \cdot L)}{\partial x}$, $\frac{\partial \text{spec}}{\partial x}$) are computed **once per triangle**.
- Pixel interpolation across scanlines reduces to simple scalar additions and vectorized SIMD steps.

### 4. AVX2 & FMA SIMD Inner Loop
- **8-Wide Edge Tests**: Triangle edge evaluation tests 8 pixels simultaneously using `_mm256_cmpgt_epi32` and `_mm256_and_si256`.
- **Scanline Bounding**: $O(\text{Pixels})$ analytical edge-intersection algorithm calculates the exact start and end pixel coordinates per scanline, skipping non-overlapping bounding-box regions.
- **Vectorized Depth Testing**: 8-way parallel depth comparison (`_mm256_cmp_ps`) followed by masked vector stores (`_mm256_maskstore_ps` and `_mm256_maskstore_epi32`), avoiding CPU branch mispredictions.
- **Fast Reciprocal Recalculation**: Newton-Raphson refined reciprocal approximations (`_mm256_rcp_ps` / `_mm256_rsqrt_ps`).

### 5. Lighting, Materials & Fog
- **Multi-Light Blinn-Phong**: Up to 4 view-space point/directional lights with ambient, diffuse, and specular half-vector ($N \cdot H$) highlights.
- **Linear Depth Fog**: Vectorized view-space distance attenuation blended via FMA instructions:
  $$\text{Color} = \text{lerp}(\text{RenderedRGB}, \text{FogColor}, \text{clamp}(\frac{Z - \text{start}}{\text{end} - \text{start}}, 0, 1))$$

### 6. Geometry Pipeline & Frustum Culling
- **Gribb-Hartmann Plane Extraction**: 6 view-frustum planes extracted directly from the combined View-Projection matrix.
- **Conservative AABB Testing**: Draw calls whose transformed object-space bounding boxes lie outside the frustum are culled prior to vertex submission.
- **Near-Plane Clipping**: Sutherland-Hodgman geometric polygon clipping against the camera near plane (`NEAR_PLANE = 4.0f`).

### 7. Zero-Allocation Lock-Free Job System
- Task queues backed by lock-free atomics (`std::memory_order_relaxed` / `std::memory_order_acq_rel`).
- Thread synchronization via **Hybrid Barriers**: tight spin-wait loops with `_mm_pause()` for intra-frame latency, with graceful fallback to `std::this_thread::yield()`.

---

## Included Demos

| Target | Description |
| :--- | :--- |
| `sandbox` | **Mushroom Kingdom**: A 2.5D Nintendo-style platformer showcase with sunlight, floating clouds, green pipes, spinning specular coins, bouncy question blocks, and a walking character. |
| `asteroids` | **Asteroid Field**: High-speed procedural asteroid field featuring rotating quaternions, tumbling 3D meshes, spheres, and capsules under dynamic orbiting light. |

---

## Building from Source

### Prerequisites
- Modern C++17 compiler (GCC 9+, Clang 10+, or MSVC 2019+)
- CPU with **AVX2** and **FMA** instruction support (Intel Haswell / AMD Zen 1 or newer)
- CMake 3.20+
- Ninja (recommended) or Make

### Windows (MinGW-w64 / Ninja)
```bash
git clone https://github.com/fmn-O05/TVK-Engine.git
cd TVK-Engine

cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

### Running the Demos
```bash
# Run the Mushroom Kingdom demo:
./build/sandbox.exe

# Run the Asteroid Field demo:
./build/asteroids.exe
```

> **Terminal Tip**: For the best visual experience, run inside **Windows Terminal**, **Alacritty**, **WezTerm**, **Kitty**, or the **VS Code Terminal** with font ligatures and UTF-8 enabled.

---

## Performance Benchmarks

Profiled on a standard x86-64 CPU (8 hardware threads, 1080p terminal resolution):

| Pipeline Stage | Wall-Clock Time | Percentage |
| :--- | :--- | :--- |
| **Vertex Phase** (SIMD AVX2) | ~0.18 ms | 1.1% |
| **Geometry & Binning** | ~0.21 ms | 1.3% |
| **Tiled Rasterization** (L1 Cache) | ~0.35 ms | 2.1% |
| **ANSI Present / Diff Output** | ~0.10 ms | 0.6% |
| **Frame Cap Sleep (~60 FPS)** | ~15.82 ms | 94.9% |
| **Total Frame Time** | **16.66 ms** | **100% (60.0 FPS)** |

---

## Architecture Overview

```
[tvk::CommandBuffer]
        │
        ├── Record Draw Calls (DrawMesh, DrawSphere, DrawCylinder, DrawGround, BindPipeline)
        ▼
[tvk::Renderer::Execute]
        │
        ├── Frustum Cull (AABB vs Gribb-Hartmann Frustum Planes)
        ├── Phase 1: Parallel Vertex Transform (AVX2 SoA batching + Multi-Light)
        ├── Phase 2: Parallel Geometry Clip (Sutherland-Hodgman) + Lock-Free Tile Binning
        ├── Phase 3: Parallel Tile Rasterization (L1-Bounded 64x64 + Early-Z + AVX2)
        └── Phase 4: Parallel ANSI Braille String Assembly (Differential Output)
        │
        ▼
[tvk::pal::FastPrint] ──> Terminal Output (60 FPS, No Flickering)
```

---

## License

This project is licensed under the [MIT License](LICENSE).

