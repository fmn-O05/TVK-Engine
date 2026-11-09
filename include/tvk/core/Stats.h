#pragma once
#include <cstdint>

namespace tvk {

    struct FrameStats {

        uint64_t frameTimeNs     = 0;
        uint64_t vertexPhaseNs   = 0;
        uint64_t geometryPhaseNs = 0;
        uint64_t rasterPhaseNs   = 0;
        uint64_t presentNs       = 0;

        uint32_t drawCallCount       = 0;
        uint32_t drawCallsCulled     = 0;
        uint32_t trianglesSubmitted  = 0;
        uint32_t trianglesCulled     = 0;
        uint32_t trianglesRasterized = 0;

        uint32_t arenaOverflows = 0;

        int activeLights = 0;
        int numWorkers   = 0;
    };

}
