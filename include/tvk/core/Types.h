#pragma once
#include <cstdint>

namespace tvk {

enum class QualityPreset { Ultra, Balanced, Performance };

struct Color {
  uint8_t r, g, b, a;
  bool operator==(const Color &o) const {
    return r == o.r && g == o.g && b == o.b && a == o.a;
  }
  bool operator!=(const Color &o) const { return !(*this == o); }
};

struct alignas(4) Pixel {
  uint8_t r, g, b, a;
};

struct Vec2 {
  float u, v;
};

struct Vec4 {
  float x, y, z, w;
};

enum class BlitterType {
  BrailleCustom,
  Braille,
  Sextant,
  Octant,
  Sixel,
  KittyPixel,
};

}
