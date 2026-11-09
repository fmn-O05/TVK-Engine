# Specification 02 — Unified Math, SIMD Kernels, Camera & Tiled 3D Software Rasterizer

- **Layers Covered**: Layer 2 (`tvk/math`), Layer 3 (`tvk/renderer` 3D pipeline)
- **Target Roadmap Phases**: **Baseline Foundation (M1 — Active)** & **Phase 2 (Extended Pipeline)**
- **Resolved Baseline Defects**: `B-02`, `B-04`, `B-05`, `B-06`, `B-07`, `B-08`, `B-09`, `B-10`, `B-14`, `B-15`, `B-16`, `B-17`, `B-18`, `B-19`, `B-20`, `B-21`, `B-24`, `B-25`, `B-26`, `B-28`, `B-29`, `B-30`
- **Industry Benchmark References**: Intel Larrabee & Quake software rasterizers (tile binning, 2D guard-band clipping, `int64_t` signed area, top-left fill convention), `bgfx` (64-bit `SortKey`), Duff et al. JCGT 2017 (branchless orthonormal basis).

---

## 1. Layer 2: Unified Math & Volume Library (`include/tvk/math/Math.h`)

### 1.1 Consolidated Vector, Matrix & Volume Types (`B-32` Fix)

All mathematical and geometric primitives reside in `include/tvk/math/Math.h` (with `Vec2`/`Vec4` re-exported in `tvk/core/Types.h` and `AABB` re-exported in `tvk/renderer/Mesh.h` for backward compatibility):

| Type | Byte Size (`sizeof`) | Alignment (`alignof`) | Layout & Accessors |
| :--- | :--- | :--- | :--- |
| `Vec2` | `8 B` | `4 B` | `union` supporting both `.x, .y` and `.u, .v` + `+`, `-`, `*` operators |
| `Vec3` | `12 B` | `4 B` | `float x, y, z;` + `+`, `-`, `*`, `/`, `Dot`, `Cross`, `Normalize`, `Length` |
| `Vec4` | `16 B` | `4 B` | `float x, y, z, w;` |
| `Quat` | `16 B` | `4 B` | `float x, y, z, w;` (`QuatFromAxisAngle`, `QuatNormalize`, `QuatSlerp`, `QuatToMat4`) |
| `Mat3` | `36 B` | `4 B` | `float m[3][3];` (`Mat3Transform(const Mat3&, Vec3)`) |
| `Mat4` | `64 B` | `4 B` | Row-major `float m[4][4];` (translation in `m[0..2][3]`) |
| `AABB` | `24 B` | `4 B` | `Vec3 min, max;` (`Center()`, `Extents()`, `Expand(Vec3)`, `Intersects(AABB)`) |
| `Plane` | `16 B` | `4 B` | `Vec3 normal; float d;` |
| `Frustum` | `96 B` | `4 B` | `Plane planes[6];` (Left, Right, Bottom, Top, Near, Far) |
| `Ray` | `24 B` | `4 B` | `Vec3 origin, direction;` (`At(float t)`) |

---

### 1.2 Exact Mathematical Derivations (Left-Handed `+Z` Forward View/Clip Pipeline)

#### 1.2.1 `Camera::LookAt(eye, target, up)` (`B-18` Fix)
TVK Engine's view space uses **`+X` Right, `+Y` Up, `+Z` Forward (into the screen)**. Given camera position $\vec{e}$, target $\vec{t}$, and world up $\vec{u}_0$:

$$\vec{f} = \operatorname{normalize}(\vec{t} - \vec{e}), \quad \vec{r} = \operatorname{normalize}(\vec{u}_0 \times \vec{f}), \quad \vec{u} = \vec{f} \times \vec{r}$$

$$V_{\text{LookAt}} = \begin{bmatrix} r_x & r_y & r_z & -\vec{r} \cdot \vec{e} \\ u_x & u_y & u_z & -\vec{u} \cdot \vec{e} \\ +f_x & +f_y & +f_z & -\vec{f} \cdot \vec{e} \\ 0 & 0 & 0 & 1 \end{bmatrix}$$

**Proof of Invariant**: Transforming $\vec{p} = \vec{t}$ yields $z_{\text{view}} = \vec{f} \cdot \vec{t} - \vec{f} \cdot \vec{e} = \vec{f} \cdot (\vec{t} - \vec{e}) = +\|\vec{t} - \vec{e}\| > 0$, matching `ClipAgainstNearPlane` ($z_{\text{view}} \ge z_{\text{near}} > 0$) and `Camera::GetViewMatrix()` orbit mode.

#### 1.2.2 Perspective & Orthographic Projection Matrices (`B-08`, `B-20` Fixes)
With near plane $n > 0$, far plane $f > n$ ($\Delta = \max(f - n, 10^{-6})$), vertical FOV $\theta$, and physical cell aspect ratio $a = \frac{\text{term\_w}}{2.0 \times \text{term\_h}}$:

- **Perspective Projection ($w_{\text{clip}} = +z_{\text{view}}$)**:
  $$t = \tan\left(\frac{\theta}{2}\right), \quad P_{\text{persp}} = \begin{bmatrix} \frac{1}{a \cdot t} & 0 & 0 & 0 \\ 0 & \frac{1}{t} & 0 & 0 \\ 0 & 0 & \frac{f + n}{f - n} & \frac{-2 f n}{f - n} \\ 0 & 0 & +1 & 0 \end{bmatrix}$$
  At $z_{\text{view}} = n$, $z_{\text{ndc}} = \frac{(f+n)n - 2fn}{(f-n)n} = -1$. At $z_{\text{view}} = f$, $z_{\text{ndc}} = +1$.

- **Orthographic Projection ($w_{\text{clip}} = 1.0$)**:
  Given half-height $h = \max(\text{orthoSize}, 10^{-6})$ and half-width $w = h \cdot a$:
  $$P_{\text{ortho}} = \begin{bmatrix} \frac{1}{w} & 0 & 0 & 0 \\ 0 & \frac{1}{h} & 0 & 0 \\ 0 & 0 & \frac{+2}{f - n} & -\frac{f + n}{f - n} \\ 0 & 0 & 0 & 1 \end{bmatrix}$$
  At $z_{\text{view}} = n$, $z_{\text{ndc}} = \frac{2n - f - n}{f - n} = -1$. At $z_{\text{view}} = f$, $z_{\text{ndc}} = +1$. Both Perspective and Orthographic matrices map $[n, f] \to [-1, +1]$ monotonically, ensuring Gribb-Hartmann frustum extraction (`ExtractFrustumPlanes`) works identically for both projection types.

#### 1.2.3 Normal Matrix: Analytical $3\times 3$ Cofactor Inverse-Transpose (`Mat4NormalMatrix`, `B-07` Fix)
When a mesh undergoes non-uniform scaling $S = \operatorname{diag}(s_x, s_y, s_z)$, multiplying normals by $M_{3\times 3}$ shears them away from perpendicularity. `Mat4NormalMatrix(const Mat4& m)` computes the cofactor matrix $C$ of the upper-left $3\times 3$ submatrix $A = M_{3\times 3}$:

$$C_{00} = A_{11}A_{22} - A_{12}A_{21}, \quad C_{01} = -(A_{10}A_{22} - A_{12}A_{20}), \quad C_{02} = A_{10}A_{21} - A_{11}A_{20}$$
$$\det(A) = A_{00}C_{00} + A_{01}C_{01} + A_{02}C_{02}$$

Because $(A^{-1})^T = \frac{1}{\det(A)} C$, and normals are normalized immediately after transformation, if $|\det(A)| > 10^{-8}$, `Mat4NormalMatrix` returns $\frac{1}{\det(A)} C$ (or fallback $A$ if singular).

#### 1.2.4 Duff et al. Branchless Orthonormal Basis (`BuildOrthonormalBasis`, `B-05` Fix)
To orient `DrawCylinder(a, b, r)` and `DrawCapsule(a, b, r)` along an arbitrary unit axis $\hat{n} = \operatorname{normalize}(\vec{b} - \vec{a})$ without a NaN singularity at $n_z = -1$:

$$s = \operatorname{copysign}(1.0f, n_z), \quad a = -\frac{1.0f}{s + n_z}, \quad b = n_x n_y a$$
$$\vec{t}_1 = \begin{bmatrix} 1.0f + s n_x^2 a \\ s b \\ -s n_x \end{bmatrix}, \quad \vec{t}_2 = \begin{bmatrix} b \\ s + n_y^2 a \\ -n_y \end{bmatrix}$$

Constructing the $4\times 4$ model matrix with columns $(\vec{t}_1 \cdot r,\; \hat{n} \cdot L,\; \vec{t}_2 \cdot r,\; \text{midpoint})$ aligns any Y-up unit cylinder/capsule mesh along $\vec{a} \to \vec{b}$. For `DrawCapsule`, two sphere end-caps of radius $r$ are additionally emitted at $\vec{a}$ and $\vec{b}$.

---

## 2. Guarded AVX2/FMA SIMD & Zero-Length Normal Safety (`include/tvk/math/MathSIMD.h`)

Fixes `B-16`, `B-21`, and `B-22`:
1. **Compile-Time Guard**: AVX2 intrinsics (`<immintrin.h>`) are compiled if and only if `#if defined(TVK_SIMD_AVX2) && defined(__AVX2__)`. Otherwise, bit/epsilon-equivalent scalar loops execute.
2. **Zero-Length Normal `NaN` Protection (`B-21`)**:
   In `NormalizeFast` (SSE) and `TransformNormalsAVX2` (AVX2), when a normal vector is `{0, 0, 0}`, `dot = 0.0f` and `_mm256_rsqrt_ps(0.0f)` yields `+Inf`. Multiplying `dot * rlen * rlen` in the Newton-Raphson step produces `0.0f * Inf = NaN`.
   We mask `rlen` before Newton-Raphson refinement:
   ```cpp
   __m256 validMask = _mm256_cmp_ps(dot, _mm256_set1_ps(1e-12f), _CMP_GT_OQ);
   __m256 rlen = _mm256_and_ps(_mm256_rsqrt_ps(dot), validMask);
   // Newton-Raphson: rlen = rlen * (1.5 - 0.5 * dot * rlen * rlen)
   // For dot == 0, rlen == 0, so output normal is cleanly {0.0f, 0.0f, 0.0f} with zero NaNs!
   ```

---

## 3. Layer 3: Command Sorting, Clipping, Lighting & Tiled Rasterizer (`tvk/renderer`)

### 3.1 64-Bit `SortKey` Layout (`include/tvk/renderer/CommandBuffer.h`)

```text
 63    60 59     56 55       52 51                  32 31               16 15                0
┌────────┬─────────┬───────────┬──────────────────────┬───────────────────┬───────────────────┐
│Pass(4b)│Layer(4b)│DepthOp(4b)│ Material / Mesh (20b)│Quantized Depth 16b│ Submission Seq 16b│
└────────┴─────────┴───────────┴──────────────────────┴───────────────────┴───────────────────┘
```
- **Opaque Pass (`DepthOp::Less`)**: Sorted front-to-back (ascending depth) to maximize Early-Z rejection in Phase 3.
- **Overlay/UI Pass (`DepthOp::Always`, `B-26` Fix)**: Preserves submission sequence (`seq_a < seq_b`) so later draw calls composite on top of earlier ones.

---

### 3.2 Per-Vertex Color Modulation & Full RGB Multi-Light Shading (`B-04`, `B-10`, `B-26` Fixes)

In `Renderer::TransformMeshVertices`:
1. **Vertex × Command Albedo Modulation (`B-04`)**:
   $$C_{\text{base}, c} = \frac{\text{Vertex::color}_c \times \text{RenderCommand::color}_c}{255}, \quad c \in \{R, G, B\}$$
   Meshes with white vertices (`Sphere`, `Cylinder`, `Ground`) inherit `cmd.color`, while vertex-colored meshes (`Sandbox` bricks/blocks, `Asteroids` ship) preserve their per-vertex colors when `cmd.color == {255, 255, 255, 255}`.
2. **Full RGB Multi-Light Diffuse & Blinn-Phong Specular Accumulation (`B-10`)**:
   For each vertex at view-space position $\vec{p}_v$ with unit view-space normal $\hat{n}$ and view vector $\hat{v} = \operatorname{normalize}(-\vec{p}_v)$:
   $$\vec{L}_{\text{diff}} = \vec{I}_{\text{ambient}}, \quad \vec{L}_{\text{spec}} = \vec{0}$$
   For each active light $i$ at view-space position $\vec{l}_i$ with normalized RGB color $\vec{c}_i = (R_i, G_i, B_i)/255$, intensity $I_i$, and distance attenuation $A_i = \operatorname{clamp}(1 - d_i / R_{\text{range}, i}, 0, 1)$:
   $$\hat{\omega}_i = \operatorname{normalize}(\vec{l}_i - \vec{p}_v), \quad \hat{h}_i = \operatorname{normalize}(\hat{\omega}_i + \hat{v})$$
   $$\vec{L}_{\text{diff}} \mathrel{+}= \vec{c}_i \cdot I_i \cdot A_i \cdot \max(0, \hat{n} \cdot \hat{\omega}_i)$$
   $$\vec{L}_{\text{spec}} \mathrel{+}= \vec{c}_i \cdot I_i \cdot A_i \cdot k_s \cdot \max(0, \hat{n} \cdot \hat{h}_i)^p$$
   The shaded vertex RGB color stored in `ShadeVertex::color` is:
   $$C_{\text{shaded}, c} = \operatorname{clamp}\left(\text{round}\left(C_{\text{base}, c} \cdot L_{\text{diff}, c} + 255.0f \cdot L_{\text{spec}, c}\right), 0, 255\right)$$

---

### 3.3 Dynamic Near-Plane Clipping, Guard-Band Clamping, `int64_t` Area & Top-Left Fill Rule (`B-08`, `B-09`, `B-25` Fixes)

1. **Dynamic Near-Plane Sutherland-Hodgman Clipping (`B-09`)**:
   - Clips view-space triangles against $z_{\text{view}} \ge z_{\text{near}}$ where $z_{\text{near}} = \max(\text{camera.GetNear()}, 0.05f)$.
   - Linearly interpolates position, normal, UV, and shaded RGB color across clipped edges.
2. **Unified Perspective & Orthographic Projection (`B-08`)**:
   - **Perspective (`proj.m[3][2] != 0.0f`)**:
     $$w_{\text{inv}} = \frac{1.0f}{z_{\text{view}}}, \quad x_{\text{ndc}} = (P_{00} x + P_{02} z) w_{\text{inv}}, \quad y_{\text{ndc}} = (P_{11} y + P_{12} z) w_{\text{inv}}, \quad z_{\text{metric}} = w_{\text{inv}}$$
   - **Orthographic (`proj.m[3][3] == 1.0f`)**:
     $$x_{\text{ndc}} = P_{00} x + P_{03}, \quad y_{\text{ndc}} = P_{11} y + P_{13}, \quad z_{\text{ndc}} = P_{22} z + P_{23}$$
     $$z_{\text{metric}} = \operatorname{clamp}\left(\frac{1.0f - z_{\text{ndc}}}{2.0f} + 10^{-4}f,\; 10^{-4}f,\; 1.0f\right) > 0$$
     Both modes produce a strictly positive $z_{\text{metric}} > 0$ where larger values are closer to the camera!
3. **2D Guard-Band Clamping & 64-Bit Signed Area (`B-25`)**:
   - Screen coordinates $(p_x, p_y)$ are clamped to $[-16384, +16384]$ before integer conversion so edge deltas satisfy $|\Delta x|, |\Delta y| \le 32768$.
   - Signed twice-area is evaluated in 64-bit signed integer arithmetic:
     ```cpp
     int64_t area64 = static_cast<int64_t>(p1x - p0x) * static_cast<int64_t>(p2y - p0y) -
                      static_cast<int64_t>(p2x - p0x) * static_cast<int64_t>(p1y - p0y);
     if (area64 <= 0) return; // Backface or degenerate triangle culled
     ```
4. **D3D / Larrabee Top-Left Fill Rule Bias (`B-25`)**:
   - For edge $(x_a, y_a) \to (x_b, y_b)$ with step deltas $\Delta x_{\text{step}} = -(y_b - y_a)$ and $\Delta y_{\text{step}} = (x_b - x_a)$, an edge is a **top edge** if $\Delta x_{\text{step}} == 0 \land \Delta y_{\text{step}} < 0$ (or according to screen-Y down convention: $\Delta x_{\text{step}} > 0 \lor (\Delta x_{\text{step}} == 0 \land \Delta y_{\text{step}} < 0)$).
   - Adding `bias = isTopOrLeft ? 0 : -1` to the initial row edge value transforms $E > 0$ into $E \ge 0$, allowing a single `_mm256_cmpgt_epi32(e, _mm256_set1_epi32(-1))` instruction to enforce water-tight shared edge rasterization.

---

### 3.4 3D Line & Wireframe Rasterizer (`CommandType::DrawLine`, `B-06` Fix)

1. **View-Space Near-Plane Clipping**:
   Given endpoints $\vec{v}_0, \vec{v}_1$ in view space:
   - If $z_0 < z_{\text{near}} \land z_1 < z_{\text{near}}$, reject the segment.
   - If one endpoint has $z < z_{\text{near}}$, clip to $t = \frac{z_{\text{near}} - z_0}{z_1 - z_0}$: $\vec{v}_{\text{clip}} = \vec{v}_0 + t(\vec{v}_1 - \vec{v}_0)$.
2. **Projection & Tile Binning**:
   Project both endpoints to screen pixel coordinates $(x_0, y_0, z_{\text{metric}, 0})$ and $(x_1, y_1, z_{\text{metric}, 1})$, compute the bounding box of tiles overlapped by the segment, and link into per-tile line lists (or rasterize during Phase 3).
3. **Depth-Tested 2D DDA / Bresenham Stepping**:
   For $N = \max(|x_1 - x_0|, |y_1 - y_0|)$ steps inside the tile:
   - Step $(x, y)$ and depth $z_{\text{metric}}$ linearly across $[0, N]$.
   - If $(x, y)$ lies inside the current tile and $z_{\text{metric}} > \text{local\_zb}[\text{idx}]$ (or `DepthOp::Always`), write `line.color` into `local_fb[idx]` and update `local_zb[idx]`.

---

## 4. Verification Criteria (`test_math` & `test_renderer`)

1. **`Camera::LookAt` & `Camera::Orbit` (`tests/test_math.cpp`)**:
   - `LookAt({0, 0, -10}, {0, 0, 0}, {0, 1, 0})` transforms `{0, 0, 0}` to `z_view == +10.0f > 0`.
   - `Orbit(0.5f, 0.25f)` immediately updates `GetViewMatrix()` without requiring a manual `UpdateCache()` call.
2. **Perspective & Orthographic Frustum & Projection (`tests/test_math.cpp`, `tests/test_renderer.cpp`)**:
   - Both Perspective and Orthographic cameras keep `{0, 0, (near + far) * 0.5f}` inside all 6 Gribb-Hartmann frustum planes and rasterize non-zero pixels into the headless backbuffer.
3. **`Mat4NormalMatrix` & `BuildOrthonormalBasis` (`tests/test_math.cpp`)**:
   - Non-uniformly scaled matrix `Mat4Scale({1.0f, 4.0f, 1.0f})` transforms a $45^\circ$ normal $(0.7071, 0.7071, 0)$ via `Mat4NormalMatrix` to match the true surface normal $(0.9701, 0.2425, 0)$.
   - `BuildOrthonormalBasis` produces mutually orthogonal unit vectors ($|\hat{a} \cdot \hat{b}| < 10^{-5}$, $\|\hat{v}\| \approx 1$) across all axes including $(0, 0, -1)$ and $(0, \pm 1, 0)$.
4. **AVX2 vs. Scalar Equivalence & Zero-Length Normal Safety (`tests/test_math.cpp`)**:
   - `NormalizeFast({0, 0, 0})` and `TransformNormalsAVX2` on zero-length normals produce `{0, 0, 0}` with `std::isnan() == false`.
   - Both `-DTVK_ENABLE_SIMD=ON` and `-DTVK_ENABLE_SIMD=OFF` pass 100% of unit and headless rendering assertions.
