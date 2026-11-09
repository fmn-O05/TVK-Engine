# `tvk/assets` — Asset Pipeline & Resource Management

Planned headers for resource loading and handle management (see [`ROADMAP.md`](../../../ROADMAP.md) — Phase 6):

- `Handle.h` — Type-safe generational resource handles (`MeshHandle`, `TextureHandle`, `MaterialHandle`, `SoundHandle`).
- `AssetManager.h` — Central resource cache, reference counting, and hot-reload support.
- `ObjLoader.h` — Zero-dependency Wavefront `.obj` / `.mtl` mesh and material parser.
- `Texture.h` — 2D RGBA texture buffer with nearest/bilinear UV sampling and procedural texture generators (checkerboard, noise, gradients, brick).
- `SceneSerializer.h` — Scene description loader/saver.
