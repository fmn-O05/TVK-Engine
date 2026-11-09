#pragma once
#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include "tvk/TVK_Math.h"
#include "tvk/TVK_Types.h"

namespace tvk {

    // =========================================================================
    // Axis-Aligned Bounding Box
    // =========================================================================

    struct AABB {
        Vec3 min = { 1e38f,  1e38f,  1e38f};
        Vec3 max = {-1e38f, -1e38f, -1e38f};

        bool IsValid() const { return min.x <= max.x; }

        Vec3 Center()  const { return {(min.x+max.x)*0.5f, (min.y+max.y)*0.5f, (min.z+max.z)*0.5f}; }
        Vec3 Extents() const { return {(max.x-min.x)*0.5f, (max.y-min.y)*0.5f, (max.z-min.z)*0.5f}; }

        void Expand(const Vec3& p) {
            min.x = std::min(min.x, p.x); min.y = std::min(min.y, p.y); min.z = std::min(min.z, p.z);
            max.x = std::max(max.x, p.x); max.y = std::max(max.y, p.y); max.z = std::max(max.z, p.z);
        }
    };

    // =========================================================================
    // Vertex
    // =========================================================================

    struct Vertex {
        Vec3  position;
        Vec3  normal;
        Color color;
        Vec2  uv = {0.0f, 0.0f}; ///< Texture coordinate (zero-init – backward compatible)
    };

    // =========================================================================
    // Mesh
    // =========================================================================

    class Mesh {
    public:
        Mesh() = default;
        Mesh(std::vector<Vertex> vertices, std::vector<uint32_t> indices)
            : m_Vertices(std::move(vertices)), m_Indices(std::move(indices))
        { CacheAABB(); }

        const std::vector<Vertex>&   GetVertices() const { return m_Vertices; }
        const std::vector<uint32_t>& GetIndices()  const { return m_Indices; }

        void SetVertices(std::vector<Vertex> v) { m_Vertices = std::move(v); m_AABBDirty = true; }
        void SetIndices (std::vector<uint32_t> i) { m_Indices  = std::move(i); }

        void Clear() { m_Vertices.clear(); m_Indices.clear(); m_AABB = {}; m_AABBDirty = false; }

        /// Returns the pre-computed object-space AABB.  Recomputed lazily on first call
        /// after SetVertices / construction.  O(n) first time, O(1) after.
        const AABB& GetAABB() const {
            if (m_AABBDirty) CacheAABB();
            return m_AABB;
        }

    private:
        void CacheAABB() const {
            m_AABB = {};
            for (const auto& v : m_Vertices) m_AABB.Expand(v.position);
            m_AABBDirty = false;
        }

        std::vector<Vertex>   m_Vertices;
        std::vector<uint32_t> m_Indices;
        mutable AABB m_AABB;
        mutable bool m_AABBDirty = true;
    };

} // namespace tvk
