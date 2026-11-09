# `include/tvk/assets` — Layer 7: Asset Pipeline & Resource Management

- **Layer**: Layer 7 (`tvk/assets`)
- **Responsibility**: Centralized asset lifetime management (`AssetManager`) built on Layer 0 generational `Handle<Tag>` pools, zero-dependency Wavefront `.obj`/`.mtl` mesh parsing, and 2D RGBA texture sampling & procedural texture synthesis.
- **Dependency Rules**: Depends on Layer 0 (`tvk/core`), Layer 2 (`tvk/math`), and Layer 3 (`tvk/renderer`).
- **Technical Specification**: [`docs/specs/06_ASSETS_UI_AND_AUDIO.md`](../../../docs/specs/06_ASSETS_UI_AND_AUDIO.md) | [`ROADMAP.md` (Phase 6)](../../../ROADMAP.md)

## Planned Phase 6 Headers
- `AssetManager.h` — `MeshHandle`, `TextureHandle`, `MaterialHandle`, `SoundHandle` and `HandlePool`-backed asset registry.
- `ObjLoader.h` — Zero-dependency Wavefront `.obj` / `.mtl` parser supporting `v`, `vt`, `vn`, vertex colors, relative/negative indices, and $N$-gon fan triangulation.
- `Texture.h` — 2D RGBA8 texture buffer with `Nearest`/`Bilinear` UV sampling and procedural generators (`CreateCheckerboard`, `CreateBrick`, `CreateValueNoise`, `CreateGradient`).
- `SceneSerializer.h` — Text/binary scene description serializer.
