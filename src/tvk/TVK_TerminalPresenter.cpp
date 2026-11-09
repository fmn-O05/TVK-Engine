#include "tvk/TVK_TerminalPresenter.h"
#include "tvk/TVK_JobSystem.h"
#include "tvk/TVK_PAL.h"
#include <charconv>
#include <cstring>

namespace tvk {

TerminalPresenter::TerminalPresenter(bool headless)
    : m_Headless(headless), m_CurrentForeground{0, 0, 0, 255} {
  if (!m_Headless && !pal::SetupTerminal())
    m_Headless = true;
  BuildBrailleTable();
}

TerminalPresenter::~TerminalPresenter() {
  if (!m_Headless)
    pal::RestoreTerminal();
}

void TerminalPresenter::BuildBrailleTable() {
  for (int v = 0; v < 256; ++v) {
    const int u = 0x2800 + v;
    m_BrailleMap[v][0] = static_cast<char>(0xE2);
    m_BrailleMap[v][1] = static_cast<char>(0xA0 | ((u >> 6) & 0x03));
    m_BrailleMap[v][2] = static_cast<char>(0x80 | (u & 0x3F));
  }
}

void TerminalPresenter::InvalidateFrontBuffer() {
  m_FrontBuffer.assign(m_TermW * m_TermH, {0xFF, {0, 0, 0, 255}});
  m_NeedsFullClear = true;
}

void TerminalPresenter::ResizeBuffers(int nw, int nh) {
  if (nw <= 0 || nh <= 0 || (nw == m_TermW && nh == m_TermH))
    return;
  m_TermW = nw;
  m_TermH = nh;
  m_FrontBuffer.assign(nw * nh, {0, {0, 0, 0, 255}});
  m_BackBuffer.assign(nw * nh, {0, {0, 0, 0, 255}});
  InvalidateFrontBuffer();
  m_CurrentForeground = {0, 0, 0, 255};
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
                                          const Pixel* framebuffer,
                                          const float* zbuffer,
                                          uint8_t &out_braille,
                                          Color &out_color) {
  uint32_t braille_char = 0;
  uint32_t r_sum = 0, g_sum = 0, b_sum = 0, count = 0;

  auto mapPixel = [&](int idx, uint32_t bit) {
    uint32_t hit = (zbuffer[idx] > 0.0f) ? 1 : 0;
    braille_char |= bit * hit;
    r_sum += framebuffer[idx].r * hit;
    g_sum += framebuffer[idx].g * hit;
    b_sum += framebuffer[idx].b * hit;
    count += hit;
  };

  mapPixel(r0, 0x01);
  mapPixel(r0 + 1, 0x08);
  mapPixel(r1, 0x02);
  mapPixel(r1 + 1, 0x10);
  mapPixel(r2, 0x04);
  mapPixel(r2 + 1, 0x20);
  mapPixel(r3, 0x40);
  mapPixel(r3 + 1, 0x80);

  out_braille = static_cast<uint8_t>(braille_char);
  if (count == 0) {
    out_color = {0, 0, 0, 255};
  } else {

    static const uint32_t inv_cnt[9] = {0, 65536/1, 65536/2, 65536/3, 65536/4, 65536/5, 65536/6, 65536/7, 65536/8};
    uint32_t inv = inv_cnt[count];
    out_color = {static_cast<uint8_t>(((r_sum * inv) >> 16) & 0xE0),
                 static_cast<uint8_t>(((g_sum * inv) >> 16) & 0xE0),
                 static_cast<uint8_t>(((b_sum * inv) >> 16) & 0xE0), 255};
  }
}

void TerminalPresenter::PreFrame(int num_workers) {
  if (m_ThreadOutputs.size() != static_cast<size_t>(num_workers)) {
    m_ThreadOutputs.resize(num_workers);
  }
  for (auto& s : m_ThreadOutputs) {
    s.clear();

    s.reserve(m_TermW * m_TermH * 2);
  }
}

void TerminalPresenter::SampleTileToBackBuffer(int tile_x, int tile_y, int term_w, int term_h,
                                     const Pixel* local_fb, const float* local_zb) {

  const int TILE_SIZE = 64;

  int start_tx = tile_x * (TILE_SIZE / 2);
  int start_ty = tile_y * (TILE_SIZE / 4);
  int end_tx = std::min(start_tx + (TILE_SIZE / 2), term_w);
  int end_ty = std::min(start_ty + (TILE_SIZE / 4), term_h);

  if (start_tx >= term_w || start_ty >= term_h) return;

  for (int ty = start_ty; ty < end_ty; ++ty) {
    int local_ty = ty - start_ty;
    const int row_base = local_ty * 4 * TILE_SIZE;

    for (int tx = start_tx; tx < end_tx; ++tx) {
      const int cell_idx = ty * m_TermW + tx;
      TerminalCell &back = m_BackBuffer[cell_idx];

      int local_tx = tx - start_tx;
      const int c_base = local_tx * 2;
      const int r0 = row_base + c_base;
      const int r1 = r0 + TILE_SIZE;
      const int r2 = r1 + TILE_SIZE;
      const int r3 = r2 + TILE_SIZE;

      bool is_empty = (local_zb[r0] <= 0.0f) & (local_zb[r0 + 1] <= 0.0f) &
                      (local_zb[r1] <= 0.0f) & (local_zb[r1 + 1] <= 0.0f) &
                      (local_zb[r2] <= 0.0f) & (local_zb[r2 + 1] <= 0.0f) &
                      (local_zb[r3] <= 0.0f) & (local_zb[r3 + 1] <= 0.0f);

      if (is_empty) {
        back.braille_char = 0;
        back.fg_color = {0, 0, 0, 255};
      } else {
        SampleBrailleCell(r0, r1, r2, r3, local_fb, local_zb,
                          back.braille_char, back.fg_color);
      }
    }
  }
}

void TerminalPresenter::BuildOutput(int worker_id, int start_y, int end_y, int term_w) {
  if (worker_id < 0 || worker_id >= static_cast<int>(m_ThreadOutputs.size())) return;
  std::string &local_out = m_ThreadOutputs[worker_id];

  static char str_num[256][4];
  static int str_len[256];
  static bool init_lut = false;
  if (!init_lut) {
    for (int i = 0; i < 256; ++i) {
      auto [ptr, ec] = std::to_chars(str_num[i], str_num[i] + 4, i);
      str_len[i] = static_cast<int>(ptr - str_num[i]);
    }
    init_lut = true;
  }

  Color current_fg = {0, 0, 0, 255};
  bool force_color = true;

  auto local_append_color = [&](Color c) {
    if (!force_color &&
        std::abs(c.r - current_fg.r) < 15 &&
        std::abs(c.g - current_fg.g) < 15 &&
        std::abs(c.b - current_fg.b) < 15) {
      return;
    }
    char buf[32];
    int len = 0;
    buf[len++] = '\x1b'; buf[len++] = '[';
    buf[len++] = '3'; buf[len++] = '8'; buf[len++] = ';';
    buf[len++] = '2'; buf[len++] = ';';
    auto append_val = [&](uint8_t v) {
      std::memcpy(buf + len, str_num[v], str_len[v]);
      len += str_len[v];
    };
    append_val(c.r); buf[len++] = ';';
    append_val(c.g); buf[len++] = ';';
    append_val(c.b); buf[len++] = 'm';
    local_out.append(buf, len);
    current_fg = c;
    force_color = false;
  };

  auto local_append_jump = [&](int cx, int cy) {
    char buf[32];
    int len = 0;
    buf[len++] = '\x1b';
    buf[len++] = '[';
    auto append_int = [&](int v) {
      if (v < 256) {
        std::memcpy(buf + len, str_num[v], str_len[v]);
        len += str_len[v];
      } else {
         auto [ptr, ec] = std::to_chars(buf + len, buf + sizeof(buf), v);
         len = static_cast<int>(ptr - buf);
      }
    };
    append_int(cy + 1);
    buf[len++] = ';';
    append_int(cx + 1);
    buf[len++] = 'H';
    local_out.append(buf, len);
  };

  int cursor_x = -99, cursor_y = -99;
  for (int y = start_y; y < end_y; ++y) {
    for (int x = 0; x < term_w; ++x) {
      const int cell_idx = y * m_TermW + x;
      TerminalCell &front = m_FrontBuffer[cell_idx];
      const TerminalCell &back = m_BackBuffer[cell_idx];

      if (back != front) {
        if (y != cursor_y || x != cursor_x + 1) {
          local_append_jump(x, y);
        }

        if (back.braille_char == 0) {
          local_out.push_back(' ');
        } else {
          local_append_color(back.fg_color);
          local_out.append(m_BrailleMap[back.braille_char].data(), 3);
        }
        front = back;
        cursor_x = x;
        cursor_y = y;
      }
    }
  }
}

void TerminalPresenter::FlushOutput() {
  bool has_any_output = m_NeedsFullClear;
  size_t estimated = 1024;
  if (m_NeedsFullClear)
    estimated += 4;
  for (const auto &s : m_ThreadOutputs) {
    estimated += s.size();
    if (!s.empty())
      has_any_output = true;
  }

  if (!has_any_output) {
    return;
  }

  std::string final_out;
  final_out.reserve(estimated);

  if (m_NeedsFullClear) {
    final_out.append("\x1b[2J", 4);
    m_NeedsFullClear = false;
  }

  for (const auto &s : m_ThreadOutputs) {
    final_out.append(s);
  }

  final_out.append("\x1b[0m", 4);
  m_CurrentForeground = {0, 0, 0, 255};

  pal::FastPrint(final_out.data(), static_cast<unsigned int>(final_out.size()));
}

void TerminalPresenter::Present(const std::vector<Pixel> &, const std::vector<float> &, int, int, int) {

}

}
