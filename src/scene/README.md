# `src/scene` — Layer 4 Implementation (`tvk/scene`)

- **Layer**: Layer 4 (`tvk/scene`)
- **Public Headers**: [`include/tvk/scene/`](../../include/tvk/scene/README.md)
- **Technical Specification**: [`docs/specs/04_ECS_AND_SCENE.md`](../../docs/specs/04_ECS_AND_SCENE.md) | [`ROADMAP.md` (Phase 4)](../../ROADMAP.md)

## Planned Phase 4 Sources
- `Registry.cpp` — Entity creation/recycling, deferred `EntityCommandBuffer` execution, and component pool management.
- `Scene.cpp` — Topological parent-before-child `TransformSystem` and $\alpha$-interpolated `RenderSystem`.
