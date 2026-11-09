#pragma once
#include <cstdint>

namespace tvk {

// =========================================================================
// Quality / Preset
// =========================================================================

enum class QualityPreset { Ultra, Balanced, Performance };

// =========================================================================
// Core colour & pixel types
// =========================================================================

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

// =========================================================================
// Math primitives (declared here so they're available to all headers
// without pulling in the full TVK_Math.h)
// =========================================================================

/// 2-D texture / UV coordinate
struct Vec2 {
  float u, v;
};

/// 4-D homogeneous vector (clips, post-projection)
struct Vec4 {
  float x, y, z, w;
};

// =========================================================================
// Terminal blitter type — identifies the active rendering protocol
// =========================================================================

enum class BlitterType {
  BrailleCustom, ///< Hand-rolled braille presenter (built-in fallback)
  Braille,       ///< notcurses NCBLIT_BRAILLE
  Sextant,       ///< notcurses NCBLIT_3x2  (2×3 sub-cells, Unicode 13)
  Octant,        ///< notcurses NCBLIT_4x2  (2×4 sub-cells, Unicode 16)
  Sixel,         ///< notcurses pixel blit – Sixel protocol
  KittyPixel,    ///< notcurses pixel blit – Kitty pixel protocol
};

} // namespace tvk
