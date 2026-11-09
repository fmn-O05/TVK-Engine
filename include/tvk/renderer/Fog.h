#pragma once
#include "tvk/core/Types.h"

namespace tvk {

    struct FogDescriptor {
        bool  enabled  = false;
        float start    = 50.0f;
        float end      = 200.0f;
        Color fogColor = {8, 8, 16, 255};
    };

}
