#pragma once
#include "tvk/TVK_Mesh.h"

namespace tvk {
    namespace primitives {
        
        Mesh CreateSphere(int lat_segments, int lon_segments);
        Mesh CreateCylinder(int cyl_len, int cyl_ring);
        Mesh CreateGround(int ground_steps);

        class PrimitiveManager {
        public:
            void ApplyDetailProfile(float base_scale, int term_w, bool adaptive, int quality_preset);
            void RebuildSharedMeshes();

            const Mesh& GetSphere() const { return m_SharedSphere; }
            const Mesh& GetCylinder() const { return m_SharedCylinder; }
            const Mesh& GetGround() const { return m_SharedGround; }

        private:
            int m_SphereLat   = 14;
            int m_SphereLon   = 28;
            int m_CylLen      = 12;
            int m_CylRing     = 18;
            int m_GroundSteps = 22;

            Mesh m_SharedSphere;
            Mesh m_SharedCylinder;
            Mesh m_SharedGround;
        };

    } // namespace primitives
} // namespace tvk
