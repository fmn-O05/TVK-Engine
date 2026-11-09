# `src/math` — Layer 2 Implementation (`tvk/math`)

- **Layer**: Layer 2 (`tvk/math`)
- **Public Headers**: [`include/tvk/math/`](../../include/tvk/math/README.md)
- **Technical Specification**: [`docs/specs/02_MATH_AND_RASTERIZER.md`](../../docs/specs/02_MATH_AND_RASTERIZER.md)

## Active Baseline Sources (`M1`)
- `Math.cpp` — Matrix multiplication (`Mat4Multiply`), translation/scaling/rotation builders, `QuatToMat4`, `Mat4NormalMatrix` (`B-07`), and Duff et al. branchless `BuildOrthonormalBasis` (`B-05`).
