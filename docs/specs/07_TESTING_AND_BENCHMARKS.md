# Specification 07 — Automated Testing Harness, Headless Golden-Frame Verification & Performance Benchmarks

- **Directory Covered**: `tests/` (`TestHarness.h`, `test_core.cpp`, `test_math.cpp`, `test_renderer.cpp`, and Phase 1–7 test suites)
- **Target Roadmap Phases**: **Baseline Foundation (M1 — Active in CTest)** & **Phases 1–7 Continuous Verification**
- **Resolved Baseline Defects**: Verifies all 32 baseline defect fixes (`B-01` through `B-32`).

---

## 1. Zero-Dependency Test Harness (`tests/TestHarness.h` — Active in Baseline)

TVK Engine uses a self-contained, zero-dependency, `-fno-exceptions`-compatible C++17/C++20 test harness (`tests/TestHarness.h`) integrated directly with CMake `CTest` (`enable_testing()`, `add_test()`).

### 1.1 Supported Assertion Macros
- `TVK_TEST(SuiteName, TestName)`: Registers a test function automatically before `main()`.
- `TVK_EXPECT_TRUE(expr)` / `TVK_EXPECT_FALSE(expr)`: Evaluates boolean condition; records file, line, and expression on failure.
- `TVK_EXPECT_EQ(a, b)` / `TVK_EXPECT_NE(a, b)`: Value equality/inequality comparison.
- `TVK_EXPECT_NEAR(a, b, eps)`: Floating-point absolute tolerance check ($|a - b| \le \epsilon$).
- `TVK_RUN_ALL_TESTS()`: Executes all registered tests in deterministic order, prints per-test `[ PASS ]` / `[ FAIL ]` diagnostics, and returns `0` iff 100% of assertions pass (`1` otherwise).

---

## 2. Active Baseline Test Suites (`test_core`, `test_math`, `test_renderer`)

### 2.1 `tests/test_core.cpp` (Layer 0 & Layer 1 Baseline Verification)
| Test Case | Defect(s) Verified | Invariant Tested |
| :--- | :--- | :--- |
| `HandlePool_CreateDestroyRecycle` | `B-32` | Generational `Handle<Tag>` creation (`gen == 1`), destruction invalidation (`IsValid == false`), slot reuse with incremented generation (`gen == 2`), and `Count()` tracking. |
| `Result_OkAndErrSemantics` | `B-32` | `Result<T, ErrorCode>::Ok` and `Err` under `-fno-exceptions`, `Value()`, `ValueOr()`, and `Error()`. |
| `JobSystem_ZeroThreadClampAndParallelFor` | `B-14`, `B-22`, `B-23` | `JobSystem(0)` clamps `GetNumThreads() >= 1` without unsigned underflow; `ParallelFor` and `ExecuteGrid` compute exact multi-threaded results across 1, 2, and 4 workers. |
| `Color_And_Types_Invariants` | `B-31`, `B-32` | `BlitterType` enum values and legacy aliases (`Braille`, `HalfBlock`, `Quadrant`, `Sextant`, `ASCII`), `Vec2` `.x/.y` and `.u/.v` accessors. |

---

### 2.2 `tests/test_math.cpp` (Layer 2 Math, SIMD & Camera Verification)
| Test Case | Defect(s) Verified | Invariant Tested |
| :--- | :--- | :--- |
| `Vec_Mat4_Quat_AABB_Operations` | `B-31`, `B-32` | Vector arithmetic, `Cross`, `Dot`, `Normalize`, `Mat4Multiply`, `QuatToMat4`, and `AABB` bounds. |
| `Mat4NormalMatrix_NonUniformScale` | `B-07` | Non-uniform scaling `Mat4Scale({1, 4, 1})` transforms tilted normals via `Mat4NormalMatrix` perpendicular to the scaled surface. |
| `BuildOrthonormalBasis_NoSingularity` | `B-05` | Duff et al. branchless ONB produces unit orthogonal axes ($|\hat{u}\cdot\hat{v}| < 10^{-5}$) for all directions including $(0, 0, -1)$ and $(0, \pm 1, 0)$. |
| `MathSIMD_ZeroLengthNormalSafety` | `B-16`, `B-21`, `B-22` | `NormalizeFast({0, 0, 0})` and `TransformNormalsAVX2` on zero-length normals return `{0, 0, 0}` with zero `NaN`s in both AVX2 and Scalar modes. |
| `Camera_LookAt_PositiveZForward` | `B-18` | `Camera::LookAt({0, 0, -10}, {0, 0, 0}, {0, 1, 0})` maps target `{0, 0, 0}` to `z_view = +10.0f > 0`. |
| `Camera_Orbit_ImmediateCacheUpdate` | `B-19` | `Camera::Orbit(yaw, pitch)` updates `GetViewMatrix()` immediately without a separate `UpdateCache()` call. |
| `Camera_Orthographic_FrustumPlanes` | `B-20` | `Camera::GetProjectionMatrix(Orthographic)` maps $z \in [n, f] \to [-1, +1]$ and `IsAABBInsideFrustum` accepts visible geometry in front of the camera. |

---

### 2.3 `tests/test_renderer.cpp` (Layer 3 Headless Rasterizer & Presenter Verification)
| Test Case | Defect(s) Verified | Invariant Tested |
| :--- | :--- | :--- |
| `Primitives_ParameterClamping` | `B-29` | Passing `<= 0` subdivisions to `CreateSphere`, `CreateCylinder`, `CreateCapsule`, `CreateGround` clamps safely and generates valid non-empty geometry. |
| `Renderer_Headless_VertexColorAndLighting` | `B-02`, `B-04`, `B-10`, `B-15`, `B-17`, `B-24`, `B-26`, `B-30` | Headless rendering of a custom vertex-colored mesh under colored RGB lights preserves vertex colors, populates `FrameStats` (`trianglesSubmitted`, `trianglesRasterized`), and sizes buffers to `FallbackWidth × FallbackHeight`. |
| `Renderer_OrientedCylinderCapsuleAndLines` | `B-05`, `B-06` | `DrawCylinder`, `DrawCapsule`, and `DrawLine` along arbitrary 3D diagonals rasterize non-empty pixels into the headless backbuffer. |
| `Renderer_OrthographicAndDynamicNearPlane` | `B-08`, `B-09`, `B-20`, `B-25` | Orthographic camera and custom near plane (`0.5f`) clip and project geometry accurately without integer overflow. |
| `Renderer_DepthOpAlwaysOrderAndAllBlitters` | `B-11`, `B-12`, `B-26`, `B-27`, `B-28` | `DepthOp::Always` preserves draw order; all 5 blitters (`Braille`, `HalfBlock`, `Quadrant`, `Sextant`, `ASCII`) produce valid UTF-8 output framed by `DECSET 2026` (`\x1b[?2026h`..`\x1b[?2026l`). |

---

## 3. 4-Configuration Compiler & SIMD Equivalence Verification Matrix

Every code change must build with **0 warnings** (`-Wall -Wextra -Wpedantic`) and pass `ctest --output-on-failure` across all 4 matrix configurations:

```bash
# Config 1: Release + AVX2 SIMD ON
cmake -S . -B /tmp/tvk_rel_simd -DCMAKE_BUILD_TYPE=Release -DTVK_ENABLE_SIMD=ON -DTVK_BUILD_TESTS=ON
cmake --build /tmp/tvk_rel_simd -j && ctest --test-dir /tmp/tvk_rel_simd --output-on-failure

# Config 2: Release + Scalar Fallback (SIMD OFF)
cmake -S . -B /tmp/tvk_rel_scalar -DCMAKE_BUILD_TYPE=Release -DTVK_ENABLE_SIMD=OFF -DTVK_BUILD_TESTS=ON
cmake --build /tmp/tvk_rel_scalar -j && ctest --test-dir /tmp/tvk_rel_scalar --output-on-failure

# Config 3: Debug + AVX2 SIMD ON
cmake -S . -B /tmp/tvk_dbg_simd -DCMAKE_BUILD_TYPE=Debug -DTVK_ENABLE_SIMD=ON -DTVK_BUILD_TESTS=ON
cmake --build /tmp/tvk_dbg_simd -j && ctest --test-dir /tmp/tvk_dbg_simd --output-on-failure

# Config 4: Debug + Scalar Fallback (SIMD OFF)
cmake -S . -B /tmp/tvk_dbg_scalar -DCMAKE_BUILD_TYPE=Debug -DTVK_ENABLE_SIMD=OFF -DTVK_BUILD_TESTS=ON
cmake --build /tmp/tvk_dbg_scalar -j && ctest --test-dir /tmp/tvk_dbg_scalar --output-on-failure

rm -rf /tmp/tvk_rel_simd /tmp/tvk_rel_scalar /tmp/tvk_dbg_simd /tmp/tvk_dbg_scalar
```

---

## 4. Quantitative Frame-Time & Memory Benchmark Budgets (`tests/benchmark_runner.cpp`)

Target workload: **`160 × 50` terminal cells** (`320 × 200` sub-pixels in `Braille` mode), **10,000 triangles**, **4 active colored point lights**, **8 worker threads**:

| Pipeline Stage | Target CPU Time Budget | Memory Allocation Budget | Key Bottleneck Mitigation |
| :--- | :---: | :---: | :--- |
| **Task Setup & Frustum Culling** | $\le 0.15\text{ ms}$ | `0 B` heap alloc | Gribb-Hartmann 6-plane AABB test + `Mat4NormalMatrix` |
| **Phase 1: SoA Vertex Transform & RGB Lighting** | $\le 0.80\text{ ms}$ | `0 B` heap alloc | 8-wide AVX2 FMA matrix multiply + `NormalizeFast` |
| **Phase 2: Clipping, Projection & Tile Binning** | $\le 1.20\text{ ms}$ | `0 B` heap alloc | Lock-free `std::atomic` CAS tile node linking |
| **Phase 3: L1 Tiled Rasterization & Cell Blitting** | $\le 2.50\text{ ms}$ | `32 KB` stack/thread | Stack-resident L1 `local_fb` + `local_zb`, Early-Z sort |
| **Phase 4: 2D Canvas + Parallel ANSI Diff** | $\le 0.65\text{ ms}$ | `0 B` heap alloc | `constexpr` byte-to-decimal LUT + pre-sized `m_FinalOutput` |
| **Total Engine CPU Frame Time** | **$\le 5.30\text{ ms}$ ($\ge 188\text{ FPS}$)** | **$< 16\text{ MB}$ total RAM** | **$11.37\text{ ms}$ ($68\%$) headroom at $60\text{ FPS}$ ($16.67\text{ ms}$)** |
