# `include/tvk/math` — Layer 2: Linear Algebra, Volumes & SIMD Kernels

- **Layer**: Layer 2 (`tvk/math`)
- **Responsibility**: Unified vector, matrix, quaternion, bounding volume, frustum, ray, orthonormal basis, and guarded AVX2/FMA + scalar fallback SIMD transformation kernels following TVK Engine's Left-Handed `+Z` forward coordinate convention.
- **Dependency Rules**: Depends only on Layer 0 (`tvk/core`).
- **Technical Specification**: [`docs/specs/02_MATH_AND_RASTERIZER.md`](../../../docs/specs/02_MATH_AND_RASTERIZER.md) | [`docs/ARCHITECTURE.md`](../../../docs/ARCHITECTURE.md)

## Active Baseline Headers (`M1`)
- `Math.h` — `Vec2` (`.x/.y` and `.u/.v`), `Vec3`, `Vec4`, `Quat`, `Mat3`, `Mat4`, `AABB`, `Plane`, `Frustum`, `Ray`, `Mat4NormalMatrix(const Mat4&)` (analytical 3×3 cofactor inverse-transpose), and `BuildOrthonormalBasis(Vec3 axis, Vec3& outRight, Vec3& outForward)` (Duff et al. branchless ONB).
- `MathSIMD.h` — Guarded `#if defined(TVK_SIMD_AVX2) && defined(__AVX2__)` 8-wide FMA kernels (`NormalizeFast`, `TransformVerticesAVX2`, `TransformNormalsAVX2`) with zero-length normal masking (`dot > 1e-12f`) and bit/epsilon-equivalent scalar fallbacks.
