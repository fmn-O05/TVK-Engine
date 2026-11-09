# `tests/` — Automated Unit, SIMD & Headless Golden-Frame Test Suite

- **Technical Specification**: [`docs/specs/07_TESTING_AND_BENCHMARKS.md`](../docs/specs/07_TESTING_AND_BENCHMARKS.md)
- **Defect Catalog Verified**: [`docs/CODEBASE_AUDIT.md` (`B-01` through `B-32`)](../docs/CODEBASE_AUDIT.md)

## Active Baseline Test Suite (`M1` — Wired to CMake `CTest`)
- `TestHarness.h` — Self-contained, zero-dependency, `-fno-exceptions`-compatible unit test framework (`TVK_TEST`, `TVK_EXPECT_TRUE`, `TVK_EXPECT_EQ`, `TVK_EXPECT_NEAR`, `TVK_RUN_ALL_TESTS`).
- `test_core.cpp` — Verifies `Handle<Tag>`, `HandlePool<T, Tag>`, `Result<T, ErrorCode>`, `JobSystem(0)` underflow protection (`B-23`), `ParallelFor`, and `Types.h` / `BlitterType` invariants (`B-31`, `B-32`).
- `test_math.cpp` — Verifies `Vec2`/`Vec3`/`Vec4`/`Mat4`/`Quat`/`AABB`, `Mat4NormalMatrix` under non-uniform scale (`B-07`), Duff et al. branchless `BuildOrthonormalBasis` (`B-05`), `MathSIMD` zero-length normal `NaN` protection (`B-21`) and AVX2-vs-Scalar equivalence (`B-16`, `B-22`), Left-Handed `+Z` `Camera::LookAt` (`B-18`), `Camera::Orbit` cache refresh (`B-19`), and `Camera::GetProjectionMatrix(Orthographic)` (`B-20`).
- `test_renderer.cpp` — Verifies `Primitives` parameter clamping (`B-29`), headless rendering of vertex-colored meshes (`B-04`, `B-26`), full RGB multi-light shading (`B-10`), oriented cylinders & capsules (`B-05`), 3D line rasterization (`B-06`), dynamic near-plane & orthographic projection (`B-08`, `B-09`, `B-25`), `DepthOp::Always` draw order (`B-26`), `FrameStats` population (`B-15`), and all 5 blitters (`Braille` 2-color FG+BG, `HalfBlock`, `Quadrant`, `Sextant`, `ASCII`) with `DECSET 2026` framing (`B-11`, `B-12`, `B-27`, `B-28`, `B-30`).

## Planned Phase 1–7 Test Targets
- `test_input.cpp` — Synthetic byte-stream tests for ANSI CSI keys, Kitty `\x1b[>1u` key-release protocol, and SGR 1006 mouse events.
- `test_presenter_canvas.cpp` — 4×4 Bayer dithering, OKLab palette LUT, and `Canvas2D` transparent text overlay.
- `test_ecs.cpp` — 64-bit `Entity` recycling, sparse-set `ComponentPool<T>`, smallest-pool lead `View<Ts...>`, and topological `TransformSystem`.
- `test_physics.cpp` — Collider manifolds, sequential impulse bounce/stacking, and `ScreenCellToRay` + `PhysicsWorld::Raycast`.
- `test_assets_ui.cpp` — Wavefront `.obj`/`.mtl` parser, `Texture` sampling, and `UIContext` ID stack / widget state machine.
- `test_audio.cpp` — Procedural `SynthPatch` oscillator and `ADSREnvelope` waveform generation.
- `benchmark_runner.cpp` — Multi-resolution headless performance benchmark (`80×24`, `160×50`, `240×80`).
