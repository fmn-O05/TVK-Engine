#pragma once
#include <vector>
#include "tvk/TVK_Math.h"
#include "tvk/TVK_Mesh.h"

namespace tvk {

    // =========================================================================
    // Pipeline sub-states
    // =========================================================================

    enum class CullMode { None, Back, Front };
    enum class DepthOp  { Less, Always };

    /// Per-material shading coefficients embedded in PipelineState.
    struct MaterialState {
        float ambient   = 0.15f;  ///< Ambient light contribution  [0, 1]
        float diffuse   = 0.85f;  ///< Lambertian diffuse          [0, 1]
        float specular  = 0.40f;  ///< Specular (Blinn-Phong)      [0, 1]
        float shininess = 32.0f;  ///< Specular exponent           (8–512 typical)
    };

    struct PipelineState {
        CullMode      cullMode = CullMode::Back;
        DepthOp       depthOp  = DepthOp::Less;
        MaterialState material;
    };

    // =========================================================================
    // Command types
    // =========================================================================

    enum class CommandType {
        DrawMesh,
        DrawSphere,
        DrawCylinder,
        DrawCapsule,
        DrawGround,
        DrawLine,       ///< Wire/debug line segment
    };

    struct RenderCommand {
        CommandType  type;
        PipelineState pso;
        // Mesh draw data
        const Mesh* mesh      = nullptr;
        Mat4        transform {};
        // Primitive / line draw data
        Vec3        a{}, b{};
        float       radius    = 0.0f;
        uint8_t     shade     = 0;
        // Line / primitive color override (0 alpha = use shade-based colour)
        Color       color     = {255, 255, 255, 255};
    };

    // =========================================================================
    // CommandBuffer
    // =========================================================================

    class CommandBuffer {
    public:
        void Begin();
        void End();

        void BindPipeline(const PipelineState& pso);

        void DrawMesh    (const Mesh& mesh, const Mat4& transform);
        void DrawSphere  (const Vec3& center, float radius, uint8_t shade);
        void DrawSphere  (const Vec3& center, float radius, Color color);
        void DrawCylinder(const Vec3& a, const Vec3& b, float radius, uint8_t shade);
        void DrawCylinder(const Vec3& a, const Vec3& b, float radius, Color color); ///< Full RGB
        void DrawCapsule (const Vec3& a, const Vec3& b, float radius, uint8_t shade);
        void DrawCapsule (const Vec3& a, const Vec3& b, float radius, Color color); ///< Full RGB
        void DrawGround  (float y_level, float half_extent, uint8_t shade);
        void DrawGround  (float y_level, float half_extent, Color color);            ///< Full RGB
        void DrawLine    (const Vec3& a, const Vec3& b, Color color = {255,255,255,255});

        const std::vector<RenderCommand>& GetCommands() const { return m_Commands; }
        void Clear() { m_Commands.clear(); }

    private:
        std::vector<RenderCommand> m_Commands;
        PipelineState              m_CurrentPipeline;
        bool                       m_IsRecording = false;
    };

} // namespace tvk