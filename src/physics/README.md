# `src/physics` — Layer 5 Implementation (`tvk/physics`)

- **Layer**: Layer 5 (`tvk/physics`)
- **Public Headers**: [`include/tvk/physics/`](../../include/tvk/physics/README.md)
- **Technical Specification**: [`docs/specs/05_PHYSICS_AND_PICKING.md`](../../docs/specs/05_PHYSICS_AND_PICKING.md) | [`ROADMAP.md` (Phase 5)](../../ROADMAP.md)

## Planned Phase 5 Sources
- `SpatialHash.cpp` — `FrameLinearArena`-allocated broadphase spatial hash grid.
- `Narrowphase.cpp` — Analytical intersection manifold generators (`Sphere`, `AABB`, `Capsule`, `OBB`).
- `PhysicsWorld.cpp` — 8-iteration sequential impulse solver with Baumgarte stabilization and `CharacterController`.
- `Raycast.cpp` — `ScreenCellToRay` unprojection and analytical raycast queries.
