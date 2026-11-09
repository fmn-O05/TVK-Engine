# `include/tvk/physics` — Layer 5: 3D/2.5D Collision, Physics Solver & Mouse Picking

- **Layer**: Layer 5 (`tvk/physics`)
- **Responsibility**: 3D/2.5D collision shapes, rigid-body dynamics state, `FrameLinearArena`-backed spatial hash broadphase, analytical narrowphase contact manifold generation, 8-iteration sequential impulse solver with Baumgarte stabilization, character controller, and terminal mouse cell-to-world ray picking.
- **Dependency Rules**: Depends on Layer 0 (`tvk/core`), Layer 2 (`tvk/math`), Layer 3 (`tvk/renderer/Camera.h`), and Layer 4 (`tvk/scene`).
- **Technical Specification**: [`docs/specs/05_PHYSICS_AND_PICKING.md`](../../../docs/specs/05_PHYSICS_AND_PICKING.md) | [`ROADMAP.md` (Phase 5)](../../../ROADMAP.md)

## Planned Phase 5 Headers
- `Colliders.h` — `ColliderComponent` (`Sphere`, `AABB`, `OBB`, `Capsule`) and 16-bit collision layer/mask filtering.
- `RigidBody.h` — `RigidBodyComponent` (`BodyType::{Static, Kinematic, Dynamic}`, mass, restitution, friction, damping, velocities, forces).
- `SpatialHash.h` — Arena-allocated uniform `SpatialHashGrid` broadphase.
- `Narrowphase.h` — Analytical intersection tests (`Sphere-Sphere`, `AABB-AABB`, `Sphere-AABB`, `Capsule-Sphere`, `Capsule-AABB`, `OBB-OBB`) producing `ContactManifold`.
- `PhysicsWorld.h` — Fixed-timestep sequential impulse velocity/position solver and kinematic `CharacterController`.
- `Raycast.h` — `ScreenCellToRay` unprojection and `PhysicsWorld::Raycast` queries.
