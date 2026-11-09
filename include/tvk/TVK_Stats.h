#pragma once
#include <cstdint>

namespace tvk {

    /// Per-frame performance counters exposed on Renderer::GetFrameStats().
    /// All timing values are wall-clock nanoseconds measured via pal::GetTimestampNs().
    struct FrameStats {
        // ── Timing (ns) ──────────────────────────────────────────────────────
        uint64_t frameTimeNs     = 0; ///< Total frame time (BeginFrame → end of Present)
        uint64_t vertexPhaseNs   = 0; ///< Duration of parallel vertex-transform phase
        uint64_t geometryPhaseNs = 0; ///< Duration of parallel geometry (bin-to-tile) phase
        uint64_t rasterPhaseNs   = 0; ///< Duration of parallel tile rasterization phase
        uint64_t presentNs       = 0; ///< Duration of terminal present (cell diff + write)

        // ── Geometry counters ─────────────────────────────────────────────────
        uint32_t drawCallCount       = 0; ///< Number of RenderCommands in the command buffer
        uint32_t drawCallsCulled     = 0; ///< Draw calls rejected by AABB frustum cull
        uint32_t trianglesSubmitted  = 0; ///< Total triangles entering the geometry phase
        uint32_t trianglesCulled     = 0; ///< Triangles killed by backface / near-clip
        uint32_t trianglesRasterized = 0; ///< Triangles that reached at least one tile

        // ── Arena health ─────────────────────────────────────────────────────
        uint32_t arenaOverflows = 0; ///< Silent triangle drops due to arena exhaustion (target: 0)

        // ── Renderer state ────────────────────────────────────────────────────
        int activeLights = 0; ///< Number of lights active this frame
        int numWorkers   = 0; ///< Worker thread count (including main)
    };

} // namespace tvk
