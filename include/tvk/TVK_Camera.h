#pragma once
#include "tvk/TVK_Math.h"

namespace tvk {

    class Camera {
    public:
        enum class ProjectionType { Perspective, Orthographic };

        Camera();

        // === Legacy Orbit API (preserved for compatibility) ===
        void Orbit(float yaw, float pitch);
        void Zoom(float distance);
        void UpdateCache(); // Recomputes cached trig values for orbit mode

        // Orbit accessors
        float GetYaw() const { return m_Yaw; }
        float GetPitch() const { return m_Pitch; }
        float GetDistance() const { return m_Distance; }

        // === New General-Purpose API ===
        void SetPerspective(float fov_deg, float near_plane, float far_plane);
        void SetOrthographic(float size, float near_plane, float far_plane);
        void SetTransform(const Mat4& transform);
        void LookAt(Vec3 eye, Vec3 target, Vec3 up);

        // Projection accessors
        ProjectionType GetProjectionType() const { return m_ProjType; }
        float GetFOV() const { return m_FOV; }
        float GetNear() const { return m_Near; }
        float GetFar() const { return m_Far; }

        // Pipeline transformation
        Mat4 GetViewMatrix() const;
        Mat4 GetProjectionMatrix(float aspect_ratio) const;
        bool WorldToCamera(const Vec3& point, float& out_x, float& out_y, float& out_z) const;

    private:
        // Orbit parameters
        float m_Yaw;
        float m_Pitch;
        float m_Distance;

        // Cached trig (orbit mode)
        float m_CosYaw;
        float m_SinYaw;
        float m_CosPitch;
        float m_SinPitch;

        // Projection parameters
        ProjectionType m_ProjType;
        float m_FOV;        // degrees (perspective)
        float m_OrthoSize;  // half-height (orthographic)
        float m_Near;
        float m_Far;

        // Custom transform (when using SetTransform/LookAt)
        Mat4 m_CustomView;
        bool m_UseCustomView;
    };

} // namespace tvk
