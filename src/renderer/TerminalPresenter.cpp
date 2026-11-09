#include "tvk/renderer/TerminalPresenter.h"
#include "tvk/core/JobSystem.h"
#include "tvk/platform/PAL.h"
#include <algorithm>
#include <charconv>
#include <cstring>

namespace tvk {

namespace {

constexpr uint32_t kInvCount[9] = {0,         65536 / 1, 65536 / 2,
                                   65536 / 3, 65536 / 4, 65536 / 5,
                                   65536 / 6, 65536 / 7, 65536 / 8};

// 16 Quadrant characters indexed by 4-bit mask (TL=1, TR=2, BL=4, BR=8)
constexpr const char *kQuadrantGlyphs[16] = {
    " ",            // 0
    "\xE2\x96\x98", // 1: U+2598 QUADRANT UPPER LEFT
    "\xE2\x96\x9D", // 2: U+259D QUADRANT UPPER RIGHT
    "\xE2\x96\x80", // 3: U+2580 UPPER HALF BLOCK
    "\xE2\x96\x96", // 4: U+2596 QUADRANT LOWER LEFT
    "\xE2\x96\x8C", // 5: U+258C LEFT HALF BLOCK
    "\xE2\x96\x9E", // 6: U+259E QUADRANT UPPER RIGHT AND LOWER LEFT
    "\xE2\x96\x9B", // 7: U+259B QUADRANT UPPER LEFT AND UPPER RIGHT AND LOWER LEFT
    "\xE2\x96\x97", // 8: U+2597 QUADRANT LOWER RIGHT
    "\xE2\x96\x9A", // 9: U+259A QUADRANT UPPER LEFT AND LOWER RIGHT
    "\xE2\x96\x90", // 10: U+2590 RIGHT HALF BLOCK
    "\xE2\x96\x9C", // 11: U+259C QUADRANT UPPER LEFT AND UPPER RIGHT AND LOWER RIGHT
    "\xE2\x96\x84", // 12: U+2584 LOWER HALF BLOCK
    "\xE2\x96\x99", // 13: U+2599 QUADRANT UPPER LEFT AND LOWER LEFT AND LOWER RIGHT
    "\xE2\x96\x9F", // 14: U+259F QUADRANT UPPER RIGHT AND LOWER LEFT AND LOWER RIGHT
    "\xE2\x96\x88"  // 15: U+2588 FULL BLOCK
};

constexpr char kAsciiRamp[11] = " .:-=+*#%@";

inline void SetCellEmpty(TerminalCell &cell) noexcept {
  cell.utf8[0] = ' ';
  cell.utf8[1] = 0;
  cell.utf8[2] = 0;
  cell.utf8[3] = 0;
  cell.utf8_len = 1;
  cell.braille_char = 0;
  cell.has_bg = false;
  cell.fg_color = {0, 0, 0, 255};
  cell.bg_color = {0, 0, 0, 255};
}

inline void EncodeSextantMask(uint32_t mask, char out_utf8[4],
                              uint8_t &out_len) noexcept {
  mask &= 0x3Fu;
  if (mask == 0) {
    out_utf8[0] = ' ';
    out_len = 1;
  } else if (mask == 21u) {
    // 0b010101 -> Left Half Block U+258C
    out_utf8[0] = static_cast<char>(0xE2);
    out_utf8[1] = static_cast<char>(0x96);
    out_utf8[2] = static_cast<char>(0x8C);
    out_len = 3;
  } else if (mask == 42u) {
    // 0b101010 -> Right Half Block U+2590
    out_utf8[0] = static_cast<char>(0xE2);
    out_utf8[1] = static_cast<char>(0x96);
    out_utf8[2] = static_cast<char>(0x90);
    out_len = 3;
  } else if (mask == 63u) {
    // 0b111111 -> Full Block U+2588
    out_utf8[0] = static_cast<char>(0xE2);
    out_utf8[1] = static_cast<char>(0x96);
    out_utf8[2] = static_cast<char>(0x88);
    out_len = 3;
  } else {
    const uint32_t cp =
        0x1FB00u + mask - 1u - (mask > 21u ? 1u : 0u) - (mask > 42u ? 1u : 0u);
    out_utf8[0] = static_cast<char>(0xF0u | ((cp >> 18) & 0x07u));
    out_utf8[1] = static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu));
    out_utf8[2] = static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu));
    out_utf8[3] = static_cast<char>(0x80u | (cp & 0x3Fu));
    out_len = 4;
  }
}

} // namespace

TerminalPresenter::TerminalPresenter(bool headless)
    : m_Headless(headless), m_CurrentForeground{0, 0, 0, 255} {
  if (!m_Headless && !pal::SetupTerminal())
    m_Headless = true;
  BuildTables();
}

TerminalPresenter::~TerminalPresenter() {
  if (!m_Headless)
    pal::RestoreTerminal();
}

void TerminalPresenter::BuildTables() {
  for (int v = 0; v < 256; ++v) {
    const int u = 0x2800 + v;
    m_BrailleMap[v][0] = static_cast<char>(0xE2);
    m_BrailleMap[v][1] = static_cast<char>(0xA0 | ((u >> 6) & 0x03));
    m_BrailleMap[v][2] = static_cast<char>(0x80 | (u & 0x3F));

    auto [ptr, ec] =
        std::to_chars(m_ByteToDecStr[v], m_ByteToDecStr[v] + 4, v);
    m_ByteToDecLen[v] = static_cast<int>(ptr - m_ByteToDecStr[v]);
  }
}

void TerminalPresenter::InvalidateFrontBuffer() {
  TerminalCell sentinel{};
  sentinel.utf8[0] = '\x01';
  sentinel.utf8_len = 1;
  sentinel.braille_char = 0xFF;
  sentinel.fg_color = {0, 0, 0, 0};
  m_FrontBuffer.assign(static_cast<size_t>(m_TermW * m_TermH), sentinel);
  m_NeedsFullClear = true;
}

void TerminalPresenter::ResizeBuffers(int nw, int nh) {
  if (nw <= 0 || nh <= 0 || (nw == m_TermW && nh == m_TermH))
    return;
  m_TermW = nw;
  m_TermH = nh;
  TerminalCell empty{};
  SetCellEmpty(empty);
  m_BackBuffer.assign(static_cast<size_t>(nw * nh), empty);
  InvalidateFrontBuffer();
  m_CurrentForeground = {0, 0, 0, 255};
  m_FinalOutput.reserve(static_cast<size_t>(nw * nh * 16));
}

bool TerminalPresenter::TryUpdateTerminalSize(int fw, int fh) {
  if (m_Headless) {
    ResizeBuffers(fw, fh);
    return false;
  }
  int nw = 0, nh = 0;
  if (pal::TryGetTerminalSize(nw, nh)) {
    if (nw != m_TermW || nh != m_TermH) {
      ResizeBuffers(nw, nh);
      return true;
    }
    return false;
  }
  if (m_TermW == 0) {
    ResizeBuffers(fw, fh);
    return true;
  }
  return false;
}

void TerminalPresenter::SampleBrailleCell(int r0, int r1, int r2, int r3,
                                          const Pixel *framebuffer,
                                          const float *zbuffer,
                                          TerminalCell &out_cell) const {
  const int indices[8] = {r0,     r0 + 1, r1,     r1 + 1,
                          r2,     r2 + 1, r3,     r3 + 1};
  const uint32_t bits[8] = {0x01, 0x08, 0x02, 0x10, 0x04, 0x20, 0x40, 0x80};

  uint32_t mask = 0;
  uint32_t r_sum = 0, g_sum = 0, b_sum = 0, count = 0;

  for (int i = 0; i < 8; ++i) {
    const int idx = indices[i];
    if (zbuffer[idx] > 0.0f) {
      mask |= bits[i];
      r_sum += framebuffer[idx].r;
      g_sum += framebuffer[idx].g;
      b_sum += framebuffer[idx].b;
      ++count;
    }
  }

  if (count == 0) {
    SetCellEmpty(out_cell);
    return;
  }

  if (count < 8) {
    const uint32_t inv = kInvCount[count];
    out_cell.braille_char = static_cast<uint8_t>(mask);
    std::memcpy(out_cell.utf8, m_BrailleMap[out_cell.braille_char].data(), 3);
    out_cell.utf8_len = 3;
    out_cell.has_bg = false;
    out_cell.fg_color = {static_cast<uint8_t>((r_sum * inv) >> 16),
                         static_cast<uint8_t>((g_sum * inv) >> 16),
                         static_cast<uint8_t>((b_sum * inv) >> 16), 255};
    out_cell.bg_color = {0, 0, 0, 255};
    return;
  }

  // All 8 sub-pixels hit: 2-color FG + BG luminance clustering (eliminates black pinholes)
  uint32_t lum[8];
  uint32_t min_y = 255, max_y = 0;
  for (int i = 0; i < 8; ++i) {
    const Pixel &p = framebuffer[indices[i]];
    const uint32_t y = (54u * p.r + 183u * p.g + 19u * p.b) >> 8;
    lum[i] = y;
    if (y < min_y)
      min_y = y;
    if (y > max_y)
      max_y = y;
  }

  if (max_y - min_y < 4) {
    const Color avg = {static_cast<uint8_t>((r_sum * kInvCount[8]) >> 16),
                       static_cast<uint8_t>((g_sum * kInvCount[8]) >> 16),
                       static_cast<uint8_t>((b_sum * kInvCount[8]) >> 16), 255};
    out_cell.braille_char = 0xFF;
    std::memcpy(out_cell.utf8, m_BrailleMap[0xFF].data(), 3);
    out_cell.utf8_len = 3;
    out_cell.has_bg = true;
    out_cell.fg_color = avg;
    out_cell.bg_color = avg;
    return;
  }

  const uint32_t mid_y = (min_y + max_y) >> 1;
  uint32_t fg_mask = 0;
  uint32_t fg_r = 0, fg_g = 0, fg_b = 0, fg_cnt = 0;
  uint32_t bg_r = 0, bg_g = 0, bg_b = 0, bg_cnt = 0;

  for (int i = 0; i < 8; ++i) {
    const Pixel &p = framebuffer[indices[i]];
    if (lum[i] >= mid_y) {
      fg_mask |= bits[i];
      fg_r += p.r;
      fg_g += p.g;
      fg_b += p.b;
      ++fg_cnt;
    } else {
      bg_r += p.r;
      bg_g += p.g;
      bg_b += p.b;
      ++bg_cnt;
    }
  }

  out_cell.braille_char = static_cast<uint8_t>(fg_mask);
  std::memcpy(out_cell.utf8, m_BrailleMap[out_cell.braille_char].data(), 3);
  out_cell.utf8_len = 3;
  out_cell.has_bg = true;

  const uint32_t fg_inv = kInvCount[fg_cnt];
  out_cell.fg_color = {static_cast<uint8_t>((fg_r * fg_inv) >> 16),
                       static_cast<uint8_t>((fg_g * fg_inv) >> 16),
                       static_cast<uint8_t>((fg_b * fg_inv) >> 16), 255};

  if (bg_cnt > 0) {
    const uint32_t bg_inv = kInvCount[bg_cnt];
    out_cell.bg_color = {static_cast<uint8_t>((bg_r * bg_inv) >> 16),
                         static_cast<uint8_t>((bg_g * bg_inv) >> 16),
                         static_cast<uint8_t>((bg_b * bg_inv) >> 16), 255};
  } else {
    out_cell.bg_color = out_cell.fg_color;
  }
}

void TerminalPresenter::SampleHalfBlockCell(int r0, int r1, int r2, int r3,
                                            const Pixel *framebuffer,
                                            const float *zbuffer,
                                            TerminalCell &out_cell) const {
  const int top_idx[4] = {r0, r0 + 1, r1, r1 + 1};
  const int bot_idx[4] = {r2, r2 + 1, r3, r3 + 1};

  uint32_t tr = 0, tg = 0, tb = 0, tcnt = 0;
  uint32_t br = 0, bg = 0, bb = 0, bcnt = 0;

  for (int i = 0; i < 4; ++i) {
    if (zbuffer[top_idx[i]] > 0.0f) {
      const Pixel &p = framebuffer[top_idx[i]];
      tr += p.r;
      tg += p.g;
      tb += p.b;
      ++tcnt;
    }
    if (zbuffer[bot_idx[i]] > 0.0f) {
      const Pixel &p = framebuffer[bot_idx[i]];
      br += p.r;
      bg += p.g;
      bb += p.b;
      ++bcnt;
    }
  }

  if (tcnt == 0 && bcnt == 0) {
    SetCellEmpty(out_cell);
    return;
  }

  Color top_col = {0, 0, 0, 255};
  Color bot_col = {0, 0, 0, 255};
  if (tcnt > 0) {
    const uint32_t inv = kInvCount[tcnt];
    top_col = {static_cast<uint8_t>((tr * inv) >> 16),
               static_cast<uint8_t>((tg * inv) >> 16),
               static_cast<uint8_t>((tb * inv) >> 16), 255};
  }
  if (bcnt > 0) {
    const uint32_t inv = kInvCount[bcnt];
    bot_col = {static_cast<uint8_t>((br * inv) >> 16),
               static_cast<uint8_t>((bg * inv) >> 16),
               static_cast<uint8_t>((bb * inv) >> 16), 255};
  }

  if (tcnt > 0 && bcnt > 0) {
    // Upper Half Block U+2580 with FG = top, BG = bottom
    out_cell.utf8[0] = static_cast<char>(0xE2);
    out_cell.utf8[1] = static_cast<char>(0x96);
    out_cell.utf8[2] = static_cast<char>(0x80);
    out_cell.utf8_len = 3;
    out_cell.braille_char = 0x03;
    out_cell.has_bg = true;
    out_cell.fg_color = top_col;
    out_cell.bg_color = bot_col;
  } else if (tcnt > 0) {
    out_cell.utf8[0] = static_cast<char>(0xE2);
    out_cell.utf8[1] = static_cast<char>(0x96);
    out_cell.utf8[2] = static_cast<char>(0x80);
    out_cell.utf8_len = 3;
    out_cell.braille_char = 0x01;
    out_cell.has_bg = false;
    out_cell.fg_color = top_col;
    out_cell.bg_color = {0, 0, 0, 255};
  } else {
    // Lower Half Block U+2584
    out_cell.utf8[0] = static_cast<char>(0xE2);
    out_cell.utf8[1] = static_cast<char>(0x96);
    out_cell.utf8[2] = static_cast<char>(0x84);
    out_cell.utf8_len = 3;
    out_cell.braille_char = 0x02;
    out_cell.has_bg = false;
    out_cell.fg_color = bot_col;
    out_cell.bg_color = {0, 0, 0, 255};
  }
}

void TerminalPresenter::SampleQuadrantCell(int r0, int r1, int r2, int r3,
                                           const Pixel *framebuffer,
                                           const float *zbuffer,
                                           TerminalCell &out_cell) const {
  const int q_idx[4][2] = {
      {r0, r1},         // TL
      {r0 + 1, r1 + 1}, // TR
      {r2, r3},         // BL
      {r2 + 1, r3 + 1}  // BR
  };

  Color q_col[4]{};
  uint32_t mask = 0;
  uint32_t r_sum = 0, g_sum = 0, b_sum = 0, hit_cnt = 0;

  for (int q = 0; q < 4; ++q) {
    uint32_t qr = 0, qg = 0, qb = 0, qc = 0;
    for (int k = 0; k < 2; ++k) {
      const int idx = q_idx[q][k];
      if (zbuffer[idx] > 0.0f) {
        qr += framebuffer[idx].r;
        qg += framebuffer[idx].g;
        qb += framebuffer[idx].b;
        ++qc;
      }
    }
    if (qc > 0) {
      mask |= (1u << q);
      q_col[q] = {static_cast<uint8_t>(qr / qc), static_cast<uint8_t>(qg / qc),
                  static_cast<uint8_t>(qb / qc), 255};
      r_sum += q_col[q].r;
      g_sum += q_col[q].g;
      b_sum += q_col[q].b;
      ++hit_cnt;
    }
  }

  if (hit_cnt == 0) {
    SetCellEmpty(out_cell);
    return;
  }

  if (hit_cnt < 4) {
    const char *glyph = kQuadrantGlyphs[mask];
    std::memcpy(out_cell.utf8, glyph, 3);
    out_cell.utf8_len = 3;
    out_cell.braille_char = static_cast<uint8_t>(mask);
    out_cell.has_bg = false;
    out_cell.fg_color = {static_cast<uint8_t>(r_sum / hit_cnt),
                         static_cast<uint8_t>(g_sum / hit_cnt),
                         static_cast<uint8_t>(b_sum / hit_cnt), 255};
    out_cell.bg_color = {0, 0, 0, 255};
    return;
  }

  // All 4 quadrants hit: cluster into 2 colors
  uint32_t lum[4], min_y = 255, max_y = 0;
  for (int q = 0; q < 4; ++q) {
    lum[q] = q_col[q].Luminance();
    min_y = std::min(min_y, lum[q]);
    max_y = std::max(max_y, lum[q]);
  }
  if (max_y - min_y < 4) {
    std::memcpy(out_cell.utf8, kQuadrantGlyphs[15], 3);
    out_cell.utf8_len = 3;
    out_cell.braille_char = 15;
    out_cell.has_bg = true;
    out_cell.fg_color = {static_cast<uint8_t>(r_sum >> 2),
                         static_cast<uint8_t>(g_sum >> 2),
                         static_cast<uint8_t>(b_sum >> 2), 255};
    out_cell.bg_color = out_cell.fg_color;
    return;
  }

  const uint32_t mid_y = (min_y + max_y) >> 1;
  uint32_t fg_mask = 0;
  uint32_t fg_r = 0, fg_g = 0, fg_b = 0, fg_c = 0;
  uint32_t bg_r = 0, bg_g = 0, bg_b = 0, bg_c = 0;
  for (int q = 0; q < 4; ++q) {
    if (lum[q] >= mid_y) {
      fg_mask |= (1u << q);
      fg_r += q_col[q].r;
      fg_g += q_col[q].g;
      fg_b += q_col[q].b;
      ++fg_c;
    } else {
      bg_r += q_col[q].r;
      bg_g += q_col[q].g;
      bg_b += q_col[q].b;
      ++bg_c;
    }
  }
  if (fg_mask == 0)
    fg_mask = 15;
  std::memcpy(out_cell.utf8, kQuadrantGlyphs[fg_mask], 3);
  out_cell.utf8_len = 3;
  out_cell.braille_char = static_cast<uint8_t>(fg_mask);
  out_cell.has_bg = true;
  out_cell.fg_color = {static_cast<uint8_t>(fg_r / std::max(1u, fg_c)),
                       static_cast<uint8_t>(fg_g / std::max(1u, fg_c)),
                       static_cast<uint8_t>(fg_b / std::max(1u, fg_c)), 255};
  out_cell.bg_color =
      (bg_c > 0) ? Color{static_cast<uint8_t>(bg_r / bg_c),
                         static_cast<uint8_t>(bg_g / bg_c),
                         static_cast<uint8_t>(bg_b / bg_c), 255}
                 : out_cell.fg_color;
}

void TerminalPresenter::SampleSextantCell(int r0, int r1, int r2, int r3,
                                          const Pixel *framebuffer,
                                          const float *zbuffer,
                                          TerminalCell &out_cell) const {
  // 2x3 sextant sub-blocks mapped from the 2x4 sub-pixel cell:
  // row 0: r0, r0+1; row 1: r1/r2, r1+1/r2+1; row 2: r3, r3+1
  const int s_idx[6] = {
      r0,
      r0 + 1,
      (zbuffer[r1] > 0.0f) ? r1 : r2,
      (zbuffer[r1 + 1] > 0.0f) ? (r1 + 1) : (r2 + 1),
      r3,
      r3 + 1,
  };

  uint32_t mask = 0;
  uint32_t r_sum = 0, g_sum = 0, b_sum = 0, count = 0;

  for (int i = 0; i < 6; ++i) {
    const int idx = s_idx[i];
    if (zbuffer[idx] > 0.0f) {
      mask |= (1u << i);
      r_sum += framebuffer[idx].r;
      g_sum += framebuffer[idx].g;
      b_sum += framebuffer[idx].b;
      ++count;
    }
  }

  if (count == 0) {
    SetCellEmpty(out_cell);
    return;
  }

  if (count < 6) {
    const uint32_t inv = kInvCount[count];
    EncodeSextantMask(mask, out_cell.utf8, out_cell.utf8_len);
    out_cell.braille_char = static_cast<uint8_t>(mask);
    out_cell.has_bg = false;
    out_cell.fg_color = {static_cast<uint8_t>((r_sum * inv) >> 16),
                         static_cast<uint8_t>((g_sum * inv) >> 16),
                         static_cast<uint8_t>((b_sum * inv) >> 16), 255};
    out_cell.bg_color = {0, 0, 0, 255};
    return;
  }

  uint32_t lum[6], min_y = 255, max_y = 0;
  for (int i = 0; i < 6; ++i) {
    const Pixel &p = framebuffer[s_idx[i]];
    lum[i] = (54u * p.r + 183u * p.g + 19u * p.b) >> 8;
    min_y = std::min(min_y, lum[i]);
    max_y = std::max(max_y, lum[i]);
  }

  if (max_y - min_y < 4) {
    const uint32_t inv = kInvCount[6];
    EncodeSextantMask(63u, out_cell.utf8, out_cell.utf8_len);
    out_cell.braille_char = 63;
    out_cell.has_bg = true;
    out_cell.fg_color = {static_cast<uint8_t>((r_sum * inv) >> 16),
                         static_cast<uint8_t>((g_sum * inv) >> 16),
                         static_cast<uint8_t>((b_sum * inv) >> 16), 255};
    out_cell.bg_color = out_cell.fg_color;
    return;
  }

  const uint32_t mid_y = (min_y + max_y) >> 1;
  uint32_t fg_mask = 0;
  uint32_t fg_r = 0, fg_g = 0, fg_b = 0, fg_c = 0;
  uint32_t bg_r = 0, bg_g = 0, bg_b = 0, bg_c = 0;
  for (int i = 0; i < 6; ++i) {
    const Pixel &p = framebuffer[s_idx[i]];
    if (lum[i] >= mid_y) {
      fg_mask |= (1u << i);
      fg_r += p.r;
      fg_g += p.g;
      fg_b += p.b;
      ++fg_c;
    } else {
      bg_r += p.r;
      bg_g += p.g;
      bg_b += p.b;
      ++bg_c;
    }
  }
  if (fg_mask == 0)
    fg_mask = 63u;
  EncodeSextantMask(fg_mask, out_cell.utf8, out_cell.utf8_len);
  out_cell.braille_char = static_cast<uint8_t>(fg_mask);
  out_cell.has_bg = true;
  const uint32_t fg_inv = kInvCount[std::max(1u, fg_c)];
  out_cell.fg_color = {static_cast<uint8_t>((fg_r * fg_inv) >> 16),
                       static_cast<uint8_t>((fg_g * fg_inv) >> 16),
                       static_cast<uint8_t>((fg_b * fg_inv) >> 16), 255};
  if (bg_c > 0) {
    const uint32_t bg_inv = kInvCount[bg_c];
    out_cell.bg_color = {static_cast<uint8_t>((bg_r * bg_inv) >> 16),
                         static_cast<uint8_t>((bg_g * bg_inv) >> 16),
                         static_cast<uint8_t>((bg_b * bg_inv) >> 16), 255};
  } else {
    out_cell.bg_color = out_cell.fg_color;
  }
}

void TerminalPresenter::SampleAsciiCell(int r0, int r1, int r2, int r3,
                                        const Pixel *framebuffer,
                                        const float *zbuffer,
                                        TerminalCell &out_cell) const {
  const int indices[8] = {r0,     r0 + 1, r1,     r1 + 1,
                          r2,     r2 + 1, r3,     r3 + 1};
  uint32_t r_sum = 0, g_sum = 0, b_sum = 0, count = 0;

  for (int i = 0; i < 8; ++i) {
    const int idx = indices[i];
    if (zbuffer[idx] > 0.0f) {
      r_sum += framebuffer[idx].r;
      g_sum += framebuffer[idx].g;
      b_sum += framebuffer[idx].b;
      ++count;
    }
  }

  if (count == 0) {
    SetCellEmpty(out_cell);
    return;
  }

  const uint32_t inv = kInvCount[count];
  const uint8_t r = static_cast<uint8_t>((r_sum * inv) >> 16);
  const uint8_t g = static_cast<uint8_t>((g_sum * inv) >> 16);
  const uint8_t b = static_cast<uint8_t>((b_sum * inv) >> 16);
  const uint32_t lum = (54u * r + 183u * g + 19u * b) >> 8;
  const int ramp_idx = std::clamp(static_cast<int>((lum * 9u) / 255u), 1, 9);

  out_cell.utf8[0] = kAsciiRamp[ramp_idx];
  out_cell.utf8[1] = 0;
  out_cell.utf8_len = 1;
  out_cell.braille_char = static_cast<uint8_t>(kAsciiRamp[ramp_idx]);
  out_cell.has_bg = false;
  out_cell.fg_color = {r, g, b, 255};
  out_cell.bg_color = {0, 0, 0, 255};
}

void TerminalPresenter::PreFrame(int num_workers) {
  if (m_ThreadOutputs.size() != static_cast<size_t>(num_workers)) {
    m_ThreadOutputs.resize(static_cast<size_t>(num_workers));
  }
  for (auto &s : m_ThreadOutputs) {
    s.clear();
    s.reserve(static_cast<size_t>(m_TermW * m_TermH * 2));
  }
}

void TerminalPresenter::SampleTileToBackBuffer(int tile_x, int tile_y,
                                               int term_w, int term_h,
                                               const Pixel *local_fb,
                                               const float *local_zb) {
  constexpr int TILE_SIZE = 64;

  const int start_tx = tile_x * (TILE_SIZE / 2);
  const int start_ty = tile_y * (TILE_SIZE / 4);
  const int end_tx = std::min(start_tx + (TILE_SIZE / 2), term_w);
  const int end_ty = std::min(start_ty + (TILE_SIZE / 4), term_h);

  if (start_tx >= term_w || start_ty >= term_h)
    return;

  for (int ty = start_ty; ty < end_ty; ++ty) {
    const int local_ty = ty - start_ty;
    const int row_base = local_ty * 4 * TILE_SIZE;

    for (int tx = start_tx; tx < end_tx; ++tx) {
      const int cell_idx = ty * m_TermW + tx;
      TerminalCell &back = m_BackBuffer[static_cast<size_t>(cell_idx)];

      const int local_tx = tx - start_tx;
      const int c_base = local_tx * 2;
      const int r0 = row_base + c_base;
      const int r1 = r0 + TILE_SIZE;
      const int r2 = r1 + TILE_SIZE;
      const int r3 = r2 + TILE_SIZE;

      const bool is_empty = (local_zb[r0] <= 0.0f) &
                            (local_zb[r0 + 1] <= 0.0f) &
                            (local_zb[r1] <= 0.0f) &
                            (local_zb[r1 + 1] <= 0.0f) &
                            (local_zb[r2] <= 0.0f) &
                            (local_zb[r2 + 1] <= 0.0f) &
                            (local_zb[r3] <= 0.0f) &
                            (local_zb[r3 + 1] <= 0.0f);

      if (is_empty) {
        SetCellEmpty(back);
      } else {
        switch (m_Blitter) {
        case BlitterType::HalfBlock:
          SampleHalfBlockCell(r0, r1, r2, r3, local_fb, local_zb, back);
          break;
        case BlitterType::Quadrant:
          SampleQuadrantCell(r0, r1, r2, r3, local_fb, local_zb, back);
          break;
        case BlitterType::Sextant:
          SampleSextantCell(r0, r1, r2, r3, local_fb, local_zb, back);
          break;
        case BlitterType::ASCII:
          SampleAsciiCell(r0, r1, r2, r3, local_fb, local_zb, back);
          break;
        case BlitterType::Braille:
        default:
          SampleBrailleCell(r0, r1, r2, r3, local_fb, local_zb, back);
          break;
        }
      }
    }
  }
}

void TerminalPresenter::BuildOutput(int worker_id, int start_y, int end_y,
                                    int term_w) {
  if (worker_id < 0 || worker_id >= static_cast<int>(m_ThreadOutputs.size()))
    return;
  std::string &local_out = m_ThreadOutputs[static_cast<size_t>(worker_id)];

  Color current_fg = {0, 0, 0, 255};
  Color current_bg = {0, 0, 0, 255};
  bool fg_valid = false;
  bool bg_valid = false;

  auto append_byte = [&](char *buf, int &len, uint8_t v) {
    std::memcpy(buf + len, m_ByteToDecStr[v],
                static_cast<size_t>(m_ByteToDecLen[v]));
    len += m_ByteToDecLen[v];
  };

  auto local_append_fg = [&](Color c) {
    if (fg_valid && c == current_fg)
      return;
    char buf[32];
    int len = 0;
    buf[len++] = '\x1b';
    buf[len++] = '[';
    buf[len++] = '3';
    buf[len++] = '8';
    buf[len++] = ';';
    buf[len++] = '2';
    buf[len++] = ';';
    append_byte(buf, len, c.r);
    buf[len++] = ';';
    append_byte(buf, len, c.g);
    buf[len++] = ';';
    append_byte(buf, len, c.b);
    buf[len++] = 'm';
    local_out.append(buf, static_cast<size_t>(len));
    current_fg = c;
    fg_valid = true;
  };

  auto local_append_bg = [&](bool has_bg, Color c) {
    if (!has_bg) {
      if (bg_valid) {
        local_out.append("\x1b[49m", 5);
        bg_valid = false;
      }
      return;
    }
    if (bg_valid && c == current_bg)
      return;
    char buf[32];
    int len = 0;
    buf[len++] = '\x1b';
    buf[len++] = '[';
    buf[len++] = '4';
    buf[len++] = '8';
    buf[len++] = ';';
    buf[len++] = '2';
    buf[len++] = ';';
    append_byte(buf, len, c.r);
    buf[len++] = ';';
    append_byte(buf, len, c.g);
    buf[len++] = ';';
    append_byte(buf, len, c.b);
    buf[len++] = 'm';
    local_out.append(buf, static_cast<size_t>(len));
    current_bg = c;
    bg_valid = true;
  };

  auto local_append_jump = [&](int cx, int cy) {
    char buf[32];
    int len = 0;
    buf[len++] = '\x1b';
    buf[len++] = '[';
    auto append_int = [&](int v) {
      if (v >= 0 && v < 256) {
        std::memcpy(buf + len, m_ByteToDecStr[v],
                    static_cast<size_t>(m_ByteToDecLen[v]));
        len += m_ByteToDecLen[v];
      } else {
        auto [ptr, ec] = std::to_chars(buf + len, buf + sizeof(buf), v);
        len = static_cast<int>(ptr - buf);
      }
    };
    append_int(cy + 1);
    buf[len++] = ';';
    append_int(cx + 1);
    buf[len++] = 'H';
    local_out.append(buf, static_cast<size_t>(len));
  };

  int cursor_x = -99, cursor_y = -99;
  for (int y = start_y; y < end_y; ++y) {
    for (int x = 0; x < term_w; ++x) {
      const size_t cell_idx = static_cast<size_t>(y * m_TermW + x);
      TerminalCell &front = m_FrontBuffer[cell_idx];
      const TerminalCell &back = m_BackBuffer[cell_idx];

      if (back != front) {
        if (y != cursor_y || x != cursor_x + 1) {
          local_append_jump(x, y);
        }

        if (back.utf8_len == 1 && back.utf8[0] == ' ' && !back.has_bg) {
          local_append_bg(false, {0, 0, 0, 255});
          local_out.push_back(' ');
        } else {
          local_append_fg(back.fg_color);
          local_append_bg(back.has_bg, back.bg_color);
          local_out.append(back.utf8, static_cast<size_t>(back.utf8_len));
        }
        front = back;
        cursor_x = x;
        cursor_y = y;
      }
    }
  }
  if (bg_valid) {
    local_out.append("\x1b[49m", 5);
  }
}

void TerminalPresenter::FlushOutput() {
  bool has_any_output = m_NeedsFullClear;
  size_t estimated = 64;
  if (m_NeedsFullClear)
    estimated += 4;
  for (const auto &s : m_ThreadOutputs) {
    estimated += s.size();
    if (!s.empty())
      has_any_output = true;
  }

  m_FinalOutput.clear();
  if (!has_any_output) {
    m_LastAnsiBytesEmitted = 0;
    return;
  }

  if (m_FinalOutput.capacity() < estimated) {
    m_FinalOutput.reserve(estimated);
  }

  // DECSET 2026 Synchronized Output Begin
  m_FinalOutput.append("\x1b[?2026h", 8);

  if (m_NeedsFullClear) {
    m_FinalOutput.append("\x1b[2J", 4);
    m_NeedsFullClear = false;
  }

  for (const auto &s : m_ThreadOutputs) {
    m_FinalOutput.append(s);
  }

  // Reset SGR and DECSET 2026 Synchronized Output End
  m_FinalOutput.append("\x1b[0m\x1b[?2026l", 12);
  m_CurrentForeground = {0, 0, 0, 255};
  m_LastAnsiBytesEmitted = static_cast<uint32_t>(m_FinalOutput.size());

  if (!m_Headless) {
    pal::FastPrint(m_FinalOutput.data(),
                   static_cast<unsigned int>(m_FinalOutput.size()));
  }
}

void TerminalPresenter::Present(const std::vector<Pixel> &,
                                const std::vector<float> &, int, int, int) {}

} // namespace tvk
