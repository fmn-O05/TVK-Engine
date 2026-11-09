# `tvk/scene` — Entity-Component-System (ECS) & Scene Graph

Planned headers for the scene and ECS architecture (see [`ROADMAP.md`](../../../ROADMAP.md) — Phase 4):

- `Entity.h` — Lightweight 64-bit entity identifier (`index` + `generation`) and null sentinel.
- `ComponentPool.h` — Cache-friendly sparse-set contiguous storage for component types.
- `Registry.h` — Entity/component registry with multi-component view iteration (`View<Components...>`).
- `Components.h` — Built-in engine components (`TransformComponent`, `HierarchyComponent`, `MeshRendererComponent`, `CameraComponent`, `LightComponent`, `TagComponent`).
- `Scene.h` — Scene container, hierarchy propagation system, and render-command extraction system.
