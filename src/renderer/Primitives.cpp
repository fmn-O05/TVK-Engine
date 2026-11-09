#include "tvk/renderer/Primitives.h"
#include <cmath>
#include <vector>

namespace tvk {
namespace primitives {

    static constexpr float kPi  = 3.14159265359f;
    static constexpr float kTau = 6.28318530718f;

    Mesh CreateSphere(int lat, int lon) {
        lat = std::max(2, lat);
        lon = std::max(3, lon);
        std::vector<Vertex>   vertices;
        std::vector<uint32_t> indices;
        vertices.reserve(static_cast<size_t>((lat + 1) * (lon + 1)));

        for (int i = 0; i <= lat; ++i) {
            float phi    = -0.5f * kPi + (i * kPi / lat);
            float sinPhi = std::sin(phi), cosPhi = std::cos(phi);
            float v_coord = static_cast<float>(i) / lat;

            for (int j = 0; j <= lon; ++j) {
                float theta  = j * kTau / lon;
                Vec3  n      = {cosPhi * std::cos(theta), sinPhi, cosPhi * std::sin(theta)};
                float u_coord = static_cast<float>(j) / lon;
                vertices.push_back({n, n, {255,255,255,255}, {u_coord, v_coord}});
            }
        }

        indices.reserve(static_cast<size_t>(lat * lon * 6));
        for (int i = 0; i < lat; ++i) {
            for (int j = 0; j < lon; ++j) {
                uint32_t first  = static_cast<uint32_t>((i * (lon + 1)) + j);
                uint32_t second = first + static_cast<uint32_t>(lon + 1);
                indices.push_back(first);   indices.push_back(second);   indices.push_back(first  + 1);
                indices.push_back(second);  indices.push_back(second + 1); indices.push_back(first + 1);
            }
        }
        return Mesh(vertices, indices);
    }

    Mesh CreateCylinder(int cyl_len, int cyl_ring) {
        cyl_len  = std::max(1, cyl_len);
        cyl_ring = std::max(3, cyl_ring);
        std::vector<Vertex>   vertices;
        std::vector<uint32_t> indices;

        for (int i = 0; i <= cyl_len; ++i) {
            float y       = -0.5f + static_cast<float>(i) / cyl_len;
            float v_coord = static_cast<float>(i) / cyl_len;
            for (int j = 0; j <= cyl_ring; ++j) {
                float ang     = j * kTau / cyl_ring;
                Vec3  n       = {std::sin(ang), 0.0f, std::cos(ang)};
                float u_coord = static_cast<float>(j) / cyl_ring;
                vertices.push_back({{n.x, y, n.z}, n, {255,255,255,255}, {u_coord, v_coord}});
            }
        }

        const uint32_t side_verts = static_cast<uint32_t>(vertices.size());

        for (int i = 0; i < cyl_len; ++i) {
            for (int j = 0; j < cyl_ring; ++j) {
                uint32_t first  = static_cast<uint32_t>((i * (cyl_ring + 1)) + j);
                uint32_t second = first + static_cast<uint32_t>(cyl_ring + 1);
                indices.push_back(first);   indices.push_back(second);   indices.push_back(first  + 1);
                indices.push_back(second);  indices.push_back(second + 1); indices.push_back(first + 1);
            }
        }

        const uint32_t bot_center_idx = static_cast<uint32_t>(vertices.size());
        vertices.push_back({{0,-.5f,0}, {0,-1,0}, {255,255,255,255}, {0.5f, 0.5f}});
        const uint32_t bot_rim_start = static_cast<uint32_t>(vertices.size());
        for (int j = 0; j <= cyl_ring; ++j) {
            float ang = j * kTau / cyl_ring;
            float u_coord = 0.5f + 0.5f * std::cos(ang);
            float v_coord = 0.5f + 0.5f * std::sin(ang);
            vertices.push_back({{std::sin(ang), -.5f, std::cos(ang)}, {0,-1,0}, {255,255,255,255}, {u_coord, v_coord}});
        }
        for (int j = 0; j < cyl_ring; ++j) {
            indices.push_back(bot_center_idx);
            indices.push_back(bot_rim_start + j + 1);
            indices.push_back(bot_rim_start + j);
        }

        const uint32_t top_center_idx = static_cast<uint32_t>(vertices.size());
        vertices.push_back({{0,.5f,0}, {0,1,0}, {255,255,255,255}, {0.5f, 0.5f}});
        const uint32_t top_rim_start = static_cast<uint32_t>(vertices.size());
        for (int j = 0; j <= cyl_ring; ++j) {
            float ang = j * kTau / cyl_ring;
            float u_coord = 0.5f + 0.5f * std::cos(ang);
            float v_coord = 0.5f + 0.5f * std::sin(ang);
            vertices.push_back({{std::sin(ang), .5f, std::cos(ang)}, {0,1,0}, {255,255,255,255}, {u_coord, v_coord}});
        }
        for (int j = 0; j < cyl_ring; ++j) {
            indices.push_back(top_center_idx);
            indices.push_back(top_rim_start + j);
            indices.push_back(top_rim_start + j + 1);
        }

        (void)side_verts;
        return Mesh(vertices, indices);
    }

    Mesh CreateGround(int steps) {
        steps = std::max(1, steps);
        std::vector<Vertex>   vertices;
        std::vector<uint32_t> indices;
        const Vec3 normal = {0, 1, 0};

        vertices.reserve(static_cast<size_t>((steps + 1) * (steps + 1)));
        for (int gz = 0; gz <= steps; ++gz) {
            float z = -0.5f + static_cast<float>(gz) / steps;
            float v_coord = static_cast<float>(gz) / steps;
            for (int gx = 0; gx <= steps; ++gx) {
                float x = -0.5f + static_cast<float>(gx) / steps;
                float u_coord = static_cast<float>(gx) / steps;
                vertices.push_back({{x, 0.0f, z}, normal, {255,255,255,255}, {u_coord, v_coord}});
            }
        }

        indices.reserve(static_cast<size_t>(steps * steps * 6));
        for (int gz = 0; gz < steps; ++gz) {
            for (int gx = 0; gx < steps; ++gx) {
                uint32_t first  = static_cast<uint32_t>((gz * (steps + 1)) + gx);
                uint32_t second = first + static_cast<uint32_t>(steps + 1);
                indices.push_back(first);   indices.push_back(second);   indices.push_back(first  + 1);
                indices.push_back(second);  indices.push_back(second + 1); indices.push_back(first + 1);
            }
        }
        return Mesh(vertices, indices);
    }

    void PrimitiveManager::RebuildSharedMeshes() {
        m_SharedSphere   = CreateSphere(m_SphereLat, m_SphereLon);
        m_SharedCylinder = CreateCylinder(m_CylLen, m_CylRing);
        m_SharedGround   = CreateGround(m_GroundSteps);
    }

    void PrimitiveManager::ApplyDetailProfile(float base_scale, int term_w, bool adaptive, int quality_preset) {
        float quality_scale = 1.0f;
        if (quality_preset == 0) quality_scale = 2.0f;
        else if (quality_preset == 1) quality_scale = 1.0f;
        else if (quality_preset == 2) quality_scale = 0.55f;

        float eff = base_scale * quality_scale;

        if (adaptive && term_w > 0) {
            float boost = std::sqrt(static_cast<float>(term_w) / 80.0f);
            eff *= std::max(0.5f, std::min(boost, 3.0f));
        }

        m_SphereLat   = std::max(4, static_cast<int>(14.0f * eff));
        m_SphereLon   = std::max(8, static_cast<int>(28.0f * eff));
        m_CylLen      = std::max(2, static_cast<int>(12.0f * eff));
        m_CylRing     = std::max(6, static_cast<int>(18.0f * eff));
        m_GroundSteps = std::max(4, static_cast<int>(22.0f * eff));
    }
}
}
