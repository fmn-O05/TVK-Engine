#include "tvk/renderer/CommandBuffer.h"
#include "tvk/renderer/Camera.h"
#include <algorithm>
#include <cmath>

namespace tvk {

    void CommandBuffer::Begin() {
        m_Commands.clear();
        m_Sequence = 0;
        m_IsRecording = true;
    }

    void CommandBuffer::End() {
        m_IsRecording = false;
    }

    void CommandBuffer::Reset() {
        m_Commands.clear();
        m_Sequence = 0;
        m_IsRecording = false;
    }

    void CommandBuffer::BindPipeline(const PipelineState& pso) {
        if (m_IsRecording) m_CurrentPipeline = pso;
    }

    void CommandBuffer::StampAndPush(RenderCommand& cmd) {
        const uint16_t seq = m_Sequence;
        m_Sequence = static_cast<uint16_t>((m_Sequence + 1u) & 0xFFFFu);
        cmd.sortKey = SortKey::EncodeSortKey(
            cmd.pso.pass,
            cmd.pso.layer,
            cmd.pso.depthOp,
            cmd.pso.materialId,
            0,
            seq
        );
        m_Commands.push_back(cmd);
    }

    void CommandBuffer::DrawMesh(const Mesh& mesh, const Mat4& transform, Color color) {
        if (!m_IsRecording) return;
        RenderCommand cmd;
        cmd.type      = CommandType::DrawMesh;
        cmd.pso       = m_CurrentPipeline;
        cmd.mesh      = &mesh;
        cmd.transform = transform;
        cmd.color     = color;
        StampAndPush(cmd);
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
        StampAndPush(cmd);
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
        StampAndPush(cmd);
    }

    void CommandBuffer::DrawCylinder(const Vec3& a, const Vec3& b, float radius, uint8_t shade) {
        if (!m_IsRecording) return;
        RenderCommand cmd;
        cmd.type   = CommandType::DrawCylinder;
        cmd.pso    = m_CurrentPipeline;
        cmd.a      = a;
        cmd.b      = b;
        cmd.radius = radius;
        cmd.shade  = shade;
        cmd.color  = {shade, shade, shade, 255};
        StampAndPush(cmd);
    }

    void CommandBuffer::DrawCylinder(const Vec3& a, const Vec3& b, float radius, Color color) {
        if (!m_IsRecording) return;
        RenderCommand cmd;
        cmd.type   = CommandType::DrawCylinder;
        cmd.pso    = m_CurrentPipeline;
        cmd.a      = a;
        cmd.b      = b;
        cmd.radius = radius;
        cmd.shade  = color.r;
        cmd.color  = color;
        StampAndPush(cmd);
    }

    void CommandBuffer::DrawCapsule(const Vec3& a, const Vec3& b, float radius, uint8_t shade) {
        if (!m_IsRecording) return;
        RenderCommand cmd;
        cmd.type   = CommandType::DrawCapsule;
        cmd.pso    = m_CurrentPipeline;
        cmd.a      = a;
        cmd.b      = b;
        cmd.radius = radius;
        cmd.shade  = shade;
        cmd.color  = {shade, shade, shade, 255};
        StampAndPush(cmd);
    }

    void CommandBuffer::DrawCapsule(const Vec3& a, const Vec3& b, float radius, Color color) {
        if (!m_IsRecording) return;
        RenderCommand cmd;
        cmd.type   = CommandType::DrawCapsule;
        cmd.pso    = m_CurrentPipeline;
        cmd.a      = a;
        cmd.b      = b;
        cmd.radius = radius;
        cmd.shade  = color.r;
        cmd.color  = color;
        StampAndPush(cmd);
    }

    void CommandBuffer::DrawGround(float y_level, float half_extent, uint8_t shade) {
        if (!m_IsRecording) return;
        RenderCommand cmd;
        cmd.type  = CommandType::DrawGround;
        cmd.pso   = m_CurrentPipeline;
        cmd.a     = {0.0f, y_level,     0.0f};
        cmd.b     = {0.0f, half_extent, 0.0f};
        cmd.shade = shade;
        cmd.color = {shade, shade, shade, 255};
        StampAndPush(cmd);
    }

    void CommandBuffer::DrawGround(float y_level, float half_extent, Color color) {
        if (!m_IsRecording) return;
        RenderCommand cmd;
        cmd.type  = CommandType::DrawGround;
        cmd.pso   = m_CurrentPipeline;
        cmd.a     = {0.0f, y_level,     0.0f};
        cmd.b     = {0.0f, half_extent, 0.0f};
        cmd.shade = color.r;
        cmd.color = color;
        StampAndPush(cmd);
    }

    void CommandBuffer::DrawLine(const Vec3& a, const Vec3& b, Color color) {
        if (!m_IsRecording) return;
        RenderCommand cmd;
        cmd.type  = CommandType::DrawLine;
        cmd.pso   = m_CurrentPipeline;
        cmd.a     = a;
        cmd.b     = b;
        cmd.color = color;
        StampAndPush(cmd);
    }

    void CommandBuffer::DrawWireBox(const Vec3& min, const Vec3& max, Color color) {
        if (!m_IsRecording) return;

        const float x0 = std::min(min.x, max.x);
        const float x1 = std::max(min.x, max.x);
        const float y0 = std::min(min.y, max.y);
        const float y1 = std::max(min.y, max.y);
        const float z0 = std::min(min.z, max.z);
        const float z1 = std::max(min.z, max.z);

        const Vec3 c[8] = {
            {x0, y0, z0}, // 0
            {x1, y0, z0}, // 1
            {x1, y1, z0}, // 2
            {x0, y1, z0}, // 3
            {x0, y0, z1}, // 4
            {x1, y0, z1}, // 5
            {x1, y1, z1}, // 6
            {x0, y1, z1}  // 7
        };

        // Bottom quad (z = z0)
        DrawLine(c[0], c[1], color);
        DrawLine(c[1], c[2], color);
        DrawLine(c[2], c[3], color);
        DrawLine(c[3], c[0], color);

        // Top quad (z = z1)
        DrawLine(c[4], c[5], color);
        DrawLine(c[5], c[6], color);
        DrawLine(c[6], c[7], color);
        DrawLine(c[7], c[4], color);

        // Vertical edges connecting z0 and z1
        DrawLine(c[0], c[4], color);
        DrawLine(c[1], c[5], color);
        DrawLine(c[2], c[6], color);
        DrawLine(c[3], c[7], color);
    }

    void CommandBuffer::DrawWireBox(const AABB& box, Color color) {
        DrawWireBox(box.min, box.max, color);
    }

    void CommandBuffer::DrawWireSphere(const Vec3& center, float radius, Color color, int segments) {
        if (!m_IsRecording) return;
        radius = std::max(0.0f, radius);
        segments = std::max(6, segments);

        static constexpr float kTau = 6.28318530717958647692f;
        const float step = kTau / static_cast<float>(segments);

        for (int i = 0; i < segments; ++i) {
            const float t0 = static_cast<float>(i) * step;
            const float t1 = static_cast<float>(i + 1) * step;
            const float cos0 = std::cos(t0), sin0 = std::sin(t0);
            const float cos1 = std::cos(t1), sin1 = std::sin(t1);

            // XY circle (z = center.z)
            DrawLine({center.x + radius * cos0, center.y + radius * sin0, center.z},
                     {center.x + radius * cos1, center.y + radius * sin1, center.z}, color);

            // XZ circle (y = center.y)
            DrawLine({center.x + radius * cos0, center.y, center.z + radius * sin0},
                     {center.x + radius * cos1, center.y, center.z + radius * sin1}, color);

            // YZ circle (x = center.x)
            DrawLine({center.x, center.y + radius * cos0, center.z + radius * sin0},
                     {center.x, center.y + radius * cos1, center.z + radius * sin1}, color);
        }
    }

    void CommandBuffer::DrawGrid(float size, float step, Color color) {
        if (!m_IsRecording) return;
        size = std::max(1e-4f, std::abs(size));
        step = std::max(1e-4f, std::abs(step));
        const float half = size * 0.5f;

        // Lines parallel to Z axis (stepping along X)
        for (float x = -half; x <= half + 1e-4f; x += step) {
            const float cx = std::min(x, half);
            DrawLine({cx, 0.0f, -half}, {cx, 0.0f, half}, color);
        }
        // Lines parallel to X axis (stepping along Z)
        for (float z = -half; z <= half + 1e-4f; z += step) {
            const float cz = std::min(z, half);
            DrawLine({-half, 0.0f, cz}, {half, 0.0f, cz}, color);
        }
    }

    void CommandBuffer::DrawGrid(float y_level, float half_extent, float grid_spacing, Color color) {
        if (!m_IsRecording) return;
        half_extent = std::max(1e-4f, std::abs(half_extent));
        grid_spacing = std::max(1e-4f, std::abs(grid_spacing));

        for (float x = -half_extent; x <= half_extent + 1e-4f; x += grid_spacing) {
            const float cx = std::min(x, half_extent);
            DrawLine({cx, y_level, -half_extent}, {cx, y_level, half_extent}, color);
        }
        for (float z = -half_extent; z <= half_extent + 1e-4f; z += grid_spacing) {
            const float cz = std::min(z, half_extent);
            DrawLine({-half_extent, y_level, cz}, {half_extent, y_level, cz}, color);
        }
    }

    void CommandBuffer::DrawAxes(const Mat4& transform, float length) {
        if (!m_IsRecording) return;
        const Vec3 origin = VectorTransform({0.0f, 0.0f, 0.0f}, transform);
        const Vec3 posX   = VectorTransform({length, 0.0f, 0.0f}, transform);
        const Vec3 posY   = VectorTransform({0.0f, length, 0.0f}, transform);
        const Vec3 posZ   = VectorTransform({0.0f, 0.0f, length}, transform);

        DrawLine(origin, posX, {255, 0, 0, 255});
        DrawLine(origin, posY, {0, 255, 0, 255});
        DrawLine(origin, posZ, {0, 0, 255, 255});
    }

    void CommandBuffer::DrawAxes(const Vec3& origin, float length) {
        if (!m_IsRecording) return;
        DrawLine(origin, Add(origin, {length, 0.0f, 0.0f}), {255, 0, 0, 255});
        DrawLine(origin, Add(origin, {0.0f, length, 0.0f}), {0, 255, 0, 255});
        DrawLine(origin, Add(origin, {0.0f, 0.0f, length}), {0, 0, 255, 255});
    }

    void CommandBuffer::DrawFrustum(const Mat4& invViewProj, Color color) {
        if (!m_IsRecording) return;

        // 8 NDC corners in OpenGL style ([-1, 1]^3)
        // Near plane (z = -1)
        const Vec3 ndc[8] = {
            {-1.0f, -1.0f, -1.0f}, // 0: near bottom-left
            { 1.0f, -1.0f, -1.0f}, // 1: near bottom-right
            { 1.0f,  1.0f, -1.0f}, // 2: near top-right
            {-1.0f,  1.0f, -1.0f}, // 3: near top-left
            // Far plane (z = +1)
            {-1.0f, -1.0f,  1.0f}, // 4: far bottom-left
            { 1.0f, -1.0f,  1.0f}, // 5: far bottom-right
            { 1.0f,  1.0f,  1.0f}, // 6: far top-right
            {-1.0f,  1.0f,  1.0f}  // 7: far top-left
        };

        Vec3 world[8];
        for (int i = 0; i < 8; ++i) {
            world[i] = VectorTransform(ndc[i], invViewProj);
        }

        // Near face (4 edges)
        DrawLine(world[0], world[1], color);
        DrawLine(world[1], world[2], color);
        DrawLine(world[2], world[3], color);
        DrawLine(world[3], world[0], color);

        // Far face (4 edges)
        DrawLine(world[4], world[5], color);
        DrawLine(world[5], world[6], color);
        DrawLine(world[6], world[7], color);
        DrawLine(world[7], world[4], color);

        // Connecting edges between near and far (4 edges)
        DrawLine(world[0], world[4], color);
        DrawLine(world[1], world[5], color);
        DrawLine(world[2], world[6], color);
        DrawLine(world[3], world[7], color);
    }

    void CommandBuffer::DrawLines(const Vec3* points, size_t count, Color color) {
        if (!m_IsRecording || points == nullptr || count < 2) return;
        for (size_t i = 0; i + 1 < count; i += 2) {
            DrawLine(points[i], points[i + 1], color);
        }
    }

    void CommandBuffer::DrawLines(const std::vector<Vec3>& points, Color color) {
        DrawLines(points.data(), points.size(), color);
    }

    void CommandBuffer::Sort(const Camera* camera) {
        if (m_Commands.empty()) {
            return;
        }

        if (camera != nullptr) {
            const Mat4 view = camera->GetViewMatrix();
            const float nearPlane = camera->GetNear();
            const float farPlane = camera->GetFar();
            const float depthRange = farPlane - nearPlane;
            const float invDepthRange = (std::abs(depthRange) > 1e-6f) ? (1.0f / depthRange) : 0.0f;

            for (auto& cmd : m_Commands) {
                Vec3 worldPos{};
                switch (cmd.type) {
                    case CommandType::DrawMesh: {
                        worldPos = {cmd.transform.m[0][3], cmd.transform.m[1][3], cmd.transform.m[2][3]};
                        if (cmd.mesh && cmd.mesh->GetAABB().IsValid()) {
                            worldPos = VectorTransform(cmd.mesh->GetAABB().Center(), cmd.transform);
                        }
                        break;
                    }
                    case CommandType::DrawSphere: {
                        worldPos = cmd.a;
                        break;
                    }
                    case CommandType::DrawCylinder:
                    case CommandType::DrawCapsule:
                    case CommandType::DrawLine: {
                        worldPos = Mul(Add(cmd.a, cmd.b), 0.5f);
                        break;
                    }
                    case CommandType::DrawGround: {
                        worldPos = {0.0f, cmd.a.y, 0.0f};
                        break;
                    }
                    default: {
                        worldPos = {cmd.transform.m[0][3], cmd.transform.m[1][3], cmd.transform.m[2][3]};
                        break;
                    }
                }

                const Vec3 viewPos = VectorTransform(worldPos, view);
                const float z_view = viewPos.z;

                const DepthOp depthOp = SortKey::ExtractDepthOp(cmd.sortKey);
                uint16_t quantizedDepth = 0;

                if (depthOp == DepthOp::Always) {
                    quantizedDepth = 0;
                } else {
                    float z_norm = (z_view - nearPlane) * invDepthRange;
                    z_norm = ClampFloat(z_norm, 0.0f, 1.0f);
                    const uint16_t rawDepth = static_cast<uint16_t>(z_norm * 65535.0f + 0.5f);

                    if (depthOp == DepthOp::Greater || depthOp == DepthOp::GreaterOrEqual) {
                        quantizedDepth = static_cast<uint16_t>(65535u - rawDepth);
                    } else {
                        quantizedDepth = rawDepth;
                    }
                }

                cmd.sortKey = (cmd.sortKey & ~(SortKey::kDepthMask << SortKey::kDepthShift))
                            | (static_cast<uint64_t>(quantizedDepth & SortKey::kDepthMask) << SortKey::kDepthShift);
            }
        }

        std::stable_sort(m_Commands.begin(), m_Commands.end(),
            [](const RenderCommand& a, const RenderCommand& b) noexcept {
                return a.sortKey < b.sortKey;
            });
    }

}
