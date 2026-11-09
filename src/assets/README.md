# `src/assets` — Layer 7 Implementation (`tvk/assets`)

- **Layer**: Layer 7 (`tvk/assets`)
- **Public Headers**: [`include/tvk/assets/`](../../include/tvk/assets/README.md)
- **Technical Specification**: [`docs/specs/06_ASSETS_UI_AND_AUDIO.md`](../../docs/specs/06_ASSETS_UI_AND_AUDIO.md) | [`ROADMAP.md` (Phase 6)](../../ROADMAP.md)

## Planned Phase 6 Sources
- `AssetManager.cpp` — Generational `HandlePool` resource registration, lookup, and lifetime management.
- `ObjLoader.cpp` — Zero-dependency Wavefront `.obj` / `.mtl` parser and $N$-gon fan triangulator.
- `Texture.cpp` — Nearest/bilinear UV sampling and procedural texture generators (`Checkerboard`, `Brick`, `ValueNoise`, `Gradient`).
