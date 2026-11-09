# `examples/` — TVK Engine Demo & Showcase Executables

- **Architecture Overview**: [`docs/ARCHITECTURE.md`](../docs/ARCHITECTURE.md)
- **Roadmap**: [`ROADMAP.md`](../ROADMAP.md)

## Active Baseline Executables (`M1`)

1. **`Sandbox.cpp` (`./build/sandbox`)**:
   - Interactive 3D voxel/platformer scene showcasing per-vertex colored meshes (`B-04`), non-uniformly scaled geometry with accurate normal matrices (`B-07`), oriented cylinders and capsules (`B-05`), dynamic point lights (`B-10`), and 24-bit TrueColor Braille rendering (`B-12`).
   - **Controls & CLI Flags**:
     - Press `q` or `ESC` (or `Ctrl+C`) to exit cleanly and restore the terminal (`B-13`, `B-31`).
     - Pass `--frames <N>` (e.g., `./build/sandbox --frames 5`) for bounded automated smoke testing.
     - Pass `--csv` to opt-in to writing `perf_log.csv` frame telemetry.

2. **`Asteroids_Demo.cpp` (`./build/asteroids`)**:
   - Real-time 3D space shooter demo featuring a player starfighter, 3D laser bolts, rotating multi-colored asteroids, background starfield (`DepthOp::Always`, `B-26`), and multi-light specular shading.
   - **Controls & CLI Flags**:
     - `W`/`A`/`S`/`D`: Move starfighter; `Space`: Fire lasers; `q` or `ESC`: Exit cleanly and restore the terminal.
     - Pass `--frames <N>` (e.g., `./build/asteroids --frames 5`) for bounded automated smoke testing.
