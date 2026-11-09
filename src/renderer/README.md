# `src/renderer` — Layer 3 Implementation (`tvk/renderer`)

- **Layer**: Layer 3 (`tvk/renderer`)
- **Public Headers**: [`include/tvk/renderer/`](../../include/tvk/renderer/README.md)
- **Technical Specifications**: [`docs/specs/02_MATH_AND_RASTERIZER.md`](../../docs/specs/02_MATH_AND_RASTERIZER.md) & [`docs/specs/03_PRESENTER_AND_CANVAS2D.md`](../../docs/specs/03_PRESENTER_AND_CANVAS2D.md)

## Active Baseline Sources (`M1`)
- `Camera.cpp` — Left-Handed `+Z` forward `LookAt` (`B-18`), `Orbit` with immediate cache refresh (`B-19`), and Perspective/Orthographic projection matrices (`B-20`).
- `CommandBuffer.cpp` — Stateless draw command and light recording (`DrawMesh`, `DrawSphere`, `DrawCylinder`, `DrawCapsule`, `DrawGround`, `DrawLine`).
- `Primitives.cpp` — Subdivision-clamped procedural mesh generators (`Sphere`, `Cylinder`, `Capsule` with hemisphere caps, `Ground`) (`B-05`, `B-29`).
- `Rasterizer.cpp` — Dynamic near-plane clipping (`B-09`), Perspective & Orthographic projection (`B-08`), 2D guard-band clamping & `int64_t` signed area (`B-25`), top-left fill bias, AVX2 + scalar scanline rasterization (`B-16`, `B-26`), and depth-tested 3D line rasterization (`B-06`).
- `Renderer.cpp` — 4-phase parallel rendering coordinator with vertex color modulation (`B-04`), 3×3 inverse-transpose normal matrix (`B-07`), full RGB multi-light shading (`B-10`), standard `std::atomic` tile binning (`B-24`), `std::vector<uint8_t> m_TileWasDirty` (`B-17`), and headless resize consistency (`B-30`).
- `TerminalPresenter.cpp` — Compile-time byte-to-decimal LUT (`B-11`), 5 blitters (`Braille` 24-bit 2-color FG+BG clustering, `HalfBlock`, `Quadrant`, `Sextant`, `ASCII`) (`B-12`, `B-28`), exact front-buffer color tracking (`B-27`), and `DECSET 2026` synchronized output.
