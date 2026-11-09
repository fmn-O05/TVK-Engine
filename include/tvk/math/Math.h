#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace tvk {

#ifndef TVK_VEC2_VEC4_DEFINED
#define TVK_VEC2_VEC4_DEFINED
struct Vec2 {
  union {
    float x;
    float u;
  };
  union {
    float y;
    float v;
  };

  constexpr Vec2() noexcept : x(0.0f), y(0.0f) {}
  constexpr Vec2(float x_, float y_) noexcept : x(x_), y(y_) {}
};

struct Vec4 {
  float x, y, z, w;
};
#endif

struct Vec3 {
  float x, y, z;
};

struct Mat3 {
  float m[3][3];

  std::array<float, 9> ToArray() const noexcept {
    return {m[0][0], m[0][1], m[0][2],
            m[1][0], m[1][1], m[1][2],
            m[2][0], m[2][1], m[2][2]};
  }
};

struct Mat4 {
  float m[4][4];
};

inline float ClampFloat(float v, float lo, float hi) {
  return std::max(lo, std::min(v, hi));
}
inline int ClampInt(int v, int lo, int hi) {
  return std::max(lo, std::min(v, hi));
}

inline Vec2 Add2(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
inline Vec2 Sub2(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
inline Vec2 Mul2(Vec2 a, float k) { return {a.x * k, a.y * k}; }
inline float Dot2(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }

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

// Duff et al. (2017) branchless orthonormal basis construction.
// Given a direction axis (normalized internally), produces unit vectors
// outRight and outForward such that (outRight, Normalize(axis), outForward)
// form a right-handed orthonormal basis with determinant +1.
inline void BuildOrthonormalBasis(Vec3 axis, Vec3 &outRight, Vec3 &outForward) {
  const Vec3 n = Normalize(axis);
  const float sign = (n.z >= 0.0f) ? 1.0f : -1.0f;
  const float a = -1.0f / (sign + n.z);
  const float b = n.x * n.y * a;
  outRight = {1.0f + sign * n.x * n.x * a, sign * b, -sign * n.x};
  outForward = {-b, -(sign + n.y * n.y * a), n.y};
}

struct Quat {
  float x, y, z, w;
};

inline Quat QuatNormalize(Quat q) {
  float len = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
  if (len < 1e-6f)
    return {0.0f, 0.0f, 0.0f, 1.0f};
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
  Mat4 m{};
  float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
  float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
  float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
  m.m[0][0] = 1.0f - 2.0f * (yy + zz);
  m.m[0][1] = 2.0f * (xy - wz);
  m.m[0][2] = 2.0f * (xz + wy);
  m.m[0][3] = 0.0f;
  m.m[1][0] = 2.0f * (xy + wz);
  m.m[1][1] = 1.0f - 2.0f * (xx + zz);
  m.m[1][2] = 2.0f * (yz - wx);
  m.m[1][3] = 0.0f;
  m.m[2][0] = 2.0f * (xz - wy);
  m.m[2][1] = 2.0f * (yz + wx);
  m.m[2][2] = 1.0f - 2.0f * (xx + yy);
  m.m[2][3] = 0.0f;
  m.m[3][0] = 0.0f;
  m.m[3][1] = 0.0f;
  m.m[3][2] = 0.0f;
  m.m[3][3] = 1.0f;
  return m;
}

Mat3 Mat3Identity();
Mat3 Mat4NormalMatrix(const Mat4 &m);

Mat4 Mat4Identity();
Mat4 Mat4Multiply(const Mat4 &a, const Mat4 &b);
Mat4 Mat4Translate(Vec3 v);
Mat4 Mat4Scale(Vec3 v);

inline Vec3 Mat3Transform(const Mat3 &mat, Vec3 v) {
  return {
      mat.m[0][0] * v.x + mat.m[0][1] * v.y + mat.m[0][2] * v.z,
      mat.m[1][0] * v.x + mat.m[1][1] * v.y + mat.m[1][2] * v.z,
      mat.m[2][0] * v.x + mat.m[2][1] * v.y + mat.m[2][2] * v.z,
  };
}

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

inline Vec3 Barycentric(Vec3 a, Vec3 b, Vec3 c, Vec3 p) {
  float area = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
  if (std::abs(area) < 1e-5f)
    return {-1.0f, -1.0f, -1.0f};
  float wa = ((b.y - c.y) * (p.x - c.x) + (c.x - b.x) * (p.y - c.y)) / area;
  float wb = ((c.y - a.y) * (p.x - c.x) + (a.x - c.x) * (p.y - c.y)) / area;
  return {wa, wb, 1.0f - wa - wb};
}

#ifndef TVK_AABB_DEFINED
#define TVK_AABB_DEFINED
struct AABB {
  Vec3 min = {1e38f, 1e38f, 1e38f};
  Vec3 max = {-1e38f, -1e38f, -1e38f};

  bool IsValid() const { return min.x <= max.x && min.y <= max.y && min.z <= max.z; }

  Vec3 Center() const {
    return {(min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f,
            (min.z + max.z) * 0.5f};
  }
  Vec3 Extents() const {
    return {(max.x - min.x) * 0.5f, (max.y - min.y) * 0.5f,
            (max.z - min.z) * 0.5f};
  }

  void Expand(const Vec3 &p) {
    min.x = std::min(min.x, p.x);
    min.y = std::min(min.y, p.y);
    min.z = std::min(min.z, p.z);
    max.x = std::max(max.x, p.x);
    max.y = std::max(max.y, p.y);
    max.z = std::max(max.z, p.z);
  }

  bool Contains(const Vec3 &p) const {
    return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y &&
           p.z >= min.z && p.z <= max.z;
  }

  bool Intersects(const AABB &o) const {
    return (min.x <= o.max.x && max.x >= o.min.x) &&
           (min.y <= o.max.y && max.y >= o.min.y) &&
           (min.z <= o.max.z && max.z >= o.min.z);
  }
};
#endif

struct Plane {
  Vec3 normal = {0.0f, 1.0f, 0.0f};
  float d = 0.0f;

  float DistanceToPoint(const Vec3 &p) const { return Dot(normal, p) + d; }
};

inline void ExtractFrustumPlanes(const Mat4 &mvp, float planes[6][4]) {
  for (int c = 0; c < 4; ++c) {
    planes[0][c] = mvp.m[3][c] + mvp.m[0][c];
    planes[1][c] = mvp.m[3][c] - mvp.m[0][c];
    planes[2][c] = mvp.m[3][c] + mvp.m[1][c];
    planes[3][c] = mvp.m[3][c] - mvp.m[1][c];
    planes[4][c] = mvp.m[3][c] + mvp.m[2][c];
    planes[5][c] = mvp.m[3][c] - mvp.m[2][c];
  }

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

inline bool IsAABBInsideFrustum(const Vec3 &aabb_min, const Vec3 &aabb_max,
                                const float planes[6][4]) {
  for (int i = 0; i < 6; ++i) {
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

struct Frustum {
  Plane planes[6];

  static Frustum FromMatrix(const Mat4 &mvp) {
    float raw[6][4];
    ExtractFrustumPlanes(mvp, raw);
    Frustum f;
    for (int i = 0; i < 6; ++i) {
      f.planes[i].normal = {raw[i][0], raw[i][1], raw[i][2]};
      f.planes[i].d = raw[i][3];
    }
    return f;
  }

  bool ContainsPoint(const Vec3 &p) const {
    for (int i = 0; i < 6; ++i) {
      if (planes[i].DistanceToPoint(p) < 0.0f)
        return false;
    }
    return true;
  }

  bool IntersectsAABB(const AABB &box) const {
    float raw[6][4];
    for (int i = 0; i < 6; ++i) {
      raw[i][0] = planes[i].normal.x;
      raw[i][1] = planes[i].normal.y;
      raw[i][2] = planes[i].normal.z;
      raw[i][3] = planes[i].d;
    }
    return IsAABBInsideFrustum(box.min, box.max, raw);
  }
};

struct Ray {
  Vec3 origin = {0.0f, 0.0f, 0.0f};
  Vec3 direction = {0.0f, 0.0f, 1.0f};

  Vec3 At(float t) const { return Add(origin, Mul(direction, t)); }

  bool IntersectAABB(const AABB &box, float &outT) const {
    float tmin = 0.0f;
    float tmax = 1e38f;
    const float orig[3] = {origin.x, origin.y, origin.z};
    const float dir[3] = {direction.x, direction.y, direction.z};
    const float bmin[3] = {box.min.x, box.min.y, box.min.z};
    const float bmax[3] = {box.max.x, box.max.y, box.max.z};

    for (int a = 0; a < 3; ++a) {
      if (std::abs(dir[a]) < 1e-8f) {
        if (orig[a] < bmin[a] || orig[a] > bmax[a])
          return false;
      } else {
        float invD = 1.0f / dir[a];
        float t0 = (bmin[a] - orig[a]) * invD;
        float t1 = (bmax[a] - orig[a]) * invD;
        if (t0 > t1)
          std::swap(t0, t1);
        tmin = std::max(tmin, t0);
        tmax = std::min(tmax, t1);
        if (tmax < tmin)
          return false;
      }
    }
    outT = tmin;
    return true;
  }

  bool IntersectSphere(const Vec3 &center, float radius, float &outT) const {
    Vec3 oc = Sub(origin, center);
    float a = Dot(direction, direction);
    float half_b = Dot(oc, direction);
    float c = Dot(oc, oc) - radius * radius;
    float disc = half_b * half_b - a * c;
    if (disc < 0.0f || a <= 1e-8f)
      return false;
    float sqrtd = std::sqrt(disc);
    float root = (-half_b - sqrtd) / a;
    if (root < 0.0f) {
      root = (-half_b + sqrtd) / a;
      if (root < 0.0f)
        return false;
    }
    outT = root;
    return true;
  }
};

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
