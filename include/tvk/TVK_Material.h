#pragma once
#include "tvk/TVK_Types.h"

namespace tvk {

    /// Per-object material parameters.
    ///
    /// These are separate from PipelineState (which holds the rasterizer settings).
    /// MaterialState (embedded in PipelineState) carries the shading coefficients.
    /// This standalone Material struct is available for scene-management code.
    struct Material {
        Color albedo    = {255, 255, 255, 255}; ///< Base colour
        float ambient   = 0.15f;  ///< Ambient light factor    [0, 1]
        float diffuse   = 0.85f;  ///< Lambertian diffuse      [0, 1]
        float specular  = 0.40f;  ///< Specular highlight      [0, 1]
        float shininess = 32.0f;  ///< Blinn-Phong exponent   (8 – 256 typical)

        enum class ShadingModel : uint8_t {
            Unlit,       ///< Albedo only, no lighting
            Lambert,     ///< Ambient + diffuse only
            BlinnPhong,  ///< Ambient + diffuse + specular (default)
        };
        ShadingModel model = ShadingModel::BlinnPhong;
    };

} // namespace tvk
