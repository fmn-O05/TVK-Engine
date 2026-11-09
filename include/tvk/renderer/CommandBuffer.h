#pragma once
#include <vector>
#include "tvk/math/Math.h"
#include "tvk/renderer/Mesh.h"

namespace tvk {

    enum class CullMode { None, Back, Front };
    enum class DepthOp  { Less, Always };

    struct MaterialState {
        float ambient   = 0.15f;
        float diffuse   = 0.85f;
        float specular  = 0.40f;
        float shininess = 32.0f;
    };

    struct PipelineState {
        CullMode      cullMode = CullMode::Back;
        DepthOp       depthOp  = DepthOp::Less;
        MaterialState material;
    };

    enum class CommandType {
        DrawMesh,
        DrawSphere,
        DrawCylinder,
        DrawCapsule,
        DrawGround,
        DrawLine,
    };

    struct RenderCommand {
        CommandType  type;
        PipelineState pso;

        const Mesh* mesh      = nullptr;
        Mat4        transform {};

        Vec3        a{}, b{};
        float       radius    = 0.0f;
        uint8_t     shade     = 0;

        Color       color     = {255, 255, 255, 255};
    };

    class CommandBuffer {
    public:
        void Begin();
        void End();

        void BindPipeline(const PipelineState& pso);

        void DrawMesh    (const Mesh& mesh, const Mat4& transform);
        void DrawSphere  (const Vec3& center, float radius, uint8_t shade);
        void DrawSphere  (const Vec3& center, float radius, Color color);
        void DrawCylinder(const Vec3& a, const Vec3& b, float radius, uint8_t shade);
        void DrawCylinder(const Vec3& a, const Vec3& b, float radius, Color color);
        void DrawCapsule (const Vec3& a, const Vec3& b, float radius, uint8_t shade);
        void DrawCapsule (const Vec3& a, const Vec3& b, float radius, Color color);
        void DrawGround  (float y_level, float half_extent, uint8_t shade);
        void DrawGround  (float y_level, float half_extent, Color color);
        void DrawLine    (const Vec3& a, const Vec3& b, Color color = {255,255,255,255});

        const std::vector<RenderCommand>& GetCommands() const { return m_Commands; }
        void Clear() { m_Commands.clear(); }

    private:
        std::vector<RenderCommand> m_Commands;
        PipelineState              m_CurrentPipeline;
        bool                       m_IsRecording = false;
    };

}
