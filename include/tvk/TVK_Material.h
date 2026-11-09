#pragma once
#include "tvk/TVK_Types.h"

namespace tvk {

    struct Material {
        Color albedo    = {255, 255, 255, 255};
        float ambient   = 0.15f;
        float diffuse   = 0.85f;
        float specular  = 0.40f;
        float shininess = 32.0f;

        enum class ShadingModel : uint8_t {
            Unlit,
            Lambert,
            BlinnPhong,
        };
        ShadingModel model = ShadingModel::BlinnPhong;
    };

}
