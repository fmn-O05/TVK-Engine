#include "TestHarness.h"
#include "tvk/TVK.h"
#include "tvk/renderer/Rasterizer.h"
#include "tvk/renderer/CommandBuffer.h"
#include "tvk/renderer/Primitives.h"
#include "tvk/renderer/Renderer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#if defined(TVK_SIMD_AVX2) && defined(__AVX2__)
#include <immintrin.h>
#endif

using namespace tvk;

namespace {

// Reference scalar TopLeft bias rule matching TVK rasterizer
inline int32_t RefTopLeftBias(int32_t dx, int32_t dy) noexcept {
  return (dx > 0 || (dx == 0 && dy < 0)) ? 0 : -1;
}

// Reference pure-scalar tile rasterizer for parity verification
void ReferenceScalarRasterizeTile(const ProjectedTriangle &tri, int tile_x,
                                 int tile_y, Pixel *local_fb,
                                 float *local_zb, int tile_size) {
  const Vec3 &p0 = tri.p0;
  const Vec3 &p1 = tri.p1;
  const Vec3 &p2 = tri.p2;
  const Rect &bb = tri.bounding_box;

  const int t_min_x = tile_x * tile_size;
  const int t_min_y = tile_y * tile_size;

  const int x0 = std::max(bb.min_x, t_min_x);
  const int y0 = std::max(bb.min_y, t_min_y);
  const int x1 = std::min(bb.max_x, t_min_x + tile_size - 1);
  const int y1 = std::min(bb.max_y, t_min_y + tile_size - 1);
  if (x0 > x1 || y0 > y1)
    return;

  const int32_t p0x = static_cast<int32_t>(std::round(p0.x - 0.5f));
  const int32_t p0y = static_cast<int32_t>(std::round(p0.y - 0.5f));
  const int32_t p1x = static_cast<int32_t>(std::round(p1.x - 0.5f));
  const int32_t p1y = static_cast<int32_t>(std::round(p1.y - 0.5f));
  const int32_t p2x = static_cast<int32_t>(std::round(p2.x - 0.5f));
  const int32_t p2y = static_cast<int32_t>(std::round(p2.y - 0.5f));

  const int32_t dx01 = p0y - p1y, dy01 = p1x - p0x;
  const int32_t dx12 = p1y - p2y, dy12 = p2x - p1x;
  const int32_t dx20 = p2y - p0y, dy20 = p0x - p2x;

  const int32_t bias01 = RefTopLeftBias(dx01, dy01);
  const int32_t bias12 = RefTopLeftBias(dx12, dy12);
  const int32_t bias20 = RefTopLeftBias(dx20, dy20);

  for (int py = y0; py <= y1; ++py) {
    const int32_t e01_row = static_cast<int32_t>(
        static_cast<int64_t>(x0 - p0x) * dx01 +
        static_cast<int64_t>(py - p0y) * dy01 + bias01);
    const int32_t e12_row = static_cast<int32_t>(
        static_cast<int64_t>(x0 - p1x) * dx12 +
        static_cast<int64_t>(py - p1y) * dy12 + bias12);
    const int32_t e20_row = static_cast<int32_t>(
        static_cast<int64_t>(x0 - p2x) * dx20 +
        static_cast<int64_t>(py - p2y) * dy20 + bias20);

    int scan_min_x = x0;
    int scan_max_x = x1;

    auto intersect = [&](int32_t e_row, int32_t dx) {
      if (dx > 0) {
        if (e_row < 0) {
          int steps = (-e_row + dx - 1) / dx;
          scan_min_x = std::max(scan_min_x, x0 + steps);
        }
      } else if (dx < 0) {
        if (e_row < 0) {
          scan_max_x = -1;
        } else {
          int steps = e_row / (-dx);
          scan_max_x = std::min(scan_max_x, x0 + steps);
        }
      } else {
        if (e_row < 0)
          scan_max_x = -1;
      }
    };

    intersect(e01_row, dx01);
    intersect(e12_row, dx12);
    intersect(e20_row, dx20);

    if (scan_min_x > scan_max_x)
      continue;

    const int local_py = py - t_min_y;
    const int row_idx = local_py * tile_size;

    const int step_delta = scan_min_x - x0;
    int32_t e01 = e01_row + step_delta * dx01;
    int32_t e12 = e12_row + step_delta * dx12;
    int32_t e20 = e20_row + step_delta * dx20;

    float vooz =
        tri.ooz[0] + (scan_min_x - p0x) * tri.dz_dx + (py - p0y) * tri.dz_dy;
    float r_oz =
        tri.col_r[0] + (scan_min_x - p0x) * tri.dr_dx + (py - p0y) * tri.dr_dy;
    float g_oz =
        tri.col_g[0] + (scan_min_x - p0x) * tri.dg_dx + (py - p0y) * tri.dg_dy;
    float b_oz =
        tri.col_b[0] + (scan_min_x - p0x) * tri.db_dx + (py - p0y) * tri.db_dy;

    const int strict_end = std::min(scan_max_x, x1);
    for (int px = scan_min_x; px <= strict_end; ++px, e01 += dx01, e12 += dx12,
             e20 += dx20, vooz += tri.dz_dx, r_oz += tri.dr_dx,
             g_oz += tri.dg_dx, b_oz += tri.db_dx) {
      if (e01 < 0 || e12 < 0 || e20 < 0)
        continue;

      const int idx = row_idx + (px - t_min_x);
      if (tri.depthOp == DepthOp::Less) {
        if (vooz <= local_zb[idx])
          continue;
      }

      local_zb[idx] = vooz;
      const float safe_vooz = std::max(vooz, 1e-6f);
      // Newton-Raphson refined reciprocal matching SIMD pipeline
      float inv_vooz = 1.0f / safe_vooz;
      inv_vooz = inv_vooz * (2.0f - safe_vooz * inv_vooz);

      float r = r_oz * inv_vooz;
      float g = g_oz * inv_vooz;
      float b = b_oz * inv_vooz;

      r = std::clamp(r, 0.0f, 255.0f);
      g = std::clamp(g, 0.0f, 255.0f);
      b = std::clamp(b, 0.0f, 255.0f);

      local_fb[idx].r = static_cast<uint8_t>(std::round(r));
      local_fb[idx].g = static_cast<uint8_t>(std::round(g));
      local_fb[idx].b = static_cast<uint8_t>(std::round(b));
      local_fb[idx].a = 255;
    }
  }
}

int CountNonBlankCells(const Renderer &r) {
  int count = 0;
  for (const auto &cell : r.GetBackBuffer()) {
    if (cell.utf8_len > 1 || cell.utf8[0] != ' ' || cell.has_bg) {
      ++count;
    }
  }
  return count;
}

} // namespace

// ============================================================================
// Suite 1: Extended_SortKey_LayoutAndSorting
// ============================================================================

TVK_TEST(Extended_SortKey_LayoutAndSorting_BitfieldPacking) {
  // 6 bitfields: [Pass:4 | Layer:4 | DepthOp:4 | Mat:20 | Depth:16 | Seq:16]
  TVK_EXPECT_EQ(SortKey::kPassShift, 60u);
  TVK_EXPECT_EQ(SortKey::kLayerShift, 56u);
  TVK_EXPECT_EQ(SortKey::kDepthOpShift, 52u);
  TVK_EXPECT_EQ(SortKey::kMaterialShift, 32u);
  TVK_EXPECT_EQ(SortKey::kDepthShift, 16u);
  TVK_EXPECT_EQ(SortKey::kSeqShift, 0u);

  // 1. All zero inputs
  const uint64_t zeroKey = SortKey::EncodeSortKey(0, 0, DepthOp::Less, 0, 0, 0);
  TVK_EXPECT_EQ(zeroKey, 0ull);
  SortKey zeroSk(zeroKey);
  TVK_EXPECT_EQ(zeroSk.GetPass(), 0u);
  TVK_EXPECT_EQ(zeroSk.GetLayer(), 0u);
  TVK_EXPECT_EQ(static_cast<uint8_t>(zeroSk.GetDepthOp()), static_cast<uint8_t>(DepthOp::Less));
  TVK_EXPECT_EQ(zeroSk.GetMaterialId(), 0u);
  TVK_EXPECT_EQ(zeroSk.GetDepth(), 0u);
  TVK_EXPECT_EQ(zeroSk.GetSequence(), 0u);

  // 2. Individual maximum field tests (ensuring no field overlap or corruption)
  // Pass: 4 bits [0..15]
  uint64_t kPass = SortKey::EncodeSortKey(0xFu, 0, DepthOp::Less, 0, 0, 0);
  TVK_EXPECT_EQ(kPass, 0xF000000000000000ull);
  TVK_EXPECT_EQ(SortKey::ExtractPass(kPass), 0xFu);

  // Layer: 4 bits [0..15]
  uint64_t kLayer = SortKey::EncodeSortKey(0, 0xFu, DepthOp::Less, 0, 0, 0);
  TVK_EXPECT_EQ(kLayer, 0x0F00000000000000ull);
  TVK_EXPECT_EQ(SortKey::ExtractLayer(kLayer), 0xFu);

  // DepthOp: 4 bits [0..15]
  uint64_t kDepthOp = SortKey::EncodeSortKey(0, 0, static_cast<DepthOp>(0xFu), 0, 0, 0);
  TVK_EXPECT_EQ(kDepthOp, 0x00F0000000000000ull);
  TVK_EXPECT_EQ(static_cast<uint8_t>(SortKey::ExtractDepthOp(kDepthOp)), 0xFu);

  // Material: 20 bits [0..0xFFFFF]
  uint64_t kMat = SortKey::EncodeSortKey(0, 0, DepthOp::Less, 0xFFFFFu, 0, 0);
  TVK_EXPECT_EQ(kMat, 0x000FFFFF00000000ull);
  TVK_EXPECT_EQ(SortKey::ExtractMaterialId(kMat), 0xFFFFFu);

  // Depth: 16 bits [0..0xFFFF]
  uint64_t kDepth = SortKey::EncodeSortKey(0, 0, DepthOp::Less, 0, 0xFFFFu, 0);
  TVK_EXPECT_EQ(kDepth, 0x00000000FFFF0000ull);
  TVK_EXPECT_EQ(SortKey::ExtractDepth(kDepth), 0xFFFFu);

  // Sequence: 16 bits [0..0xFFFF]
  uint64_t kSeq = SortKey::EncodeSortKey(0, 0, DepthOp::Less, 0, 0, 0xFFFFu);
  TVK_EXPECT_EQ(kSeq, 0x000000000000FFFFull);
  TVK_EXPECT_EQ(SortKey::ExtractSequence(kSeq), 0xFFFFu);

  // 3. All fields simultaneously saturated at maximum
  uint64_t allOnes = SortKey::Pack(0xFu, 0xFu, static_cast<DepthOp>(0xFu), 0xFFFFFu, 0xFFFFu, 0xFFFFu);
  TVK_EXPECT_EQ(allOnes, 0xFFFFFFFFFFFFFFFFull);

  // 4. Arbitrary pattern packing and member getters
  SortKey mixed = SortKey::Encode(9, 5, DepthOp::Greater, 0x5A5A5, 0x1234, 0x89AB);
  TVK_EXPECT_EQ(mixed.GetPass(), 9u);
  TVK_EXPECT_EQ(mixed.GetLayer(), 5u);
  TVK_EXPECT_EQ(mixed.GetDepthOp(), DepthOp::Greater);
  TVK_EXPECT_EQ(mixed.GetMaterialId(), 0x5A5A5u);
  TVK_EXPECT_EQ(mixed.GetDepth(), 0x1234u);
  TVK_EXPECT_EQ(mixed.GetSequence(), 0x89ABu);

  // Comparison operator testing
  SortKey skA(100ull);
  SortKey skB(200ull);
  SortKey skC(100ull);
  TVK_EXPECT_TRUE(skA < skB);
  TVK_EXPECT_TRUE(skA <= skB);
  TVK_EXPECT_TRUE(skA <= skC);
  TVK_EXPECT_TRUE(skB > skA);
  TVK_EXPECT_TRUE(skB >= skA);
  TVK_EXPECT_TRUE(skA == skC);
  TVK_EXPECT_TRUE(skA != skB);
}

TVK_TEST(Extended_SortKey_LayoutAndSorting_DepthQuantizationAndOrdering) {
  // Setup camera with known near/far
  Camera cam;
  cam.LookAt({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 10.0f}, {0.0f, 1.0f, 0.0f});
  cam.SetPerspective(60.0f, 1.0f, 101.0f); // near=1.0f, far=101.0f, range=100.0f

  CommandBuffer cmd;
  cmd.Begin();

  // Test 1: DepthOp::Always must quantize depth to 0
  PipelineState psoAlways;
  psoAlways.depthOp = DepthOp::Always;
  psoAlways.pass = 1;
  cmd.BindPipeline(psoAlways);
  cmd.DrawWireBox({0, 0, 50}, {1, 1, 51}, {255, 255, 255, 255}); // 12 lines at z~50.5

  // Test 2: DepthOp::Less (Early-Z front-to-back: near->0, far->65535)
  PipelineState psoLess;
  psoLess.depthOp = DepthOp::Less;
  psoLess.pass = 2;
  cmd.BindPipeline(psoLess);
  // Mesh near (z = 1.0) -> depth 0
  cmd.DrawWireBox({-1, -1, 0.5f}, {1, 1, 1.5f}, {255, 0, 0, 255}); // center z=1.0
  // Mesh mid (z = 51.0) -> depth ~32768
  cmd.DrawWireBox({-1, -1, 50.5f}, {1, 1, 51.5f}, {0, 255, 0, 255}); // center z=51.0
  // Mesh far (z = 101.0) -> depth 65535
  cmd.DrawWireBox({-1, -1, 100.5f}, {1, 1, 101.5f}, {0, 0, 255, 255}); // center z=101.0

  // Test 3: DepthOp::Greater (back-to-front inverted: near->65535, far->0)
  PipelineState psoGreater;
  psoGreater.depthOp = DepthOp::Greater;
  psoGreater.pass = 3;
  cmd.BindPipeline(psoGreater);
  cmd.DrawWireBox({-1, -1, 0.5f}, {1, 1, 1.5f}, {255, 255, 0, 255}); // near -> 65535
  cmd.DrawWireBox({-1, -1, 100.5f}, {1, 1, 101.5f}, {0, 255, 255, 255}); // far -> 0

  cmd.End();

  // Perform sorting
  cmd.Sort(&cam);

  const auto &sorted = cmd.GetCommands();
  TVK_EXPECT_TRUE(sorted.size() >= 72u); // 6 wireboxes * 12 lines = 72

  // Verify pass 1 (Always) has depth == 0
  for (size_t i = 0; i < 12; ++i) {
    TVK_EXPECT_EQ(SortKey::ExtractPass(sorted[i].sortKey), 1u);
    TVK_EXPECT_EQ(SortKey::ExtractDepth(sorted[i].sortKey), 0u);
  }

  // Verify pass 2 (Less) is ordered front-to-back:
  // First 12 lines near (depth ~0)
  const uint16_t depthNear = SortKey::ExtractDepth(sorted[12].sortKey);
  TVK_EXPECT_TRUE(depthNear <= 100u); // close to 0

  // Mid 12 lines (depth ~32768)
  const uint16_t depthMid = SortKey::ExtractDepth(sorted[24].sortKey);
  TVK_EXPECT_TRUE(depthMid > 30000u && depthMid < 35000u);

  // Far 12 lines (depth ~65535)
  const uint16_t depthFar = SortKey::ExtractDepth(sorted[36].sortKey);
  TVK_EXPECT_TRUE(depthFar >= 65000u);

  // Verify Pass 3 (Greater) is ordered back-to-front:
  // Far object quantized to 0, near object quantized to 65535
  const uint16_t gFirst = SortKey::ExtractDepth(sorted[48].sortKey);
  const uint16_t gLast = SortKey::ExtractDepth(sorted[60].sortKey);
  TVK_EXPECT_TRUE(gFirst <= 100u); // Far object depth quantized to 0
  TVK_EXPECT_TRUE(gLast >= 65000u); // Near object depth quantized to ~65535
}

TVK_TEST(Extended_SortKey_LayoutAndSorting_StateGroupingAndEarlyZ) {
  // Test hierarchical sorting: Pass -> Layer -> DepthOp -> MaterialId -> Depth -> Sequence
  Camera cam;
  cam.LookAt({0, 0, 0}, {0, 0, 10}, {0, 1, 0});
  cam.SetPerspective(60.0f, 1.0f, 100.0f);

  CommandBuffer cmd;
  cmd.Begin();

  // Create a mesh quad
  std::vector<Vertex> verts = {
      {{-1, -1, 0}, {0, 0, -1}, {255, 255, 255, 255}},
      {{ 1, -1, 0}, {0, 0, -1}, {255, 255, 255, 255}},
      {{ 0,  1, 0}, {0, 0, -1}, {255, 255, 255, 255}}
  };
  std::vector<uint32_t> inds = {0, 1, 2};
  Mesh tri(verts, inds);

  // Command 0: Pass 2
  PipelineState p0; p0.pass = 2; p0.materialId = 1;
  cmd.BindPipeline(p0);
  cmd.DrawMesh(tri, Mat4Translate({0, 0, 10}));

  // Command 1: Pass 1, Layer 1
  PipelineState p1; p1.pass = 1; p1.layer = 1; p1.materialId = 1;
  cmd.BindPipeline(p1);
  cmd.DrawMesh(tri, Mat4Translate({0, 0, 10}));

  // Command 2: Pass 1, Layer 0, Material 5, z=20
  PipelineState p2; p2.pass = 1; p2.layer = 0; p2.materialId = 5;
  cmd.BindPipeline(p2);
  cmd.DrawMesh(tri, Mat4Translate({0, 0, 20}));

  // Command 3: Pass 1, Layer 0, Material 2, z=30 (farther)
  PipelineState p3; p3.pass = 1; p3.layer = 0; p3.materialId = 2;
  cmd.BindPipeline(p3);
  cmd.DrawMesh(tri, Mat4Translate({0, 0, 30}));

  // Command 4: Pass 1, Layer 0, Material 2, z=10 (closer, should sort before Command 3)
  PipelineState p4; p4.pass = 1; p4.layer = 0; p4.materialId = 2;
  cmd.BindPipeline(p4);
  cmd.DrawMesh(tri, Mat4Translate({0, 0, 10}));

  // Command 5: Pass 1, Layer 0, Material 2, z=10 (identical key to Command 4, tests stable sequence)
  cmd.DrawMesh(tri, Mat4Translate({0, 0, 10}));

  cmd.End();
  cmd.Sort(&cam);

  const auto &res = cmd.GetCommands();
  TVK_EXPECT_EQ(res.size(), 6u);

  // 1. All Pass 1 commands must precede Pass 2
  TVK_EXPECT_EQ(SortKey::ExtractPass(res[0].sortKey), 1u);
  TVK_EXPECT_EQ(SortKey::ExtractPass(res[1].sortKey), 1u);
  TVK_EXPECT_EQ(SortKey::ExtractPass(res[2].sortKey), 1u);
  TVK_EXPECT_EQ(SortKey::ExtractPass(res[3].sortKey), 1u);
  TVK_EXPECT_EQ(SortKey::ExtractPass(res[4].sortKey), 1u);
  TVK_EXPECT_EQ(SortKey::ExtractPass(res[5].sortKey), 2u);

  // 2. Layer 0 commands precede Layer 1
  TVK_EXPECT_EQ(SortKey::ExtractLayer(res[4].sortKey), 1u);

  // 3. Material 2 precedes Material 5
  TVK_EXPECT_EQ(SortKey::ExtractMaterialId(res[0].sortKey), 2u);
  TVK_EXPECT_EQ(SortKey::ExtractMaterialId(res[1].sortKey), 2u);
  TVK_EXPECT_EQ(SortKey::ExtractMaterialId(res[2].sortKey), 2u);
  TVK_EXPECT_EQ(SortKey::ExtractMaterialId(res[3].sortKey), 5u);

  // 4. Early-Z ordering within Material 2: z=10 comes before z=30
  TVK_EXPECT_TRUE(SortKey::ExtractDepth(res[0].sortKey) < SortKey::ExtractDepth(res[2].sortKey));

  // 5. Stable sorting: sequence of res[0] must precede res[1]
  TVK_EXPECT_TRUE(SortKey::ExtractSequence(res[0].sortKey) < SortKey::ExtractSequence(res[1].sortKey));
}

// ============================================================================
// Suite 2: Extended_ShadingModels_Evaluation
// ============================================================================

TVK_TEST(Extended_ShadingModels_Diffuse_NdotL_AnglesAndClamping) {
  // Test diffuse lighting calculations
  const Vec3 N = {0.0f, 1.0f, 0.0f};

  // 1. Direct overhead lighting: N dot L = 1.0
  const Vec3 L_overhead = {0.0f, 1.0f, 0.0f};
  const float nl_overhead = std::max(0.0f, Dot(N, L_overhead));
  TVK_EXPECT_NEAR(nl_overhead, 1.0f, 1e-6f);

  // 2. 45-degree angle: N dot L = cos(45 deg) = sqrt(0.5) ~ 0.7071
  const Vec3 L_45 = Normalize({1.0f, 1.0f, 0.0f});
  const float nl_45 = std::max(0.0f, Dot(N, L_45));
  TVK_EXPECT_NEAR(nl_45, std::sqrt(0.5f), 1e-5f);

  // 3. Grazing angle: N dot L = 0.0
  const Vec3 L_grazing = {1.0f, 0.0f, 0.0f};
  const float nl_grazing = std::max(0.0f, Dot(N, L_grazing));
  TVK_EXPECT_NEAR(nl_grazing, 0.0f, 1e-6f);

  // 4. Back-facing / pointing away: clamped strictly to 0.0
  const Vec3 L_back = {0.0f, -1.0f, 0.0f};
  const float nl_back = std::max(0.0f, Dot(N, L_back));
  TVK_EXPECT_NEAR(nl_back, 0.0f, 1e-6f);
}

TVK_TEST(Extended_ShadingModels_Specular_HalfVectorPowerAndShininess) {
  const Vec3 N = {0.0f, 1.0f, 0.0f};
  const Vec3 L = {0.0f, 1.0f, 0.0f};
  const Vec3 V = {0.0f, 1.0f, 0.0f};

  // Collinear: H = Normalize(L + V) = N -> N dot H = 1.0
  const Vec3 H_aligned = Normalize(Add(L, V));
  const float nh_aligned = std::max(0.0f, Dot(N, H_aligned));
  TVK_EXPECT_NEAR(nh_aligned, 1.0f, 1e-6f);
  TVK_EXPECT_NEAR(std::pow(nh_aligned, 32.0f), 1.0f, 1e-6f);

  // Angular displacement: V tilted so N dot H = 0.85
  // Compare low shininess vs high shininess falloff
  const float nh_tilt = 0.85f;
  const float specLowShininess = std::pow(nh_tilt, 4.0f);   // 0.85^4 ~ 0.5220
  const float specHighShininess = std::pow(nh_tilt, 32.0f); // 0.85^32 ~ 0.0055

  TVK_EXPECT_NEAR(specLowShininess, 0.522006f, 1e-4f);
  TVK_EXPECT_NEAR(specHighShininess, 0.005537f, 1e-4f);
  TVK_EXPECT_TRUE(specLowShininess > specHighShininess * 50.0f);
}

TVK_TEST(Extended_ShadingModels_Toon_QuantizationAndThresholds) {
  // Test Toon stepped diffuse quantization: diff_factor = floor(nl * steps) / steps
  const int steps = 3;
  const float s = static_cast<float>(steps);

  auto toonDiffuse = [s](float nl) {
    if (nl >= 1.0f) return 1.0f;
    return std::floor(nl * s) / s;
  };

  TVK_EXPECT_NEAR(toonDiffuse(0.00f), 0.0f, 1e-6f);
  TVK_EXPECT_NEAR(toonDiffuse(0.25f), 0.0f, 1e-6f);
  TVK_EXPECT_NEAR(toonDiffuse(0.334f), 1.0f / 3.0f, 1e-4f);
  TVK_EXPECT_NEAR(toonDiffuse(0.50f), 1.0f / 3.0f, 1e-4f);
  TVK_EXPECT_NEAR(toonDiffuse(0.667f), 2.0f / 3.0f, 1e-4f);
  TVK_EXPECT_NEAR(toonDiffuse(0.95f), 2.0f / 3.0f, 1e-4f);
  TVK_EXPECT_NEAR(toonDiffuse(1.00f), 1.0f, 1e-6f);

  // Test Toon specular binary threshold
  const float threshold = 0.5f;
  const float shininess = 16.0f;

  auto toonSpecular = [threshold, shininess](float nh) {
    if (nh <= 0.0f) return 0.0f;
    const float raw = std::pow(nh, shininess);
    return (raw >= threshold) ? 1.0f : 0.0f;
  };

  // With shininess=16, threshold=0.5: cut is at nh = 0.5^(1/16) ~ 0.9576
  TVK_EXPECT_EQ(toonSpecular(0.90f), 0.0f); // 0.90^16 ~ 0.185 < 0.5 -> 0.0
  TVK_EXPECT_EQ(toonSpecular(0.98f), 1.0f); // 0.98^16 ~ 0.723 >= 0.5 -> 1.0
}

TVK_TEST(Extended_ShadingModels_UnlitAndGouraudAndFlat) {
  // Verify Unlit model preserves albedo without light modulation
  MaterialState matUnlit;
  matUnlit.shadingModel = ShadingModel::Unlit;
  matUnlit.ambient = 0.2f;
  matUnlit.diffuse = 0.8f;
  // target_dr = (1.0 - 0.2) / 0.8 = 1.0 -> ambient + diffuse * 1.0 = 1.0
  const float target_dr = (matUnlit.diffuse > 1e-4f)
                              ? ((1.0f - matUnlit.ambient) / matUnlit.diffuse)
                              : 1.0f;
  TVK_EXPECT_NEAR(target_dr, 1.0f, 1e-6f);
  TVK_EXPECT_NEAR(matUnlit.ambient + matUnlit.diffuse * target_dr, 1.0f, 1e-6f);

  // Verify Flat shading computes face normal from cross product
  const Vec3 v0 = {0.0f, 0.0f, 0.0f};
  const Vec3 v1 = {5.0f, 0.0f, 0.0f};
  const Vec3 v2 = {0.0f, 5.0f, 0.0f};
  const Vec3 e1 = Sub(v1, v0);
  const Vec3 e2 = Sub(v2, v0);
  const Vec3 cross = Cross(e1, e2);
  const Vec3 faceNorm = Normalize(cross);
  TVK_EXPECT_NEAR(faceNorm.x, 0.0f, 1e-5f);
  TVK_EXPECT_NEAR(faceNorm.y, 0.0f, 1e-5f);
  TVK_EXPECT_NEAR(faceNorm.z, 1.0f, 1e-5f);
}

// ============================================================================
// Suite 3: Extended_LightAttenuation_Falloff
// ============================================================================

TVK_TEST(Extended_LightAttenuation_DistanceFalloff) {
  // Formula: attenuation = 1.0 / max(1e-6, kc + kl * d + kq * d^2)
  Light light;
  light.type = LightType::Point;
  light.range = 50.0f;
  light.attenConstant = 1.0f;
  light.attenLinear = 0.1f;
  light.attenQuadratic = 0.02f;

  auto computeAtten = [](const Light &l, float d) {
    if (d > l.range) return 0.0f;
    const float denom = l.attenConstant + l.attenLinear * d + l.attenQuadratic * (d * d);
    return 1.0f / std::max(1e-6f, denom);
  };

  // 1. Distance d = 0: 1 / 1.0 = 1.0
  TVK_EXPECT_NEAR(computeAtten(light, 0.0f), 1.0f, 1e-6f);

  // 2. Distance d = 10: 1 / (1.0 + 1.0 + 2.0) = 1 / 4.0 = 0.25
  TVK_EXPECT_NEAR(computeAtten(light, 10.0f), 0.25f, 1e-6f);

  // 3. Distance d = 20: 1 / (1.0 + 2.0 + 8.0) = 1 / 11.0 ~ 0.090909
  TVK_EXPECT_NEAR(computeAtten(light, 20.0f), 1.0f / 11.0f, 1e-5f);

  // 4. Range cutoff: d <= 50.0 is active; d > 50.0 is strictly culled to 0.0
  TVK_EXPECT_TRUE(computeAtten(light, 49.9f) > 0.0f);
  TVK_EXPECT_EQ(computeAtten(light, 50.1f), 0.0f);
  TVK_EXPECT_EQ(computeAtten(light, 100.0f), 0.0f);
}

TVK_TEST(Extended_LightAttenuation_SpotConeHermiteCutoff) {
  // Spot cone smooth Hermite interpolation:
  // s = clamp((cosTheta - outer) / (inner - outer), 0, 1)
  // spotFactor = s * s * (3 - 2 * s)
  const float innerCutoff = 0.95f; // ~18.2 deg
  const float outerCutoff = 0.85f; // ~31.8 deg

  auto spotFactor = [innerCutoff, outerCutoff](float cosTheta) {
    if (cosTheta <= outerCutoff) return 0.0f;
    if (cosTheta >= innerCutoff) return 1.0f;
    const float s = (cosTheta - outerCutoff) / (innerCutoff - outerCutoff);
    return s * s * (3.0f - 2.0f * s);
  };

  // 1. Directly on axis: cosTheta = 1.0 -> 1.0
  TVK_EXPECT_NEAR(spotFactor(1.0f), 1.0f, 1e-6f);

  // 2. Inside inner cone: cosTheta = 0.98 -> 1.0
  TVK_EXPECT_NEAR(spotFactor(0.98f), 1.0f, 1e-6f);

  // 3. Outside outer cone: cosTheta = 0.80 -> 0.0
  TVK_EXPECT_NEAR(spotFactor(0.80f), 0.0f, 1e-6f);

  // 4. Exact midpoint of transition: cosTheta = 0.90 -> s = 0.5 -> spotFactor = 0.5
  TVK_EXPECT_NEAR(spotFactor(0.90f), 0.5f, 1e-6f);

  // 5. Quarter point: s = 0.25 -> 0.25^2 * (3 - 0.5) = 0.0625 * 2.5 = 0.15625
  const float cosQuarter = 0.85f + 0.25f * (innerCutoff - outerCutoff);
  TVK_EXPECT_NEAR(spotFactor(cosQuarter), 0.15625f, 1e-5f);

  // 6. Three-quarter point: s = 0.75 -> 0.75^2 * (3 - 1.5) = 0.5625 * 1.5 = 0.84375
  const float cosThreeQuarter = 0.85f + 0.75f * (innerCutoff - outerCutoff);
  TVK_EXPECT_NEAR(spotFactor(cosThreeQuarter), 0.84375f, 1e-5f);
}

TVK_TEST(Extended_LightAttenuation_DirectionalNoAttenuation) {
  Light dirLight;
  dirLight.type = LightType::Directional;
  dirLight.direction = {0.0f, -1.0f, 0.0f};
  dirLight.intensity = 1.5f;

  // Directional light must have attenuation == 1.0 regardless of distance
  TVK_EXPECT_EQ(dirLight.type, LightType::Directional);
}

// ============================================================================
// Suite 4: Extended_PerspectiveCorrect_UVInterpolation
// ============================================================================

TVK_TEST(Extended_PerspectiveCorrect_HyperbolicStepping_EliminatesAffineDistortion) {
  // Segment: A(z=2.0, u=0.0) -> B(z=8.0, u=1.0)
  // In screen space, linear interpolation steps ooz = 1/z and u_oz = u/z
  const float z0 = 2.0f;
  const float z1 = 8.0f;
  const float u0 = 0.0f;
  const float u1 = 1.0f;

  const float ooz0 = 1.0f / z0;  // 0.5
  const float ooz1 = 1.0f / z1;  // 0.125
  const float u_oz0 = u0 * ooz0; // 0.0
  const float u_oz1 = u1 * ooz1; // 0.125

  // Screen midpoint: interpolation factor t = 0.5
  const float ooz_mid = 0.5f * (ooz0 + ooz1);   // 0.3125
  const float u_oz_mid = 0.5f * (u_oz0 + u_oz1); // 0.0625

  // Perspective-corrected u at screen midpoint
  const float u_correct = u_oz_mid / ooz_mid; // 0.0625 / 0.3125 = 0.20
  TVK_EXPECT_NEAR(u_correct, 0.20f, 1e-6f);

  // Affine (non-perspective) would give midpoint u = 0.5
  const float u_affine = 0.5f * (u0 + u1);
  TVK_EXPECT_NEAR(u_affine, 0.50f, 1e-6f);

  // In 3D world space, the ray corresponding to screen midpoint has z = 1/ooz_mid = 3.2
  // Parametric position s along 3D segment: z(s) = z0 + s * (z1 - z0) = 3.2 -> s = 1.2 / 6.0 = 0.20
  // True 3D u at that point is u0 + s * (u1 - u0) = 0.20
  const float z_mid_3d = 1.0f / ooz_mid;
  TVK_EXPECT_NEAR(z_mid_3d, 3.2f, 1e-6f);
  const float s_3d = (z_mid_3d - z0) / (z1 - z0);
  TVK_EXPECT_NEAR(s_3d, 0.20f, 1e-6f);
  TVK_EXPECT_NEAR(u0 + s_3d * (u1 - u0), u_correct, 1e-6f);
}

TVK_TEST(Extended_PerspectiveCorrect_NewtonRaphson_ReciprocalPrecision) {
  // Test Newton-Raphson reciprocal refinement: x1 = x0 * (2 - d * x0)
  for (float d = 0.05f; d <= 500.0f; d += 2.5f) {
    const float exact = 1.0f / d;

    // Simulate standard ~11-bit reciprocal approximation x0
    // (introducing up to 0.1% initial error)
    const float x0 = exact * 1.001f;
    const float x1 = x0 * (2.0f - d * x0);

    const float relErr = std::abs(x1 - exact) / exact;
    TVK_EXPECT_TRUE(relErr < 1e-5f);
  }

#if defined(TVK_SIMD_AVX2) && defined(__AVX2__)
  // Verify AVX2 rcp_ps + fnmadd refinement accuracy against IEEE float
  const __m256 v_d = _mm256_setr_ps(0.1f, 1.0f, 5.0f, 10.0f, 25.0f, 50.0f, 100.0f, 250.0f);
  const __m256 v_two = _mm256_set1_ps(2.0f);
  __m256 v_rcp = _mm256_rcp_ps(v_d);
  v_rcp = _mm256_mul_ps(v_rcp, _mm256_fnmadd_ps(v_d, v_rcp, v_two));

  alignas(32) float simdBits[8];
  _mm256_storeu_ps(simdBits, v_rcp);

  alignas(32) float dBits[8];
  _mm256_storeu_ps(dBits, v_d);

  for (int i = 0; i < 8; ++i) {
    const float groundTruth = 1.0f / dBits[i];
    const float relErr = std::abs(simdBits[i] - groundTruth) / groundTruth;
    TVK_EXPECT_TRUE(relErr < 1e-5f);
  }
#endif
}

TVK_TEST(Extended_PerspectiveCorrect_NearPlaneClipping_Interpolation) {
  // Test Rasterizer::ClipAgainstNearPlane attribute interpolation
  const float nearPlane = 4.0f;

  // Case 1: 3 vertices behind near plane -> 0 triangles
  ShadeVertex behindTri[3]{};
  behindTri[0].position = {0, 0, 1.0f};
  behindTri[1].position = {1, 0, 2.0f};
  behindTri[2].position = {0, 1, 3.0f};
  ShadeVertex out[2][3]{};
  TVK_EXPECT_EQ(Rasterizer::ClipAgainstNearPlane(behindTri, out, nearPlane), 0);

  // Case 2: 3 vertices in front -> 1 triangle (identical)
  ShadeVertex inFrontTri[3]{};
  inFrontTri[0].position = {0, 0, 5.0f};
  inFrontTri[1].position = {1, 0, 6.0f};
  inFrontTri[2].position = {0, 1, 7.0f};
  TVK_EXPECT_EQ(Rasterizer::ClipAgainstNearPlane(inFrontTri, out, nearPlane), 1);
  TVK_EXPECT_NEAR(out[0][0].position.z, 5.0f, 1e-6f);

  // Case 3: 1 vertex in front (z=6.0), 2 vertices behind (z=2.0)
  // Generates 1 clipped triangle with 2 interpolated vertices at z = nearPlane
  ShadeVertex oneInFront[3]{};
  oneInFront[0].position = {0.0f, 0.0f, 6.0f};
  oneInFront[0].uv = {0.0f, 0.0f};
  oneInFront[0].color = {200, 0, 0, 255};

  oneInFront[1].position = {4.0f, 0.0f, 2.0f};
  oneInFront[1].uv = {1.0f, 0.0f};
  oneInFront[1].color = {0, 200, 0, 255};

  oneInFront[2].position = {0.0f, 4.0f, 2.0f};
  oneInFront[2].uv = {0.0f, 1.0f};
  oneInFront[2].color = {0, 0, 200, 255};

  const int count1 = Rasterizer::ClipAgainstNearPlane(oneInFront, out, nearPlane);
  TVK_EXPECT_EQ(count1, 1);
  // Vertex 0 remains inFront[0]
  TVK_EXPECT_NEAR(out[0][0].position.z, 6.0f, 1e-5f);
  // Vertices 1 and 2 clipped exactly onto near plane z = 4.0
  TVK_EXPECT_NEAR(out[0][1].position.z, 4.0f, 1e-5f);
  TVK_EXPECT_NEAR(out[0][2].position.z, 4.0f, 1e-5f);

  // Interpolation factor t = (4.0 - 6.0) / (2.0 - 6.0) = -2.0 / -4.0 = 0.5
  TVK_EXPECT_NEAR(out[0][1].uv.u, 0.5f, 1e-5f);
  TVK_EXPECT_NEAR(out[0][1].uv.v, 0.0f, 1e-5f);
  TVK_EXPECT_NEAR(out[0][2].uv.u, 0.0f, 1e-5f);
  TVK_EXPECT_NEAR(out[0][2].uv.v, 0.5f, 1e-5f);

  // Case 4: 2 vertices in front (z=6.0), 1 vertex behind (z=2.0) -> returns 2 triangles
  ShadeVertex twoInFront[3]{};
  twoInFront[0].position = {-2.0f, -2.0f, 6.0f};
  twoInFront[1].position = { 2.0f, -2.0f, 6.0f};
  twoInFront[2].position = { 0.0f,  2.0f, 2.0f};

  const int count2 = Rasterizer::ClipAgainstNearPlane(twoInFront, out, nearPlane);
  TVK_EXPECT_EQ(count2, 2);
  // Verify clipped edges lie on nearPlane
  for (int t = 0; t < 2; ++t) {
    for (int v = 0; v < 3; ++v) {
      TVK_EXPECT_TRUE(out[t][v].position.z >= nearPlane - 1e-5f);
    }
  }
}

// ============================================================================
// Suite 5: Extended_ProceduralPrimitives_Generation
// ============================================================================

TVK_TEST(Extended_ProceduralPrimitives_CreateBox) {
  Mesh box = primitives::CreateBox(2.0f, 4.0f, 6.0f, {255, 128, 64, 255});

  // 6 faces * 4 vertices = 24 vertices; 6 faces * 2 triangles * 3 indices = 36 indices
  TVK_EXPECT_EQ(box.GetVertices().size(), 24u);
  TVK_EXPECT_EQ(box.GetIndices().size(), 36u);

  // Check AABB bounds: [-1, 1] x [-2, 2] x [-3, 3]
  const AABB aabb = box.GetAABB();
  TVK_EXPECT_NEAR(aabb.min.x, -1.0f, 1e-5f);
  TVK_EXPECT_NEAR(aabb.min.y, -2.0f, 1e-5f);
  TVK_EXPECT_NEAR(aabb.min.z, -3.0f, 1e-5f);
  TVK_EXPECT_NEAR(aabb.max.x,  1.0f, 1e-5f);
  TVK_EXPECT_NEAR(aabb.max.y,  2.0f, 1e-5f);
  TVK_EXPECT_NEAR(aabb.max.z,  3.0f, 1e-5f);

  // Check the 6 face normals (+Z, -Z, +Y, -Y, +X, -X)
  const auto &verts = box.GetVertices();
  const Vec3 expectedNormals[6] = {
      { 0.0f,  0.0f,  1.0f}, // Front
      { 0.0f,  0.0f, -1.0f}, // Back
      { 0.0f,  1.0f,  0.0f}, // Top
      { 0.0f, -1.0f,  0.0f}, // Bottom
      { 1.0f,  0.0f,  0.0f}, // Right
      {-1.0f,  0.0f,  0.0f}  // Left
  };

  for (size_t f = 0; f < 6; ++f) {
    for (size_t v = 0; v < 4; ++v) {
      const Vec3 &n = verts[f * 4 + v].normal;
      TVK_EXPECT_NEAR(n.x, expectedNormals[f].x, 1e-5f);
      TVK_EXPECT_NEAR(n.y, expectedNormals[f].y, 1e-5f);
      TVK_EXPECT_NEAR(n.z, expectedNormals[f].z, 1e-5f);
    }
  }
}

TVK_TEST(Extended_ProceduralPrimitives_CreateCone) {
  const int segments = 12;
  Mesh cone = primitives::CreateCone(2.0f, 4.0f, segments, {200, 200, 200, 255});

  // Base disk has segments triangles; side has segments triangles -> segments * 6 indices
  TVK_EXPECT_EQ(cone.GetIndices().size(), static_cast<size_t>(segments * 6));
  TVK_EXPECT_TRUE(cone.GetVertices().size() > 0);

  const auto &verts = cone.GetVertices();
  // Verify outward slanted normals on side vertices: Normals must have length 1.0 and positive Y component
  for (int j = 0; j <= segments; ++j) {
    const Vec3 &n = verts[static_cast<size_t>(j)].normal;
    const float len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
    TVK_EXPECT_NEAR(len, 1.0f, 1e-4f);
    TVK_EXPECT_TRUE(n.y > 0.0f); // slanted upward toward apex
  }
}

TVK_TEST(Extended_ProceduralPrimitives_CreateTorus) {
  const int majorSegs = 16;
  const int minorSegs = 12;
  const float majorR = 3.0f;
  const float minorR = 1.0f;
  Mesh torus = primitives::CreateTorus(majorR, minorR, majorSegs, minorSegs, {100, 150, 200, 255});

  // Vertices: (majorSegs + 1) * (minorSegs + 1)
  TVK_EXPECT_EQ(torus.GetVertices().size(), static_cast<size_t>((majorSegs + 1) * (minorSegs + 1)));
  // Indices: majorSegs * minorSegs * 6
  TVK_EXPECT_EQ(torus.GetIndices().size(), static_cast<size_t>(majorSegs * minorSegs * 6));

  const AABB aabb = torus.GetAABB();
  // Torus bounding box: [-4, 4] x [-1, 1] x [-4, 4]
  TVK_EXPECT_NEAR(aabb.min.x, -(majorR + minorR), 1e-4f);
  TVK_EXPECT_NEAR(aabb.max.x,  (majorR + minorR), 1e-4f);
  TVK_EXPECT_NEAR(aabb.min.y, -minorR, 1e-4f);
  TVK_EXPECT_NEAR(aabb.max.y,  minorR, 1e-4f);
  TVK_EXPECT_NEAR(aabb.min.z, -(majorR + minorR), 1e-4f);
  TVK_EXPECT_NEAR(aabb.max.z,  (majorR + minorR), 1e-4f);

  // All normals must be normalized unit vectors pointing radially from tube core
  for (const auto &v : torus.GetVertices()) {
    const float len = std::sqrt(v.normal.x * v.normal.x + v.normal.y * v.normal.y + v.normal.z * v.normal.z);
    TVK_EXPECT_NEAR(len, 1.0f, 1e-4f);
  }
}

TVK_TEST(Extended_ProceduralPrimitives_CreatePlane_WindingAndNormals) {
  const int gridX = 4;
  const int gridZ = 5;
  Mesh plane = primitives::CreatePlane(10.0f, 15.0f, gridX, gridZ, {255, 255, 255, 255});

  // Vertices: (gridX + 1) * (gridZ + 1) = 5 * 6 = 30
  TVK_EXPECT_EQ(plane.GetVertices().size(), 30u);
  // Indices: gridX * gridZ * 6 = 4 * 5 * 6 = 120
  TVK_EXPECT_EQ(plane.GetIndices().size(), 120u);

  // 1. All normals strictly point to +Y (0, 1, 0)
  for (const auto &v : plane.GetVertices()) {
    TVK_EXPECT_NEAR(v.normal.x, 0.0f, 1e-6f);
    TVK_EXPECT_NEAR(v.normal.y, 1.0f, 1e-6f);
    TVK_EXPECT_NEAR(v.normal.z, 0.0f, 1e-6f);
  }

  // 2. Verify quad triangle winding order:
  // For each quad (gz, gx), indices are {first, first + 1, second} and {first + 1, second + 1, second}
  // Viewed from above (+Y looking down at XZ plane), this produces CCW front-facing winding
  const auto &inds = plane.GetIndices();
  const auto &verts = plane.GetVertices();
  for (size_t quad = 0; quad < static_cast<size_t>(gridX * gridZ); ++quad) {
    const uint32_t i0 = inds[quad * 6 + 0];
    const uint32_t i1 = inds[quad * 6 + 1];
    const uint32_t i2 = inds[quad * 6 + 2];

    const uint32_t i3 = inds[quad * 6 + 3];
    const uint32_t i4 = inds[quad * 6 + 4];
    const uint32_t i5 = inds[quad * 6 + 5];

    // Check triangle 1 indices match {first, first + 1, second}
    TVK_EXPECT_EQ(i1, i0 + 1);
    TVK_EXPECT_EQ(i2, i0 + static_cast<uint32_t>(gridX + 1));

    // Check triangle 2 indices match {first + 1, second + 1, second}
    TVK_EXPECT_EQ(i3, i0 + 1);
    TVK_EXPECT_EQ(i4, i0 + static_cast<uint32_t>(gridX + 1) + 1);
    TVK_EXPECT_EQ(i5, i0 + static_cast<uint32_t>(gridX + 1));

    // Verify 2D area viewed from +Y looking down:
    // Looking down from +Y, X is screen-X (+X), Z is screen-Y (+Z)
    const Vec3 &vA = verts[i0].position;
    const Vec3 &vB = verts[i1].position;
    const Vec3 &vC = verts[i2].position;
    const float screenArea = (vB.x - vA.x) * (vC.z - vA.z) - (vC.x - vA.x) * (vB.z - vA.z);
    TVK_EXPECT_TRUE(screenArea > 0.0f); // Positive screen area -> front-facing when viewed from above (+Y)
  }
}

TVK_TEST(Extended_ProceduralPrimitives_DegenerateInputClamping) {
  // Test that negative or 0 parameters do not cause crash or divide-by-zero
  Mesh box = primitives::CreateBox(-5.0f, 0.0f, -10.0f);
  TVK_EXPECT_TRUE(box.GetVertices().size() == 24u);

  Mesh cone = primitives::CreateCone(-2.0f, 0.0f, 0); // segments clamped to 3
  TVK_EXPECT_TRUE(cone.GetVertices().size() > 0u);
  TVK_EXPECT_TRUE(cone.GetIndices().size() >= 18u);

  Mesh torus = primitives::CreateTorus(-3.0f, 0.0f, -5, 1); // segments clamped to 3
  TVK_EXPECT_TRUE(torus.GetVertices().size() >= 16u);

  Mesh plane = primitives::CreatePlane(-10.0f, 0.0f, -4, 0); // grid clamped to 1
  TVK_EXPECT_TRUE(plane.GetVertices().size() >= 4u);
}

// ============================================================================
// Suite 6: Extended_3DWireframeDebugDraw_Helpers
// ============================================================================

TVK_TEST(Extended_3DWireframeDebugDraw_DrawWireBox) {
  CommandBuffer cmd;
  cmd.Begin();

  cmd.DrawWireBox({-2.0f, -3.0f, -4.0f}, {2.0f, 3.0f, 4.0f}, {255, 0, 0, 255});
  TVK_EXPECT_EQ(cmd.GetCommands().size(), 12u);

  cmd.DrawWireBox(AABB{{-1, -1, -1}, {1, 1, 1}}, {0, 255, 0, 255});
  TVK_EXPECT_EQ(cmd.GetCommands().size(), 24u);

  cmd.End();

  for (const auto &c : cmd.GetCommands()) {
    TVK_EXPECT_EQ(c.type, CommandType::DrawLine);
  }
}

TVK_TEST(Extended_3DWireframeDebugDraw_DrawWireSphere) {
  CommandBuffer cmd;
  cmd.Begin();

  // With segments=12: 3 orthogonal great circles * 12 = 36 lines
  cmd.DrawWireSphere({0.0f, 0.0f, 0.0f}, 5.0f, {0, 0, 255, 255}, 12);
  TVK_EXPECT_EQ(cmd.GetCommands().size(), 36u);

  // Clamping: negative radius clamped to 0, segments clamped to 6 -> 3 * 6 = 18 lines
  cmd.DrawWireSphere({1, 2, 3}, -2.0f, {255, 255, 0, 255}, 2);
  TVK_EXPECT_EQ(cmd.GetCommands().size(), 36u + 18u);

  cmd.End();
}

TVK_TEST(Extended_3DWireframeDebugDraw_DrawGrid) {
  CommandBuffer cmd;
  cmd.Begin();

  // Grid size=10, step=2 -> x from -5 to +5 step 2 = 6 lines; z from -5 to +5 step 2 = 6 lines -> 12 lines
  cmd.DrawGrid(10.0f, 2.0f, {128, 128, 128, 255});
  TVK_EXPECT_EQ(cmd.GetCommands().size(), 12u);

  // Overload: DrawGrid(y_level, half_extent, grid_spacing)
  cmd.DrawGrid(5.0f, 6.0f, 3.0f, {100, 100, 100, 255});
  TVK_EXPECT_TRUE(cmd.GetCommands().size() > 12u);

  cmd.End();
}

TVK_TEST(Extended_3DWireframeDebugDraw_DrawAxes) {
  CommandBuffer cmd;
  cmd.Begin();

  // DrawAxes(origin, length): emits 3 lines: +X (Red), +Y (Green), +Z (Blue)
  cmd.DrawAxes(Vec3{1.0f, 2.0f, 3.0f}, 4.0f);
  TVK_EXPECT_EQ(cmd.GetCommands().size(), 3u);

  const auto &c = cmd.GetCommands();
  // X axis: Red
  TVK_EXPECT_EQ(c[0].color, Color::Red());
  TVK_EXPECT_NEAR(c[0].a.x, 1.0f, 1e-5f);
  TVK_EXPECT_NEAR(c[0].b.x, 5.0f, 1e-5f);

  // Y axis: Green
  TVK_EXPECT_EQ(c[1].color, Color::Green());
  TVK_EXPECT_NEAR(c[1].a.y, 2.0f, 1e-5f);
  TVK_EXPECT_NEAR(c[1].b.y, 6.0f, 1e-5f);

  // Z axis: Blue
  TVK_EXPECT_EQ(c[2].color, Color::Blue());
  TVK_EXPECT_NEAR(c[2].a.z, 3.0f, 1e-5f);
  TVK_EXPECT_NEAR(c[2].b.z, 7.0f, 1e-5f);

  // DrawAxes with transform matrix overload
  cmd.DrawAxes(Mat4Translate({10.0f, 20.0f, 30.0f}), 2.0f);
  TVK_EXPECT_EQ(cmd.GetCommands().size(), 6u);

  cmd.End();
}

TVK_TEST(Extended_3DWireframeDebugDraw_DrawFrustum) {
  CommandBuffer cmd;
  cmd.Begin();

  // With Identity matrix, unprojects NDC [-1, 1]^3 directly to 12 lines
  cmd.DrawFrustum(Mat4Identity(), {255, 255, 255, 255});
  TVK_EXPECT_EQ(cmd.GetCommands().size(), 12u);

  const auto &c = cmd.GetCommands();
  for (const auto &cmdLine : c) {
    TVK_EXPECT_EQ(cmdLine.type, CommandType::DrawLine);
    // Near face z=-1, Far face z=+1
    TVK_EXPECT_TRUE(std::abs(cmdLine.a.x) <= 1.0001f);
    TVK_EXPECT_TRUE(std::abs(cmdLine.a.y) <= 1.0001f);
    TVK_EXPECT_TRUE(std::abs(cmdLine.a.z) <= 1.0001f);
  }

  cmd.End();

  // Test recording guard: calls outside Begin/End are ignored
  const size_t count = cmd.GetCommands().size();
  cmd.DrawWireBox({-1, -1, -1}, {1, 1, 1}, {255, 255, 255, 255});
  cmd.DrawFrustum(Mat4Identity(), {255, 255, 255, 255});
  cmd.DrawAxes(Vec3{0, 0, 0}, 1.0f);
  TVK_EXPECT_EQ(cmd.GetCommands().size(), count);
}

// ============================================================================
// Suite 7: Extended_SIMD_vs_Scalar_Parity
// ============================================================================

TVK_TEST(Extended_SIMD_vs_Scalar_CoverageParity) {
  // Test 100% pixel coverage parity between engine rasterizer and reference scalar rasterizer
  const int tileSize = 32;

  // Build a lit, shaded projected triangle
  ProjectedTriangle pt{};
  pt.p0 = {4.0f, 4.0f, 0.0f};
  pt.p1 = {28.0f, 8.0f, 0.0f};
  pt.p2 = {14.0f, 28.0f, 0.0f};

  pt.ooz[0] = 1.0f / 4.0f;  // z=4
  pt.ooz[1] = 1.0f / 8.0f;  // z=8
  pt.ooz[2] = 1.0f / 12.0f; // z=12

  // Edge vectors & area
  const int32_t p0x = static_cast<int32_t>(std::round(pt.p0.x - 0.5f));
  const int32_t p0y = static_cast<int32_t>(std::round(pt.p0.y - 0.5f));
  const int32_t p1x = static_cast<int32_t>(std::round(pt.p1.x - 0.5f));
  const int32_t p1y = static_cast<int32_t>(std::round(pt.p1.y - 0.5f));
  const int32_t p2x = static_cast<int32_t>(std::round(pt.p2.x - 0.5f));
  const int32_t p2y = static_cast<int32_t>(std::round(pt.p2.y - 0.5f));

  const int64_t area64 =
      static_cast<int64_t>(p1x - p0x) * static_cast<int64_t>(p2y - p0y) -
      static_cast<int64_t>(p2x - p0x) * static_cast<int64_t>(p1y - p0y);
  TVK_EXPECT_TRUE(area64 > 0);
  pt.inv_area = 1.0f / static_cast<float>(area64);

  const float dx12 = static_cast<float>(p1y - p2y);
  const float dy12 = static_cast<float>(p2x - p1x);
  const float dx20 = static_cast<float>(p2y - p0y);
  const float dy20 = static_cast<float>(p0x - p2x);
  const float dx01 = static_cast<float>(p0y - p1y);
  const float dy01 = static_cast<float>(p1x - p0x);

  pt.dz_dx = (dx12 * pt.ooz[0] + dx20 * pt.ooz[1] + dx01 * pt.ooz[2]) * pt.inv_area;
  pt.dz_dy = (dy12 * pt.ooz[0] + dy20 * pt.ooz[1] + dy01 * pt.ooz[2]) * pt.inv_area;

  // Vertex colors: R, G, B
  pt.col_r[0] = 255.0f * pt.ooz[0]; pt.col_r[1] = 0.0f; pt.col_r[2] = 0.0f;
  pt.col_g[0] = 0.0f; pt.col_g[1] = 255.0f * pt.ooz[1]; pt.col_g[2] = 0.0f;
  pt.col_b[0] = 0.0f; pt.col_b[1] = 0.0f; pt.col_b[2] = 255.0f * pt.ooz[2];

  pt.dr_dx = (dx12 * pt.col_r[0] + dx20 * pt.col_r[1] + dx01 * pt.col_r[2]) * pt.inv_area;
  pt.dr_dy = (dy12 * pt.col_r[0] + dy20 * pt.col_r[1] + dy01 * pt.col_r[2]) * pt.inv_area;
  pt.dg_dx = (dx12 * pt.col_g[0] + dx20 * pt.col_g[1] + dx01 * pt.col_g[2]) * pt.inv_area;
  pt.dg_dy = (dy12 * pt.col_g[0] + dy20 * pt.col_g[1] + dy01 * pt.col_g[2]) * pt.inv_area;
  pt.db_dx = (dx12 * pt.col_b[0] + dx20 * pt.col_b[1] + dx01 * pt.col_b[2]) * pt.inv_area;
  pt.db_dy = (dy12 * pt.col_b[0] + dy20 * pt.col_b[1] + dy01 * pt.col_b[2]) * pt.inv_area;

  pt.depthOp = DepthOp::Less;
  pt.bounding_box = {4, 4, 28, 28};

  RasterContext ctx{};
  ctx.pix_w = tileSize;
  ctx.pix_h = tileSize;

  // Buffers for TVK engine rasterizer
  std::vector<Pixel> testFb(tileSize * tileSize, Pixel{0, 0, 0, 0});
  std::vector<float> testZb(tileSize * tileSize, 0.0f);

  // Buffers for reference scalar rasterizer
  std::vector<Pixel> refFb(tileSize * tileSize, Pixel{0, 0, 0, 0});
  std::vector<float> refZb(tileSize * tileSize, 0.0f);

  // 1. Run engine rasterizer
  Rasterizer::ProcessTileBand(ctx, pt, 0, 0, testFb.data(), testZb.data(), tileSize);

  // 2. Run reference scalar rasterizer
  ReferenceScalarRasterizeTile(pt, 0, 0, refFb.data(), refZb.data(), tileSize);

  // 3. Compare pixel-by-pixel
  int coveredPixels = 0;
  for (int y = 0; y < tileSize; ++y) {
    for (int x = 0; x < tileSize; ++x) {
      const int idx = y * tileSize + x;
      const bool testCovered = (testZb[static_cast<size_t>(idx)] > 0.0f);
      const bool refCovered = (refZb[static_cast<size_t>(idx)] > 0.0f);

      // Verify 100% exact coverage parity!
      TVK_EXPECT_EQ(testCovered, refCovered);

      if (testCovered) {
        ++coveredPixels;

        // Depth parity tolerance
        TVK_EXPECT_NEAR(testZb[static_cast<size_t>(idx)], refZb[static_cast<size_t>(idx)], 1e-4f);

        // Color channel parity tolerance (+/- 1 LSB due to float rounding)
        const int diffR = std::abs(static_cast<int>(testFb[static_cast<size_t>(idx)].r) -
                                   static_cast<int>(refFb[static_cast<size_t>(idx)].r));
        const int diffG = std::abs(static_cast<int>(testFb[static_cast<size_t>(idx)].g) -
                                   static_cast<int>(refFb[static_cast<size_t>(idx)].g));
        const int diffB = std::abs(static_cast<int>(testFb[static_cast<size_t>(idx)].b) -
                                   static_cast<int>(refFb[static_cast<size_t>(idx)].b));

        TVK_EXPECT_TRUE(diffR <= 1);
        TVK_EXPECT_TRUE(diffG <= 1);
        TVK_EXPECT_TRUE(diffB <= 1);
        TVK_EXPECT_EQ(testFb[static_cast<size_t>(idx)].a, 255);
      }
    }
  }

  // Ensure triangle actually rasterized non-trivial number of pixels
  TVK_EXPECT_TRUE(coveredPixels > 100);
}

TVK_TEST(Extended_SIMD_vs_Scalar_MultipleShapesParity) {
  // Test multiple diverse triangle geometries (acute, obtuse, right, flat-top, flat-bottom)
  const int tileSize = 32;

  struct TriGeom {
    Vec3 p0, p1, p2;
  } triangles[3] = {
      {{2.0f, 2.0f, 0.0f}, {29.0f, 2.0f, 0.0f}, {15.0f, 25.0f, 0.0f}},  // Flat-top
      {{15.0f, 2.0f, 0.0f}, {29.0f, 25.0f, 0.0f}, {2.0f, 25.0f, 0.0f}}, // Flat-bottom
      {{3.0f, 3.0f, 0.0f}, {25.0f, 3.0f, 0.0f}, {3.0f, 25.0f, 0.0f}}    // Right triangle
  };

  RasterContext ctx{};
  ctx.pix_w = tileSize;
  ctx.pix_h = tileSize;

  for (size_t i = 0; i < 3; ++i) {
    ProjectedTriangle pt{};
    pt.p0 = triangles[i].p0;
    pt.p1 = triangles[i].p1;
    pt.p2 = triangles[i].p2;
    pt.ooz[0] = 0.2f; pt.ooz[1] = 0.1f; pt.ooz[2] = 0.15f;

    const int32_t p0x = static_cast<int32_t>(std::round(pt.p0.x - 0.5f));
    const int32_t p0y = static_cast<int32_t>(std::round(pt.p0.y - 0.5f));
    const int32_t p1x = static_cast<int32_t>(std::round(pt.p1.x - 0.5f));
    const int32_t p1y = static_cast<int32_t>(std::round(pt.p1.y - 0.5f));
    const int32_t p2x = static_cast<int32_t>(std::round(pt.p2.x - 0.5f));
    const int32_t p2y = static_cast<int32_t>(std::round(pt.p2.y - 0.5f));

    const int64_t area64 =
        static_cast<int64_t>(p1x - p0x) * static_cast<int64_t>(p2y - p0y) -
        static_cast<int64_t>(p2x - p0x) * static_cast<int64_t>(p1y - p0y);
    TVK_EXPECT_TRUE(area64 > 0);
    pt.inv_area = 1.0f / static_cast<float>(area64);

    const float dx12 = static_cast<float>(p1y - p2y);
    const float dy12 = static_cast<float>(p2x - p1x);
    const float dx20 = static_cast<float>(p2y - p0y);
    const float dy20 = static_cast<float>(p0x - p2x);
    const float dx01 = static_cast<float>(p0y - p1y);
    const float dy01 = static_cast<float>(p1x - p0x);

    pt.dz_dx = (dx12 * pt.ooz[0] + dx20 * pt.ooz[1] + dx01 * pt.ooz[2]) * pt.inv_area;
    pt.dz_dy = (dy12 * pt.ooz[0] + dy20 * pt.ooz[1] + dy01 * pt.ooz[2]) * pt.inv_area;

    pt.col_r[0] = 120.0f * pt.ooz[0]; pt.col_r[1] = 180.0f * pt.ooz[1]; pt.col_r[2] = 220.0f * pt.ooz[2];
    pt.col_g[0] = 200.0f * pt.ooz[0]; pt.col_g[1] = 50.0f * pt.ooz[1];  pt.col_g[2] = 100.0f * pt.ooz[2];
    pt.col_b[0] = 80.0f * pt.ooz[0];  pt.col_b[1] = 240.0f * pt.ooz[1]; pt.col_b[2] = 150.0f * pt.ooz[2];

    pt.dr_dx = (dx12 * pt.col_r[0] + dx20 * pt.col_r[1] + dx01 * pt.col_r[2]) * pt.inv_area;
    pt.dr_dy = (dy12 * pt.col_r[0] + dy20 * pt.col_r[1] + dy01 * pt.col_r[2]) * pt.inv_area;
    pt.dg_dx = (dx12 * pt.col_g[0] + dx20 * pt.col_g[1] + dx01 * pt.col_g[2]) * pt.inv_area;
    pt.dg_dy = (dy12 * pt.col_g[0] + dy20 * pt.col_g[1] + dy01 * pt.col_g[2]) * pt.inv_area;
    pt.db_dx = (dx12 * pt.col_b[0] + dx20 * pt.col_b[1] + dx01 * pt.col_b[2]) * pt.inv_area;
    pt.db_dy = (dy12 * pt.col_b[0] + dy20 * pt.col_b[1] + dy01 * pt.col_b[2]) * pt.inv_area;

    pt.depthOp = DepthOp::Less;
    pt.bounding_box = {
        std::max(0, static_cast<int>(std::min({pt.p0.x, pt.p1.x, pt.p2.x}))),
        std::max(0, static_cast<int>(std::min({pt.p0.y, pt.p1.y, pt.p2.y}))),
        std::min(tileSize - 1, static_cast<int>(std::max({pt.p0.x, pt.p1.x, pt.p2.x})) + 1),
        std::min(tileSize - 1, static_cast<int>(std::max({pt.p0.y, pt.p1.y, pt.p2.y})) + 1)
    };

    std::vector<Pixel> testFb(tileSize * tileSize, Pixel{0, 0, 0, 0});
    std::vector<float> testZb(tileSize * tileSize, 0.0f);
    std::vector<Pixel> refFb(tileSize * tileSize, Pixel{0, 0, 0, 0});
    std::vector<float> refZb(tileSize * tileSize, 0.0f);

    Rasterizer::ProcessTileBand(ctx, pt, 0, 0, testFb.data(), testZb.data(), tileSize);
    ReferenceScalarRasterizeTile(pt, 0, 0, refFb.data(), refZb.data(), tileSize);

    for (int y = 0; y < tileSize; ++y) {
      for (int x = 0; x < tileSize; ++x) {
        const int idx = y * tileSize + x;
        const bool testCovered = (testZb[static_cast<size_t>(idx)] > 0.0f);
        const bool refCovered = (refZb[static_cast<size_t>(idx)] > 0.0f);
        TVK_EXPECT_EQ(testCovered, refCovered);
      }
    }
  }
}

TVK_TEST(Extended_SIMD_vs_Scalar_RendererSceneEndToEnd) {
  // End-to-end full scene rendering test in headless mode
  RendererDescriptor desc;
  desc.HeadlessFallback = true;
  desc.FallbackWidth = 80;
  desc.FallbackHeight = 30;
  Renderer renderer(desc);

  Camera cam;
  cam.LookAt({0.0f, 5.0f, -15.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f});
  cam.SetPerspective(60.0f, 1.0f, 100.0f);

  // Setup directional and point lights
  renderer.ClearLights();
  renderer.AddLight({0.0f, 10.0f, -5.0f}, {255, 255, 255, 255}, 1.2f);
  Light dirL;
  dirL.type = LightType::Directional;
  dirL.direction = {0.5f, -1.0f, 0.5f};
  dirL.intensity = 0.8f;
  renderer.AddLight(dirL);

  // Build meshes
  Mesh box = primitives::CreateBox(2.0f, 2.0f, 2.0f, {255, 100, 100, 255});
  Mesh sphere = primitives::CreateSphere(12, 16);
  Mesh plane = primitives::CreatePlane(20.0f, 20.0f, 4, 4, {80, 180, 80, 255});

  CommandBuffer cmd;
  cmd.Begin();

  PipelineState psoBox;
  psoBox.material.shadingModel = ShadingModel::BlinnPhong;
  psoBox.cullMode = CullMode::Back;
  cmd.BindPipeline(psoBox);
  cmd.DrawMesh(box, Mat4Translate({-3.0f, 1.0f, 0.0f}));

  PipelineState psoSphere;
  psoSphere.material.shadingModel = ShadingModel::Gouraud;
  psoSphere.cullMode = CullMode::Back;
  cmd.BindPipeline(psoSphere);
  cmd.DrawMesh(sphere, Mat4Translate({3.0f, 1.0f, 0.0f}));

  PipelineState psoPlane;
  psoPlane.material.shadingModel = ShadingModel::Toon;
  psoPlane.cullMode = CullMode::Back;
  cmd.BindPipeline(psoPlane);
  cmd.DrawMesh(plane, Mat4Identity());

  cmd.End();

  // Execute frame
  renderer.BeginFrame(cam);
  renderer.Execute(cmd);
  renderer.Present();

  // Verify render stats and presentation
  const FrameStats &stats = renderer.GetFrameStats();
  TVK_EXPECT_TRUE(stats.trianglesRasterized > 0);
  TVK_EXPECT_EQ(stats.activeLights, 2);

  const int nonBlank = CountNonBlankCells(renderer);
  TVK_EXPECT_TRUE(nonBlank > 100);
}

// Hardening advisory verification: Mat4Invert non-finite inputs
TVK_TEST(Extended_Hardening_Mat4Invert_NonFiniteDet) {
  // Test that Mat4Invert safely handles NaN and Inf without crashing or undefined behavior
  Mat4 nanMat = Mat4Identity();
  nanMat.m[0][0] = std::numeric_limits<float>::quiet_NaN();

  Mat4 outInv = Mat4Identity();
  outInv.m[0][0] = 999.0f;

  const bool inverted = Mat4Invert(nanMat, outInv);
  TVK_EXPECT_FALSE(inverted);
  // Verify outInv reset to identity
  TVK_EXPECT_NEAR(outInv.m[0][0], 1.0f, 1e-6f);
  TVK_EXPECT_NEAR(outInv.m[1][1], 1.0f, 1e-6f);
  TVK_EXPECT_NEAR(outInv.m[2][2], 1.0f, 1e-6f);
  TVK_EXPECT_NEAR(outInv.m[3][3], 1.0f, 1e-6f);
}

int main() {
  return TVK_RUN_ALL_TESTS();
}
