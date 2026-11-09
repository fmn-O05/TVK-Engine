#include "tvk/renderer/Camera.h"
#include "tvk/renderer/Rasterizer.h"
#include <cmath>

namespace tvk {

Camera::Camera()
    : m_Yaw(0.40f), m_Pitch(0.20f), m_Distance(128.0f),
      m_ProjType(ProjectionType::Perspective), m_FOV(70.0f)
      ,
      m_OrthoSize(50.0f), m_Near(NEAR_PLANE), m_Far(1000.0f), m_CustomView{},
      m_UseCustomView(false) {
  UpdateCache();
}

void Camera::Orbit(float yaw, float pitch) {
  m_Yaw = yaw;
  m_Pitch = ClampFloat(pitch, -0.70f, 0.82f);
  m_UseCustomView = false;
}

void Camera::Zoom(float distance) {
  m_Distance = ClampFloat(distance, 40.0f, 320.0f);
}

void Camera::UpdateCache() {
  m_CosYaw = std::cos(m_Yaw);
  m_SinYaw = std::sin(m_Yaw);
  m_CosPitch = std::cos(m_Pitch);
  m_SinPitch = std::sin(m_Pitch);
}

void Camera::SetPerspective(float fov_deg, float near_plane, float far_plane) {
  m_ProjType = ProjectionType::Perspective;
  m_FOV = fov_deg;
  m_Near = near_plane;
  m_Far = far_plane;
}

void Camera::SetOrthographic(float size, float near_plane, float far_plane) {
  m_ProjType = ProjectionType::Orthographic;
  m_OrthoSize = size;
  m_Near = near_plane;
  m_Far = far_plane;
}

void Camera::SetTransform(const Mat4 &transform) {
  m_CustomView = transform;
  m_UseCustomView = true;
}

void Camera::LookAt(Vec3 eye, Vec3 target, Vec3 up) {

  Vec3 f = Normalize(Sub(target, eye));
  Vec3 r = Normalize(Cross(f, up));
  Vec3 u = Cross(r, f);

  Mat4 v;
  v.m[0][0] = r.x;
  v.m[0][1] = r.y;
  v.m[0][2] = r.z;
  v.m[0][3] = -Dot(r, eye);
  v.m[1][0] = u.x;
  v.m[1][1] = u.y;
  v.m[1][2] = u.z;
  v.m[1][3] = -Dot(u, eye);
  v.m[2][0] = -f.x;
  v.m[2][1] = -f.y;
  v.m[2][2] = -f.z;
  v.m[2][3] = Dot(f, eye);
  v.m[3][0] = 0.0f;
  v.m[3][1] = 0.0f;
  v.m[3][2] = 0.0f;
  v.m[3][3] = 1.0f;

  m_CustomView = v;
  m_UseCustomView = true;
}

Mat4 Camera::GetViewMatrix() const {
  if (m_UseCustomView) {
    return m_CustomView;
  }

  Mat4 v;
  v.m[0][0] = m_CosYaw;
  v.m[0][1] = 0.0f;
  v.m[0][2] = m_SinYaw;
  v.m[0][3] = 0.0f;

  v.m[1][0] = m_SinPitch * m_SinYaw;
  v.m[1][1] = m_CosPitch;
  v.m[1][2] = -m_SinPitch * m_CosYaw;
  v.m[1][3] = 0.0f;

  v.m[2][0] = -m_CosPitch * m_SinYaw;
  v.m[2][1] = m_SinPitch;
  v.m[2][2] = m_CosPitch * m_CosYaw;
  v.m[2][3] = m_Distance;

  v.m[3][0] = 0.0f;
  v.m[3][1] = 0.0f;
  v.m[3][2] = 0.0f;
  v.m[3][3] = 1.0f;
  return v;
}

Mat4 Camera::GetProjectionMatrix(float aspect_ratio) const {
  Mat4 p{};

  if (m_ProjType == ProjectionType::Perspective) {
    float fov_rad = m_FOV * 3.14159265358979f / 180.0f;
    float tan_half_fov = std::tan(fov_rad * 0.5f);

    if (std::abs(tan_half_fov) < 1e-6f || std::abs(aspect_ratio) < 1e-6f) {
      return Mat4Identity();
    }

    float f = 1.0f / tan_half_fov;

    float range_inv = 1.0f / (m_Far - m_Near);

    p.m[0][0] = f / aspect_ratio;
    p.m[1][1] = f;
    p.m[2][2] =  (m_Far + m_Near) * range_inv;
    p.m[2][3] = -2.0f * m_Far * m_Near * range_inv;
    p.m[3][2] = 1.0f;

  } else {

    float half_h = m_OrthoSize;
    float half_w = half_h * aspect_ratio;
    float range_inv = 1.0f / (m_Near - m_Far);

    p.m[0][0] = 1.0f / half_w;
    p.m[1][1] = 1.0f / half_h;
    p.m[2][2] = 2.0f * range_inv;
    p.m[2][3] = (m_Far + m_Near) * range_inv;
    p.m[3][3] = 1.0f;
  }

  return p;
}

bool Camera::WorldToCamera(const Vec3 &point, float &out_x, float &out_y,
                           float &out_z) const {
  if (m_UseCustomView) {

    out_x = m_CustomView.m[0][0] * point.x + m_CustomView.m[0][1] * point.y +
            m_CustomView.m[0][2] * point.z + m_CustomView.m[0][3];
    out_y = m_CustomView.m[1][0] * point.x + m_CustomView.m[1][1] * point.y +
            m_CustomView.m[1][2] * point.z + m_CustomView.m[1][3];
    out_z = m_CustomView.m[2][0] * point.x + m_CustomView.m[2][1] * point.y +
            m_CustomView.m[2][2] * point.z + m_CustomView.m[2][3];
    return out_z > m_Near;
  }

  const float x1 = m_CosYaw * point.x + m_SinYaw * point.z;
  const float z1 = -m_SinYaw * point.x + m_CosYaw * point.z;

  const float y2 = m_CosPitch * point.y - m_SinPitch * z1;
  const float z2 = m_SinPitch * point.y + m_CosPitch * z1 + m_Distance;

  out_x = x1;
  out_y = y2;
  out_z = z2;

  return z2 > m_Near;
}

}
