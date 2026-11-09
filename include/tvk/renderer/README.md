# `include/tvk/renderer` — Layer 3: 3D Software Rasterizer & Terminal Presentation

- **Layer**: Layer 3 (`tvk/renderer`)
- **Responsibility**: Stateless command recording, Left-Handed `+Z` Perspective & Orthographic cameras, procedural geometric primitives, L1-cache-bounded 64×64 tiled triangle and 3D line rasterization, and 5-blitter (`Braille` 2-color FG+BG, `HalfBlock`, `Quadrant`, `Sextant`, `ASCII`) differential ANSI presentation with `DECSET 2026` synchronized output.
- **Dependency Rules**: Depends on Layer 0 (`tvk/core`), Layer 1 (`tvk/platform`), and Layer 2 (`tvk/math`).
- **Technical Specifications**: [`docs/specs/02_MATH_AND_RASTERIZER.md`](../../../docs/specs/02_MATH_AND_RASTERIZER.md) & [`docs/specs/03_PRESENTER_AND_CANVAS2D.md`](../../../docs/specs/03_PRESENTER_AND_CANVAS2D.md)

## Active Baseline Headers (`M1`)
- `Camera.h` — Left-Handed `+Z` forward `LookAt`, `Orbit` (with immediate cache update), Perspective & Orthographic projection matrices, and physical cell aspect ratio calculation.
- `CommandBuffer.h` — Stateless draw command recording (`DrawMesh`, `DrawSphere`, `DrawCylinder`, `DrawCapsule`, `DrawGround`, `DrawLine`), `PipelineState`, `MaterialState`, and `PointLight`.
- `Fog.h` — Depth fog descriptor (`FogDescriptor`).
- `Mesh.h` — `Vertex`, `Mesh`, and local-space `AABB` computation.
- `Primitives.h` — Parameter-clamped procedural mesh generators (`Sphere`, `Cylinder`, `Capsule` with hemisphere caps, `Ground`).
- `Rasterizer.h` — Dynamic near-plane clipping, Perspective/Orthographic projection, 2D guard-band clamping, `int64_t` signed area backface culling, top-left fill bias, AVX2 + scalar scanline rasterization, and depth-tested 3D line rasterization.
- `Renderer.h` — Multi-threaded 4-phase rendering coordinator (`ExecuteVertexPhase`, `ExecuteGeometryPhase`, `ExecuteRasterPhase`, `BuildOutput`).
- `TerminalPresenter.h` — `IPresenter` and `TerminalPresenter` supporting `Braille` (24-bit 2-color luminance clustering), `HalfBlock`, `Quadrant`, `Sextant`, and `ASCII` blitters with exact front-buffer color tracking and `DECSET 2026` framing.
