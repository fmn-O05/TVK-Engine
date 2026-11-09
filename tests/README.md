# `tests/` — Automated Unit & Integration Test Suite

Planned test targets (see [`ROADMAP.md`](../ROADMAP.md)):

- `test_math.cpp` — `Vec3`, `Mat4`, `Quat`, normal matrix, frustum extraction, AABB transforms, and AVX2 vs. scalar equivalence.
- `test_clipper_rasterizer.cpp` — Sutherland-Hodgman near-plane clipping, perspective & orthographic projection, headless tile rasterization, and depth buffer accuracy.
- `test_presenter.cpp` — Braille/HalfBlock/Sextant cell encoding and differential ANSI stream generation.
- `test_ecs.cpp` — Entity lifecycle, generational handles, sparse-set component pools, and hierarchy propagation.
- `test_physics.cpp` — Sphere/AABB/Capsule intersections, raycasting, and impulse resolution.
