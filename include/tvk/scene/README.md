# `include/tvk/scene` — Layer 4: Entity-Component-System (ECS) & Scene Graph

- **Layer**: Layer 4 (`tvk/scene`)
- **Responsibility**: 64-bit generational entity handles, paged sparse-set component pools, smallest-pool lead `View<Ts...>`, deferred structural `EntityCommandBuffer`, hierarchical transform propagation, and $\alpha$-interpolated render command extraction.
- **Dependency Rules**: Depends on Layer 0 (`tvk/core`), Layer 2 (`tvk/math`), and Layer 3 (`tvk/renderer`).
- **Technical Specification**: [`docs/specs/04_ECS_AND_SCENE.md`](../../../docs/specs/04_ECS_AND_SCENE.md) | [`ROADMAP.md` (Phase 4)](../../../ROADMAP.md)

## Planned Phase 4 Headers
- `Entity.h` — 64-bit generational entity identifier (`32-bit index` + `32-bit generation`) and `kNullEntity`.
- `ComponentPool.h` — Paged sparse-set contiguous storage (`1024`-entry sparse pages + packed dense `Entity` and `T` vectors) with $O(1)$ `Emplace`, swap-and-pop `Remove`, and `TryGet`.
- `Registry.h` — Entity/component registry, smallest-pool lead `View<Components...>::Each`, and `EntityCommandBuffer`.
- `Components.h` — Built-in components (`TagComponent`, `TransformComponent` with `prevWorldMat`/`worldMatrix`, `HierarchyComponent`, `MeshRendererComponent`, `PrimitiveRendererComponent`, `CameraComponent`, `LightComponent`).
- `Scene.h` — Scene container, topological depth-sorted `TransformSystem`, and $\alpha$-interpolated `RenderSystem`.
