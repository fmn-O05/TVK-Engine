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

    Mesh CreateBox(float width, float height, float depth, Color color) {
        width  = std::max(1e-4f, std::abs(width));
        height = std::max(1e-4f, std::abs(height));
        depth  = std::max(1e-4f, std::abs(depth));

        const float hx = width * 0.5f;
        const float hy = height * 0.5f;
        const float hz = depth * 0.5f;

        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        vertices.reserve(24);
        indices.reserve(36);

        auto addFace = [&](const Vec3& v0, const Vec3& v1, const Vec3& v2, const Vec3& v3, const Vec3& normal) {
            const uint32_t base = static_cast<uint32_t>(vertices.size());
            vertices.push_back({v0, normal, color, {0.0f, 0.0f}});
            vertices.push_back({v1, normal, color, {1.0f, 0.0f}});
            vertices.push_back({v2, normal, color, {1.0f, 1.0f}});
            vertices.push_back({v3, normal, color, {0.0f, 1.0f}});

            indices.push_back(base + 0);
            indices.push_back(base + 1);
            indices.push_back(base + 2);

            indices.push_back(base + 0);
            indices.push_back(base + 2);
            indices.push_back(base + 3);
        };

        // Front (+Z)
        addFace({-hx, -hy,  hz}, { hx, -hy,  hz}, { hx,  hy,  hz}, {-hx,  hy,  hz}, { 0.0f,  0.0f,  1.0f});
        // Back (-Z)
        addFace({ hx, -hy, -hz}, {-hx, -hy, -hz}, {-hx,  hy, -hz}, { hx,  hy, -hz}, { 0.0f,  0.0f, -1.0f});
        // Top (+Y)
        addFace({-hx,  hy,  hz}, { hx,  hy,  hz}, { hx,  hy, -hz}, {-hx,  hy, -hz}, { 0.0f,  1.0f,  0.0f});
        // Bottom (-Y)
        addFace({-hx, -hy, -hz}, { hx, -hy, -hz}, { hx, -hy,  hz}, {-hx, -hy,  hz}, { 0.0f, -1.0f,  0.0f});
        // Right (+X)
        addFace({ hx, -hy,  hz}, { hx, -hy, -hz}, { hx,  hy, -hz}, { hx,  hy,  hz}, { 1.0f,  0.0f,  0.0f});
        // Left (-X)
        addFace({-hx, -hy, -hz}, {-hx, -hy,  hz}, {-hx,  hy,  hz}, {-hx,  hy, -hz}, {-1.0f,  0.0f,  0.0f});

        return Mesh(std::move(vertices), std::move(indices));
    }

    Mesh CreateCone(float radius, float height, int segments, Color color) {
        radius   = std::max(1e-4f, std::abs(radius));
        height   = std::max(1e-4f, std::abs(height));
        segments = std::max(3, segments);

        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;

        const float halfH = height * 0.5f;
        const float slopeLen = std::sqrt(height * height + radius * radius);
        const float cosSlope = (slopeLen > 1e-6f) ? (height / slopeLen) : 1.0f;
        const float sinSlope = (slopeLen > 1e-6f) ? (radius / slopeLen) : 0.0f;

        // Side vertices: base ring
        const uint32_t sideBaseStart = static_cast<uint32_t>(vertices.size());
        for (int j = 0; j <= segments; ++j) {
            const float ang = static_cast<float>(j) * kTau / static_cast<float>(segments);
            const float cosA = std::cos(ang);
            const float sinA = std::sin(ang);
            const Vec3 pos = {radius * cosA, -halfH, radius * sinA};
            const Vec3 norm = {cosSlope * cosA, sinSlope, cosSlope * sinA};
            const float u = static_cast<float>(j) / static_cast<float>(segments);
            vertices.push_back({pos, norm, color, {u, 0.0f}});
        }

        // Side vertices: apex vertices (one per segment to interpolate UV and normal properly)
        const uint32_t sideApexStart = static_cast<uint32_t>(vertices.size());
        for (int j = 0; j < segments; ++j) {
            const float angMid = (static_cast<float>(j) + 0.5f) * kTau / static_cast<float>(segments);
            const float cosM = std::cos(angMid);
            const float sinM = std::sin(angMid);
            const Vec3 pos = {0.0f, halfH, 0.0f};
            const Vec3 norm = {cosSlope * cosM, sinSlope, cosSlope * sinM};
            const float u = (static_cast<float>(j) + 0.5f) / static_cast<float>(segments);
            vertices.push_back({pos, norm, color, {u, 1.0f}});
        }

        // Side triangle indices (CCW outward: base[j], apex[j], base[j+1])
        for (int j = 0; j < segments; ++j) {
            indices.push_back(sideBaseStart + static_cast<uint32_t>(j));
            indices.push_back(sideApexStart + static_cast<uint32_t>(j));
            indices.push_back(sideBaseStart + static_cast<uint32_t>(j + 1));
        }

        // Base disk
        const uint32_t baseCenterIdx = static_cast<uint32_t>(vertices.size());
        vertices.push_back({{0.0f, -halfH, 0.0f}, {0.0f, -1.0f, 0.0f}, color, {0.5f, 0.5f}});

        const uint32_t baseRimStart = static_cast<uint32_t>(vertices.size());
        for (int j = 0; j <= segments; ++j) {
            const float ang = static_cast<float>(j) * kTau / static_cast<float>(segments);
            const float cosA = std::cos(ang);
            const float sinA = std::sin(ang);
            const Vec3 pos = {radius * cosA, -halfH, radius * sinA};
            const float u = 0.5f + 0.5f * cosA;
            const float v = 0.5f + 0.5f * sinA;
            vertices.push_back({pos, {0.0f, -1.0f, 0.0f}, color, {u, v}});
        }

        // Base disk triangles (CCW outward: center, rim[j], rim[j+1])
        for (int j = 0; j < segments; ++j) {
            indices.push_back(baseCenterIdx);
            indices.push_back(baseRimStart + static_cast<uint32_t>(j));
            indices.push_back(baseRimStart + static_cast<uint32_t>(j + 1));
        }

        return Mesh(std::move(vertices), std::move(indices));
    }

    Mesh CreateTorus(float majorRadius, float minorRadius, int majorSegments, int minorSegments, Color color) {
        majorRadius   = std::max(1e-4f, std::abs(majorRadius));
        minorRadius   = std::max(1e-4f, std::abs(minorRadius));
        majorSegments = std::max(3, majorSegments);
        minorSegments = std::max(3, minorSegments);

        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        vertices.reserve(static_cast<size_t>((majorSegments + 1) * (minorSegments + 1)));
        indices.reserve(static_cast<size_t>(majorSegments * minorSegments * 6));

        for (int i = 0; i <= majorSegments; ++i) {
            const float phi  = static_cast<float>(i) * kTau / static_cast<float>(majorSegments);
            const float cosPhi = std::cos(phi);
            const float sinPhi = std::sin(phi);
            const float u = static_cast<float>(i) / static_cast<float>(majorSegments);

            for (int j = 0; j <= minorSegments; ++j) {
                const float theta  = static_cast<float>(j) * kTau / static_cast<float>(minorSegments);
                const float cosTheta = std::cos(theta);
                const float sinTheta = std::sin(theta);
                const float v = static_cast<float>(j) / static_cast<float>(minorSegments);

                const Vec3 pos = {
                    (majorRadius + minorRadius * cosTheta) * cosPhi,
                    minorRadius * sinTheta,
                    (majorRadius + minorRadius * cosTheta) * sinPhi
                };
                const Vec3 norm = {
                    cosTheta * cosPhi,
                    sinTheta,
                    cosTheta * sinPhi
                };

                vertices.push_back({pos, norm, color, {u, v}});
            }
        }

        for (int i = 0; i < majorSegments; ++i) {
            for (int j = 0; j < minorSegments; ++j) {
                const uint32_t first = static_cast<uint32_t>(i * (minorSegments + 1) + j);
                const uint32_t nextI = static_cast<uint32_t>((i + 1) * (minorSegments + 1) + j);

                indices.push_back(first);
                indices.push_back(first + 1);
                indices.push_back(nextI + 1);

                indices.push_back(first);
                indices.push_back(nextI + 1);
                indices.push_back(nextI);
            }
        }

        return Mesh(std::move(vertices), std::move(indices));
    }

    Mesh CreatePlane(float width, float depth, int gridX, int gridZ, Color color) {
        width  = std::max(1e-4f, std::abs(width));
        depth  = std::max(1e-4f, std::abs(depth));
        gridX  = std::max(1, gridX);
        gridZ  = std::max(1, gridZ);

        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        const Vec3 normal = {0.0f, 1.0f, 0.0f};

        vertices.reserve(static_cast<size_t>((gridZ + 1) * (gridX + 1)));
        indices.reserve(static_cast<size_t>(gridZ * gridX * 6));

        const float halfW = width * 0.5f;
        const float halfD = depth * 0.5f;

        for (int gz = 0; gz <= gridZ; ++gz) {
            const float z = -halfD + (static_cast<float>(gz) * depth) / static_cast<float>(gridZ);
            const float v = static_cast<float>(gz) / static_cast<float>(gridZ);
            for (int gx = 0; gx <= gridX; ++gx) {
                const float x = -halfW + (static_cast<float>(gx) * width) / static_cast<float>(gridX);
                const float u = static_cast<float>(gx) / static_cast<float>(gridX);
                vertices.push_back({{x, 0.0f, z}, normal, color, {u, v}});
            }
        }

        for (int gz = 0; gz < gridZ; ++gz) {
            for (int gx = 0; gx < gridX; ++gx) {
                const uint32_t first  = static_cast<uint32_t>(gz * (gridX + 1) + gx);
                const uint32_t second = first + static_cast<uint32_t>(gridX + 1);

                indices.push_back(first);
                indices.push_back(first + 1);
                indices.push_back(second);

                indices.push_back(first + 1);
                indices.push_back(second + 1);
                indices.push_back(second);
            }
        }

        return Mesh(std::move(vertices), std::move(indices));
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
