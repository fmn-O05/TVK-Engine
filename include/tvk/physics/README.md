# `tvk/physics` — 3D/2.5D Collision & Physics Engine

Planned headers for physics simulation and spatial queries (see [`ROADMAP.md`](../../../ROADMAP.md) — Phase 5):

- `Colliders.h` — `SphereCollider`, `AABBCollider`, `OBBCollider`, `CapsuleCollider`.
- `RigidBody.h` — `RigidBodyComponent` (mass, velocity, angular velocity, restitution, drag, kinematic/dynamic/static flags).
- `SpatialHash.h` — Broadphase uniform spatial hash grid and dynamic AABB tree.
- `Narrowphase.h` — GJK/EPA & analytical intersection tests (Sphere-Sphere, Sphere-AABB, Capsule-AABB, AABB-AABB) with contact manifold generation.
- `PhysicsWorld.h` — Sequential impulse velocity/position solver, gravity, and `CharacterController`.
- `Raycast.h` — 3D ray intersection queries and terminal mouse-cell-to-world ray picking.
