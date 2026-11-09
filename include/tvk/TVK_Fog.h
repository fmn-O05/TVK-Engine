#pragma once
#include "tvk/TVK_Types.h"

namespace tvk {

    /// Linear depth-fog parameters.
    ///
    /// Fog is computed per-pixel in view space using the interpolated 1/Z value:
    ///   fog_t = clamp( (view_z - start) / (end - start), 0, 1 )
    ///   final_rgb = lerp( rendered_rgb, fog_color, fog_t )
    ///
    /// Attach to a Renderer via Renderer::SetFog(). Disable cheaply with enabled=false;
    /// the AVX2 inner loop skips the fog path entirely when disabled.
    struct FogDescriptor {
        bool  enabled  = false;
        float start    = 50.0f;              ///< View-space Z where fog begins (0 = no fog)
        float end      = 200.0f;             ///< View-space Z where scene is fully fogged
        Color fogColor = {8, 8, 16, 255};   ///< Deep space blue-black default
    };

} // namespace tvk
