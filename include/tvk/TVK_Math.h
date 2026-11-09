#pragma once
#include <algorithm>
#include <cmath>

namespace tvk {

struct Vec3 {
  float x, y, z;
};

struct Mat4 {
  float m[4][4]; // Row-major: m[row][col]
};

// =========================================================================
// Scalar utilities
// =========================================================================

inline float ClampFloat(float v, float lo, float hi) {
  return std::max(lo, std::min(v, hi));
}
inline int ClampInt(int v, int lo, int hi) {
  return std::max(lo, std::min(v, hi));
}

// =========================================================================
// Vec3 math
// =========================================================================

inline Vec3 Add(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 Sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 Mul(Vec3 a, float k) { return {a.x * k, a.y * k, a.z * k}; }

inline float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline float LengthSq(Vec3 v) { return Dot(v, v); }
inline float Length(Vec3 v) { return std::sqrt(LengthSq(v)); }

inline Vec3 Cross(Vec3 a, Vec3 b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

inline Vec3 Normalize(Vec3 v) {
  const float len = Length(v);
  if (len <= 1e-6f)
    return {0.0f, 1.0f, 0.0f};
  const float inv = 1.0f / len;
  return {v.x * inv, v.y * inv, v.z * inv};
}

// =========================================================================
// Quaternion
// =========================================================================

struct Quat {
  float x, y, z, w;
};

inline Quat QuatNormalize(Quat q) {
  float len = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
  if (len < 1e-6f)
    return {0, 0, 0, 1};
  float inv = 1.0f / len;
  return {q.x * inv, q.y * inv, q.z * inv, q.w * inv};
}

inline Quat QuatIntegrate(Quat q, Vec3 w, float dt) {
  float hdt = dt * 0.5f;
  Quat s;
  s.x = hdt * (w.x * q.w + w.y * q.z - w.z * q.y);
  s.y = hdt * (w.y * q.w + w.z * q.x - w.x * q.z);
  s.z = hdt * (w.z * q.w + w.x * q.y - w.y * q.x);
  s.w = -hdt * (w.x * q.x + w.y * q.y + w.z * q.z);
  return QuatNormalize({q.x + s.x, q.y + s.y, q.z + s.z, q.w + s.w});
}

inline Mat4 QuatToMat4(Quat q) {
  Mat4 m;
  float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
  float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
  float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
  m.m[0][0] = 1 - 2 * (yy + zz);
  m.m[0][1] = 2 * (xy - wz);
  m.m[0][2] = 2 * (xz + wy);
  m.m[0][3] = 0;
  m.m[1][0] = 2 * (xy + wz);
  m.m[1][1] = 1 - 2 * (xx + zz);
  m.m[1][2] = 2 * (yz - wx);
  m.m[1][3] = 0;
  m.m[2][0] = 2 * (xz - wy);
  m.m[2][1] = 2 * (yz + wx);
  m.m[2][2] = 1 - 2 * (xx + yy);
  m.m[2][3] = 0;
  m.m[3][0] = 0;
  m.m[3][1] = 0;
  m.m[3][2] = 0;
  m.m[3][3] = 1;
  return m;
}

// =========================================================================
// Matrix math (declarations – implemented in TVK_Math.cpp)
// =========================================================================

Mat4 Mat4Identity();
Mat4 Mat4Multiply(const Mat4 &a, const Mat4 &b);
Mat4 Mat4Translate(Vec3 v);
Mat4 Mat4Scale(Vec3 v);

// Affine transform of a point (includes w-divide for perspective)
inline Vec3 VectorTransform(Vec3 v, const Mat4 &mat) {
  float x =
      mat.m[0][0] * v.x + mat.m[0][1] * v.y + mat.m[0][2] * v.z + mat.m[0][3];
  float y =
      mat.m[1][0] * v.x + mat.m[1][1] * v.y + mat.m[1][2] * v.z + mat.m[1][3];
  float z =
      mat.m[2][0] * v.x + mat.m[2][1] * v.y + mat.m[2][2] * v.z + mat.m[2][3];
  float w =
      mat.m[3][0] * v.x + mat.m[3][1] * v.y + mat.m[3][2] * v.z + mat.m[3][3];
  if (std::abs(w) > 1e-6f && w != 1.0f)
    return {x / w, y / w, z / w};
  return {x, y, z};
}

// Barycentric coordinates of point P with respect to triangle ABC
inline Vec3 Barycentric(Vec3 a, Vec3 b, Vec3 c, Vec3 p) {
  float area = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
  if (std::abs(area) < 1e-5f)
    return {-1, -1, -1};
  float wa = ((b.y - c.y) * (p.x - c.x) + (c.x - b.x) * (p.y - c.y)) / area;
  float wb = ((c.y - a.y) * (p.x - c.x) + (a.x - c.x) * (p.y - c.y)) / area;
  return {wa, wb, 1.0f - wa - wb};
}

// =========================================================================
// Frustum utilities (Gribb-Hartmann extraction)
// =========================================================================

/// Extract 6 frustum planes from a combined MVP matrix.
/// planes[i] = {a,b,c,d}: a*x + b*y + c*z + d >= 0 is INSIDE.
/// Plane order: Left, Right, Bottom, Top, Near, Far.
inline void ExtractFrustumPlanes(const Mat4 &mvp, float planes[6][4]) {
  // Gribb-Hartmann extraction adapted for row-major layout.
  // NOTE: This renderer uses a LEFT-HANDED projection (+Z forward, p.m[3][2]=+1,
  // range_inv = 1/(near-far) < 0). In this convention the near/far sign is
  // flipped versus standard right-handed OpenGL, so:
  //   Near plane = row3 - row2  (NOT row3 + row2)
  //   Far  plane = row3 + row2  (NOT row3 - row2)
  // All lateral planes (Left/Right/Bottom/Top) are unaffected.
  for (int c = 0; c < 4; ++c) {
    planes[0][c] = mvp.m[3][c] + mvp.m[0][c]; // Left
    planes[1][c] = mvp.m[3][c] - mvp.m[0][c]; // Right
    planes[2][c] = mvp.m[3][c] + mvp.m[1][c]; // Bottom
    planes[3][c] = mvp.m[3][c] - mvp.m[1][c]; // Top
    planes[4][c] = mvp.m[3][c] + mvp.m[2][c]; // Near
    planes[5][c] = mvp.m[3][c] - mvp.m[2][c]; // Far
  }
  // Normalise plane normals so signed-distance is in world units
  for (int i = 0; i < 6; ++i) {
    float len =
        std::sqrt(planes[i][0] * planes[i][0] + planes[i][1] * planes[i][1] +
                  planes[i][2] * planes[i][2]);
    if (len > 1e-6f) {
      float inv = 1.0f / len;
      planes[i][0] *= inv;
      planes[i][1] *= inv;
      planes[i][2] *= inv;
      planes[i][3] *= inv;
    }
  }
}

/// AABB vs. frustum test (conservative – never false-negatives).
/// Returns false only when the box is provably ENTIRELY outside one plane.
inline bool IsAABBInsideFrustum(const Vec3 &aabb_min, const Vec3 &aabb_max,
                                const float planes[6][4]) {
  for (int i = 0; i < 6; ++i) {
    // P-vertex: most positive point along this plane's normal
    float px = (planes[i][0] >= 0.0f) ? aabb_max.x : aabb_min.x;
    float py = (planes[i][1] >= 0.0f) ? aabb_max.y : aabb_min.y;
    float pz = (planes[i][2] >= 0.0f) ? aabb_max.z : aabb_min.z;
    if (planes[i][0] * px + planes[i][1] * py + planes[i][2] * pz +
            planes[i][3] <
        0.0f)
      return false;
  }
  return true;
}

/// Transform an object-space AABB by a 4×4 matrix (conservative result).
/// Transforms all 8 corners and recomputes the enclosing AABB.
inline void TransformAABB(const Vec3 &in_min, const Vec3 &in_max,
                          const Mat4 &mat, Vec3 &out_min, Vec3 &out_max) {
  out_min = {1e38f, 1e38f, 1e38f};
  out_max = {-1e38f, -1e38f, -1e38f};
  const float xs[2] = {in_min.x, in_max.x};
  const float ys[2] = {in_min.y, in_max.y};
  const float zs[2] = {in_min.z, in_max.z};
  for (int xi = 0; xi < 2; ++xi)
    for (int yi = 0; yi < 2; ++yi)
      for (int zi = 0; zi < 2; ++zi) {
        Vec3 c = VectorTransform({xs[xi], ys[yi], zs[zi]}, mat);
        out_min.x = std::min(out_min.x, c.x);
        out_max.x = std::max(out_max.x, c.x);
        out_min.y = std::min(out_min.y, c.y);
        out_max.y = std::max(out_max.y, c.y);
        out_min.z = std::min(out_min.z, c.z);
        out_max.z = std::max(out_max.z, c.z);
      }
}

} // namespace tvk
