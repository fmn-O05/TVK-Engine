#include "TestHarness.h"
#include "tvk/TVK.h"
#include <cmath>
#include <vector>

using namespace tvk;

TVK_TEST(Vec2_UnionAliasingAndOps) {
  Vec2 v(0.25f, 0.75f);
  TVK_EXPECT_NEAR(v.x, 0.25f, 1e-6f);
  TVK_EXPECT_NEAR(v.u, 0.25f, 1e-6f);
  TVK_EXPECT_NEAR(v.y, 0.75f, 1e-6f);
  TVK_EXPECT_NEAR(v.v, 0.75f, 1e-6f);

  v.u = 1.5f;
  v.v = -2.5f;
  TVK_EXPECT_NEAR(v.x, 1.5f, 1e-6f);
  TVK_EXPECT_NEAR(v.y, -2.5f, 1e-6f);
}

TVK_TEST(Mat4NormalMatrix_NonUniformScaleAndSingularFallback) {
  // Pure rotation preserves normals
  Quat q = QuatNormalize({0.2f, -0.5f, 0.3f, 0.8f});
  Mat4 rot = QuatToMat4(q);
  Mat3 nmRot = Mat4NormalMatrix(rot);
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 3; ++c) {
      TVK_EXPECT_NEAR(nmRot.m[r][c], rot.m[r][c], 1e-5f);
    }
  }

  // Non-uniform scaling diag(2, 4, 8) -> inverse-transpose is diag(0.5, 0.25, 0.125)
  Mat4 scale = Mat4Scale({2.0f, 4.0f, 8.0f});
  Mat3 nmScale = Mat4NormalMatrix(scale);
  TVK_EXPECT_NEAR(nmScale.m[0][0], 0.5f, 1e-6f);
  TVK_EXPECT_NEAR(nmScale.m[1][1], 0.25f, 1e-6f);
  TVK_EXPECT_NEAR(nmScale.m[2][2], 0.125f, 1e-6f);
  TVK_EXPECT_NEAR(nmScale.m[0][1], 0.0f, 1e-6f);

  // Verify normal perpendicularity to a tangent under non-uniform scale (B-07)
  Vec3 tangent = {1.0f, 1.0f, 0.0f};
  Vec3 normal = Normalize({-1.0f, 1.0f, 0.0f});
  Vec3 scaledTangent = VectorTransform(tangent, scale);
  Vec3 transformedNormal = Normalize(Mat3Transform(nmScale, normal));
  TVK_EXPECT_NEAR(Dot(scaledTangent, transformedNormal), 0.0f, 1e-5f);

  // Singular matrix fallback does not produce NaN or Inf
  Mat4 singular{};
  Mat3 nmSing = Mat4NormalMatrix(singular);
  for (float v : nmSing.ToArray()) {
    TVK_EXPECT_FALSE(std::isnan(v));
    TVK_EXPECT_FALSE(std::isinf(v));
  }
}

TVK_TEST(BuildOrthonormalBasis_AllDirectionsAndRightHandedness) {
  const Vec3 axes[] = {
      {1.0f, 0.0f, 0.0f},  {-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f},
      {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f},  {0.0f, 0.0f, -1.0f},
      {1.0f, 2.0f, 3.0f},  {-3.0f, 0.5f, -1.2f}};

  for (const Vec3 &rawAxis : axes) {
    Vec3 up = Normalize(rawAxis);
    Vec3 right{}, forward{};
    BuildOrthonormalBasis(up, right, forward);

    TVK_EXPECT_NEAR(Length(right), 1.0f, 1e-5f);
    TVK_EXPECT_NEAR(Length(forward), 1.0f, 1e-5f);
    TVK_EXPECT_NEAR(Dot(up, right), 0.0f, 1e-5f);
    TVK_EXPECT_NEAR(Dot(up, forward), 0.0f, 1e-5f);
    TVK_EXPECT_NEAR(Dot(right, forward), 0.0f, 1e-5f);

    // Determinant of [right, up, forward] must be +1 so triangle winding is preserved
    Vec3 crossRU = Cross(right, up);
    TVK_EXPECT_NEAR(Dot(crossRU, forward), 1.0f, 1e-5f);
  }
}

TVK_TEST(AABB_Frustum_And_Ray_Intersections) {
  AABB box;
  box.Expand({-2.0f, -3.0f, 10.0f});
  box.Expand({2.0f, 3.0f, 14.0f});
  TVK_EXPECT_TRUE(box.IsValid());
  TVK_EXPECT_TRUE(box.Contains({0.0f, 0.0f, 12.0f}));
  TVK_EXPECT_FALSE(box.Contains({0.0f, 0.0f, 5.0f}));

  Ray ray{{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
  float tHit = 0.0f;
  TVK_EXPECT_TRUE(ray.IntersectAABB(box, tHit));
  TVK_EXPECT_NEAR(tHit, 10.0f, 1e-5f);

  float tSphere = 0.0f;
  TVK_EXPECT_TRUE(ray.IntersectSphere({0.0f, 0.0f, 12.0f}, 2.0f, tSphere));
  TVK_EXPECT_NEAR(tSphere, 10.0f, 1e-5f);

  Camera cam;
  cam.LookAt({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 10.0f}, {0.0f, 1.0f, 0.0f});
  cam.SetPerspective(70.0f, 1.0f, 100.0f);
  Mat4 mvp = Mat4Multiply(cam.GetProjectionMatrix(1.5f), cam.GetViewMatrix());
  Frustum frustum = Frustum::FromMatrix(mvp);
  TVK_EXPECT_TRUE(frustum.ContainsPoint({0.0f, 0.0f, 12.0f}));
  TVK_EXPECT_FALSE(frustum.ContainsPoint({0.0f, 0.0f, -5.0f}));
  TVK_EXPECT_TRUE(frustum.IntersectsAABB(box));
}

TVK_TEST(SIMD_ZeroNormalMaskingAndScalarParity) {
  // B-20/21: Zero-length vector must not produce NaN
  Vec3 zeroNorm = simd::NormalizeFast({0.0f, 0.0f, 0.0f});
  TVK_EXPECT_FALSE(std::isnan(zeroNorm.x));
  TVK_EXPECT_FALSE(std::isnan(zeroNorm.y));
  TVK_EXPECT_FALSE(std::isnan(zeroNorm.z));
  TVK_EXPECT_NEAR(Length(zeroNorm), 0.0f, 1e-6f);

  Vec3 unitNorm = simd::NormalizeFast({3.0f, 4.0f, 0.0f});
  TVK_EXPECT_NEAR(unitNorm.x, 0.6f, 1e-3f);
  TVK_EXPECT_NEAR(unitNorm.y, 0.8f, 1e-3f);
  TVK_EXPECT_NEAR(unitNorm.z, 0.0f, 1e-3f);

  // Compare AVX2 batch transform against Scalar batch transform (19 vertices = 2x8 + 3 tail)
  const uint32_t count = 19;
  std::vector<float> vx(count), vy(count), vz(count);
  std::vector<float> nx(count), ny(count), nz(count);
  for (uint32_t i = 0; i < count; ++i) {
    vx[i] = static_cast<float>(i) * 1.5f - 5.0f;
    vy[i] = static_cast<float>(i) * -0.7f + 2.0f;
    vz[i] = static_cast<float>(i) * 2.1f + 1.0f;
    if (i == 3 || i == 17) {
      // Inject zero-length normals in both SIMD body and scalar tail
      nx[i] = 0.0f;
      ny[i] = 0.0f;
      nz[i] = 0.0f;
    } else {
      nx[i] = 1.0f + static_cast<float>(i) * 0.2f;
      ny[i] = -2.0f + static_cast<float>(i) * 0.3f;
      nz[i] = 0.5f + static_cast<float>(i) * 0.1f;
    }
  }

  std::vector<float> ox1(count), oy1(count), oz1(count), onx1(count),
      ony1(count), onz1(count);
  std::vector<float> ox2(count), oy2(count), oz2(count), onx2(count),
      ony2(count), onz2(count);

  Mat4 M = Mat4Multiply(Mat4Translate({4.0f, -3.0f, 12.0f}),
                        Mat4Scale({2.0f, 0.5f, 3.0f}));
  auto it = Mat4NormalMatrix(M).ToArray();

  simd::VertexBatchSoA inBatch{vx.data(), vy.data(), vz.data(),
                               nx.data(), ny.data(), nz.data(), count};
  simd::VertexBatchSoA outSimd{ox1.data(),  oy1.data(),  oz1.data(),
                               onx1.data(), ony1.data(), onz1.data(), count};
  simd::VertexBatchSoA outScalar{ox2.data(),  oy2.data(),  oz2.data(),
                                 onx2.data(), ony2.data(), onz2.data(), count};

  simd::TransformVerticesAVX2(inBatch, outSimd, M);
  simd::TransformNormalsAVX2(inBatch, outSimd, it);

  simd::TransformVerticesScalar(inBatch, outScalar, M);
  simd::TransformNormalsScalar(inBatch, outScalar, it);

  for (uint32_t i = 0; i < count; ++i) {
    TVK_EXPECT_NEAR(ox1[i], ox2[i], 1e-4f);
    TVK_EXPECT_NEAR(oy1[i], oy2[i], 1e-4f);
    TVK_EXPECT_NEAR(oz1[i], oz2[i], 1e-4f);

    TVK_EXPECT_FALSE(std::isnan(onx1[i]));
    TVK_EXPECT_FALSE(std::isnan(ony1[i]));
    TVK_EXPECT_FALSE(std::isnan(onz1[i]));

    TVK_EXPECT_NEAR(onx1[i], onx2[i], 1e-3f);
    TVK_EXPECT_NEAR(ony1[i], ony2[i], 1e-3f);
    TVK_EXPECT_NEAR(onz1[i], onz2[i], 1e-3f);
  }
}

TVK_TEST(Mat4Invert_AnalyticalAdjugateAndSingularities) {
  // 1. Identity matrix inversion
  Mat4 I = Mat4Identity();
  Mat4 invI{};
  TVK_EXPECT_TRUE(Mat4Invert(I, invI));
  for (int r = 0; r < 4; ++r) {
    for (int c = 0; c < 4; ++c) {
      float expected = (r == c) ? 1.0f : 0.0f;
      TVK_EXPECT_NEAR(invI.m[r][c], expected, 1e-6f);
    }
  }

  // 2. Combined affine matrix (Translate * Rotate * Scale)
  Mat4 T = Mat4Translate({3.5f, -7.2f, 15.0f});
  Mat4 S = Mat4Scale({2.0f, 0.5f, 4.0f});
  Mat4 R = QuatToMat4(QuatNormalize({0.3f, -0.2f, 0.5f, 0.8f}));
  Mat4 M = Mat4Multiply(T, Mat4Multiply(R, S));
  Mat4 invM{};
  TVK_EXPECT_TRUE(Mat4Invert(M, invM));

  // Verify M * invM == Identity
  Mat4 prod = Mat4Multiply(M, invM);
  for (int r = 0; r < 4; ++r) {
    for (int c = 0; c < 4; ++c) {
      float expected = (r == c) ? 1.0f : 0.0f;
      TVK_EXPECT_NEAR(prod.m[r][c], expected, 1e-4f);
    }
  }

  // Verify invM * M == Identity
  Mat4 prodRev = Mat4Multiply(invM, M);
  for (int r = 0; r < 4; ++r) {
    for (int c = 0; c < 4; ++c) {
      float expected = (r == c) ? 1.0f : 0.0f;
      TVK_EXPECT_NEAR(prodRev.m[r][c], expected, 1e-4f);
    }
  }

  // 3. Singularity: Zero matrix det = 0 -> returns false, sets outInv = Identity
  Mat4 zeroMat{};
  Mat4 outZero{};
  TVK_EXPECT_FALSE(Mat4Invert(zeroMat, outZero));
  for (int r = 0; r < 4; ++r) {
    for (int c = 0; c < 4; ++c) {
      float expected = (r == c) ? 1.0f : 0.0f;
      TVK_EXPECT_NEAR(outZero.m[r][c], expected, 1e-6f);
    }
  }

  // 4. Near-singularity: det <= 1e-8 -> returns false, sets outInv = Identity
  Mat4 nearSing = Mat4Identity();
  nearSing.m[0][0] = 1e-5f;
  nearSing.m[1][1] = 1e-5f; // det = 1e-10 <= 1e-8
  Mat4 outNearSing{};
  TVK_EXPECT_FALSE(Mat4Invert(nearSing, outNearSing));
  for (int r = 0; r < 4; ++r) {
    for (int c = 0; c < 4; ++c) {
      float expected = (r == c) ? 1.0f : 0.0f;
      TVK_EXPECT_NEAR(outNearSing.m[r][c], expected, 1e-6f);
    }
  }

  // 5. In-place aliasing: Mat4Invert(M, M)
  Mat4 M_alias = M;
  TVK_EXPECT_TRUE(Mat4Invert(M_alias, M_alias));
  Mat4 prodAlias = Mat4Multiply(M, M_alias);
  for (int r = 0; r < 4; ++r) {
    for (int c = 0; c < 4; ++c) {
      float expected = (r == c) ? 1.0f : 0.0f;
      TVK_EXPECT_NEAR(prodAlias.m[r][c], expected, 1e-4f);
    }
  }
}

int main() { return TVK_RUN_ALL_TESTS(); }
