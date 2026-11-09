#include "tvk/renderer/CommandBuffer.h"

namespace tvk {

    void CommandBuffer::Begin() {
        m_Commands.clear();
        m_IsRecording = true;
    }

    void CommandBuffer::End() {
        m_IsRecording = false;
    }

    void CommandBuffer::BindPipeline(const PipelineState& pso) {
        if (m_IsRecording) m_CurrentPipeline = pso;
    }

    void CommandBuffer::DrawMesh(const Mesh& mesh, const Mat4& transform, Color color) {
        if (!m_IsRecording) return;
        RenderCommand cmd;
        cmd.type      = CommandType::DrawMesh;
        cmd.pso       = m_CurrentPipeline;
        cmd.mesh      = &mesh;
        cmd.transform = transform;
        cmd.color     = color;
        m_Commands.push_back(cmd);
    }

    void CommandBuffer::DrawSphere(const Vec3& center, float radius, uint8_t shade) {
        if (!m_IsRecording) return;
        RenderCommand cmd;
        cmd.type   = CommandType::DrawSphere;
        cmd.pso    = m_CurrentPipeline;
        cmd.a      = center;
        cmd.radius = radius;
        cmd.shade  = shade;
        cmd.color  = {shade, shade, shade, 255};
        m_Commands.push_back(cmd);
    }

    void CommandBuffer::DrawSphere(const Vec3& center, float radius, Color color) {
        if (!m_IsRecording) return;
        RenderCommand cmd;
        cmd.type   = CommandType::DrawSphere;
        cmd.pso    = m_CurrentPipeline;
        cmd.a      = center;
        cmd.radius = radius;
        cmd.shade  = color.r;
        cmd.color  = color;
        m_Commands.push_back(cmd);
    }

    void CommandBuffer::DrawCylinder(const Vec3& a, const Vec3& b, float radius, uint8_t shade) {
        if (!m_IsRecording) return;
        RenderCommand cmd;
        cmd.type   = CommandType::DrawCylinder;
        cmd.pso    = m_CurrentPipeline;
        cmd.a = a; cmd.b = b;
        cmd.radius = radius;
        cmd.shade  = shade;
        cmd.color  = {shade, shade, shade, 255};
        m_Commands.push_back(cmd);
    }

    void CommandBuffer::DrawCylinder(const Vec3& a, const Vec3& b, float radius, Color color) {
        if (!m_IsRecording) return;
        RenderCommand cmd;
        cmd.type   = CommandType::DrawCylinder;
        cmd.pso    = m_CurrentPipeline;
        cmd.a = a; cmd.b = b;
        cmd.radius = radius;
        cmd.shade  = color.r;
        cmd.color  = color;
        m_Commands.push_back(cmd);
    }

    void CommandBuffer::DrawCapsule(const Vec3& a, const Vec3& b, float radius, uint8_t shade) {
        if (!m_IsRecording) return;
        RenderCommand cmd;
        cmd.type   = CommandType::DrawCapsule;
        cmd.pso    = m_CurrentPipeline;
        cmd.a = a; cmd.b = b;
        cmd.radius = radius;
        cmd.shade  = shade;
        cmd.color  = {shade, shade, shade, 255};
        m_Commands.push_back(cmd);
    }

    void CommandBuffer::DrawCapsule(const Vec3& a, const Vec3& b, float radius, Color color) {
        if (!m_IsRecording) return;
        RenderCommand cmd;
        cmd.type   = CommandType::DrawCapsule;
        cmd.pso    = m_CurrentPipeline;
        cmd.a = a; cmd.b = b;
        cmd.radius = radius;
        cmd.shade  = color.r;
        cmd.color  = color;
        m_Commands.push_back(cmd);
    }

    void CommandBuffer::DrawGround(float y_level, float half_extent, uint8_t shade) {
        if (!m_IsRecording) return;
        RenderCommand cmd;
        cmd.type  = CommandType::DrawGround;
        cmd.pso   = m_CurrentPipeline;
        cmd.a     = {0.0f, y_level,      0.0f};
        cmd.b     = {0.0f, half_extent,  0.0f};
        cmd.shade = shade;
        cmd.color = {shade, shade, shade, 255};
        m_Commands.push_back(cmd);
    }

    void CommandBuffer::DrawGround(float y_level, float half_extent, Color color) {
        if (!m_IsRecording) return;
        RenderCommand cmd;
        cmd.type  = CommandType::DrawGround;
        cmd.pso   = m_CurrentPipeline;
        cmd.a     = {0.0f, y_level,      0.0f};
        cmd.b     = {0.0f, half_extent,  0.0f};
        cmd.shade = color.r;
        cmd.color = color;
        m_Commands.push_back(cmd);
    }

    void CommandBuffer::DrawLine(const Vec3& a, const Vec3& b, Color color) {
        if (!m_IsRecording) return;
        RenderCommand cmd;
        cmd.type  = CommandType::DrawLine;
        cmd.pso   = m_CurrentPipeline;
        cmd.a     = a;
        cmd.b     = b;
        cmd.color = color;
        m_Commands.push_back(cmd);
    }

}
