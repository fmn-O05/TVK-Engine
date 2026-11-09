#include "tvk/math/Math.h"
#include <algorithm>
#include <cmath>

namespace tvk {

Mat3 Mat3Identity() {
  Mat3 mat{};
  mat.m[0][0] = 1.0f;
  mat.m[1][1] = 1.0f;
  mat.m[2][2] = 1.0f;
  return mat;
}

Mat3 Mat4NormalMatrix(const Mat4 &m) {
  const float a00 = m.m[0][0], a01 = m.m[0][1], a02 = m.m[0][2];
  const float a10 = m.m[1][0], a11 = m.m[1][1], a12 = m.m[1][2];
  const float a20 = m.m[2][0], a21 = m.m[2][1], a22 = m.m[2][2];

  // Cofactor matrix C = det(A) * (A^-1)^T
  const float c00 = +(a11 * a22 - a12 * a21);
  const float c01 = -(a10 * a22 - a12 * a20);
  const float c02 = +(a10 * a21 - a11 * a20);

  const float c10 = -(a01 * a22 - a02 * a21);
  const float c11 = +(a00 * a22 - a02 * a20);
  const float c12 = -(a00 * a21 - a01 * a20);

  const float c20 = +(a01 * a12 - a02 * a11);
  const float c21 = -(a00 * a12 - a02 * a10);
  const float c22 = +(a00 * a11 - a01 * a10);

  const float det = a00 * c00 + a01 * c01 + a02 * c02;
  if (std::abs(det) <= 1e-8f) {
    Mat3 fallback{};
    for (int r = 0; r < 3; ++r)
      for (int c = 0; c < 3; ++c)
        fallback.m[r][c] = m.m[r][c];
    return fallback;
  }

  const float invDet = 1.0f / det;
  Mat3 res{};
  res.m[0][0] = c00 * invDet;
  res.m[0][1] = c01 * invDet;
  res.m[0][2] = c02 * invDet;
  res.m[1][0] = c10 * invDet;
  res.m[1][1] = c11 * invDet;
  res.m[1][2] = c12 * invDet;
  res.m[2][0] = c20 * invDet;
  res.m[2][1] = c21 * invDet;
  res.m[2][2] = c22 * invDet;
  return res;
}

Mat4 Mat4Identity() {
  Mat4 mat{};
  mat.m[0][0] = 1.0f;
  mat.m[1][1] = 1.0f;
  mat.m[2][2] = 1.0f;
  mat.m[3][3] = 1.0f;
  return mat;
}

Mat4 Mat4Multiply(const Mat4 &a, const Mat4 &b) {
  Mat4 mat{};
  for (int r = 0; r < 4; ++r) {
    for (int k = 0; k < 4; ++k) {
      float a_val = a.m[r][k];
      for (int c = 0; c < 4; ++c) {
        mat.m[r][c] += a_val * b.m[k][c];
      }
    }
  }
  return mat;
}

Mat4 Mat4Translate(Vec3 v) {
  Mat4 mat = Mat4Identity();
  mat.m[0][3] = v.x;
  mat.m[1][3] = v.y;
  mat.m[2][3] = v.z;
  return mat;
}

Mat4 Mat4Scale(Vec3 v) {
  Mat4 mat{};
  mat.m[0][0] = v.x;
  mat.m[1][1] = v.y;
  mat.m[2][2] = v.z;
  mat.m[3][3] = 1.0f;
  return mat;
}

} // namespace tvk
