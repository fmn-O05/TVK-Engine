#pragma once
#include "tvk/renderer/Mesh.h"

namespace tvk {
    namespace primitives {

        Mesh CreateSphere(int lat_segments, int lon_segments);
        Mesh CreateCylinder(int cyl_len, int cyl_ring);
        Mesh CreateGround(int ground_steps);

        Mesh CreateBox(float width = 1.0f, float height = 1.0f, float depth = 1.0f, Color color = {255, 255, 255, 255});
        Mesh CreateCone(float radius = 1.0f, float height = 2.0f, int segments = 24, Color color = {255, 255, 255, 255});
        Mesh CreateTorus(float majorRadius = 1.0f, float minorRadius = 0.3f, int majorSegments = 24, int minorSegments = 16, Color color = {255, 255, 255, 255});
        Mesh CreatePlane(float width = 10.0f, float depth = 10.0f, int gridX = 10, int gridZ = 10, Color color = {255, 255, 255, 255});

        class PrimitiveManager {
        public:
            void ApplyDetailProfile(float base_scale, int term_w, bool adaptive, int quality_preset);
            void ApplyDetailProfile(float base_scale, int term_w, bool adaptive, QualityPreset preset) {
                ApplyDetailProfile(base_scale, term_w, adaptive, static_cast<int>(preset));
            }
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

    }
}
