#pragma once
#include <algorithm>
#include <cstdint>
#include "tvk/math/Math.h"

namespace tvk {

enum class QualityPreset { Ultra, Balanced, Performance };

struct Color {
  uint8_t r = 0, g = 0, b = 0, a = 255;

  constexpr bool operator==(const Color &o) const noexcept {
    return r == o.r && g == o.g && b == o.b && a == o.a;
  }
  constexpr bool operator!=(const Color &o) const noexcept {
    return !(*this == o);
  }

  static constexpr Color FromRGB(uint8_t r_, uint8_t g_, uint8_t b_,
                                 uint8_t a_ = 255) noexcept {
    return {r_, g_, b_, a_};
  }
  static constexpr Color White() noexcept { return {255, 255, 255, 255}; }
  static constexpr Color Black() noexcept { return {0, 0, 0, 255}; }
  static constexpr Color Red() noexcept { return {255, 0, 0, 255}; }
  static constexpr Color Green() noexcept { return {0, 255, 0, 255}; }
  static constexpr Color Blue() noexcept { return {0, 0, 255, 255}; }

  static constexpr Color Modulate(Color a, Color b) noexcept {
    return {
        static_cast<uint8_t>((static_cast<uint32_t>(a.r) * b.r) / 255u),
        static_cast<uint8_t>((static_cast<uint32_t>(a.g) * b.g) / 255u),
        static_cast<uint8_t>((static_cast<uint32_t>(a.b) * b.b) / 255u),
        static_cast<uint8_t>((static_cast<uint32_t>(a.a) * b.a) / 255u),
    };
  }

  constexpr uint8_t Luminance() const noexcept {
    return static_cast<uint8_t>((54u * r + 183u * g + 19u * b) >> 8);
  }
};

struct alignas(4) Pixel {
  uint8_t r, g, b, a;
};

enum class BlitterType : uint8_t {
  Braille = 0,   // 2x4 sub-pixels per cell (U+2800..U+28FF) with 2-color FG+BG clustering
  HalfBlock = 1, // 1x2 sub-pixels per cell (U+2580)
  Quadrant = 2,  // 2x2 sub-pixels per cell (U+2580..U+259F)
  Sextant = 3,   // 2x3 sub-pixels per cell (U+1FB00..U+1FB3B)
  ASCII = 4,     // 1x1 cell luminance ramp (" .:-=+*#%@")
  BrailleCustom = Braille,
  Octant = Braille,
  Sixel = HalfBlock,
  KittyPixel = HalfBlock,
};

inline constexpr void GetBlitterSubpixelRatio(BlitterType type, int &out_sub_w,
                                              int &out_sub_h) noexcept {
  switch (type) {
  case BlitterType::Braille:
    out_sub_w = 2;
    out_sub_h = 4;
    break;
  case BlitterType::HalfBlock:
    out_sub_w = 1;
    out_sub_h = 2;
    break;
  case BlitterType::Quadrant:
    out_sub_w = 2;
    out_sub_h = 2;
    break;
  case BlitterType::Sextant:
    out_sub_w = 2;
    out_sub_h = 3;
    break;
  case BlitterType::ASCII:
  default:
    out_sub_w = 1;
    out_sub_h = 1;
    break;
  }
}

} // namespace tvk
