#pragma once
#include "tvk/core/Types.h"
#include <array>
#include <string>
#include <vector>

namespace tvk {
class JobSystem;

struct IPresenter {
  virtual void ResizeBuffers(int new_w, int new_h) = 0;
  virtual bool TryUpdateTerminalSize(int fallback_w, int fallback_h) = 0;
  virtual void Present(const std::vector<Pixel> &framebuffer,
                       const std::vector<float> &zbuffer, int pix_w, int term_w,
                       int term_h) = 0;
  virtual void PreFrame(int num_workers) = 0;
  virtual void SampleTileToBackBuffer(int tile_x, int tile_y, int term_w, int term_h,
                                      const Pixel* local_fb, const float* local_zb) = 0;
  virtual void BuildOutput(int worker_id, int start_y, int end_y, int term_w) = 0;
  virtual void FlushOutput() = 0;
  virtual void InvalidateFrontBuffer() = 0;
  virtual void SetDirtyTiles(const std::vector<bool>& tile_is_dirty, int tile_w, int tile_h, int num_tiles_x) {
      (void)tile_is_dirty; (void)tile_w; (void)tile_h; (void)num_tiles_x;
  }
  virtual void SetHudRows(int n) { (void)n; }
  virtual BlitterType GetActiveBlitter() const = 0;
  virtual void SetJobSystem(JobSystem *js) { (void)js; }
  virtual ~IPresenter() = default;
};

struct TerminalCell {
  uint8_t braille_char;
  Color fg_color;
  bool operator!=(const TerminalCell &o) const {
    return braille_char != o.braille_char || fg_color.r != o.fg_color.r ||
           fg_color.g != o.fg_color.g || fg_color.b != o.fg_color.b;
  }
};

class TerminalPresenter : public IPresenter {
public:
  explicit TerminalPresenter(bool headless);
  ~TerminalPresenter() override;

  void ResizeBuffers(int new_w, int new_h) override;
  bool TryUpdateTerminalSize(int fw, int fh) override;

  void Present(const std::vector<Pixel> &framebuffer,
               const std::vector<float> &zbuffer, int pix_w, int term_w,
               int term_h) override;

  void PreFrame(int num_workers);
  void SampleTileToBackBuffer(int tile_x, int tile_y, int term_w, int term_h,
                              const Pixel* local_fb, const float* local_zb);
  void BuildOutput(int worker_id, int start_y, int end_y, int term_w);
  void FlushOutput();

  void InvalidateFrontBuffer() override;

  void SetHudRows(int n) override {
    if (m_HudRows != n) {
      m_HudRows = n;
      m_NeedsFullClear = true;
    }
  }

  BlitterType GetActiveBlitter() const override {
    return BlitterType::BrailleCustom;
  }

  void SetJobSystem(JobSystem *js) override { m_JobSystem = js; }

private:
  void BuildBrailleTable();
  void SampleBrailleCell(int r0, int r1, int r2, int r3,
                         const Pixel* framebuffer,
                         const float* zbuffer,
                         uint8_t &out_braille, Color &out_color);

  bool m_Headless;
  int m_TermW = 0;
  int m_TermH = 0;
  int m_HudRows = 0;
  std::vector<TerminalCell> m_FrontBuffer;
  std::vector<TerminalCell> m_BackBuffer;

  std::vector<std::string> m_ThreadOutputs;

  Color m_CurrentForeground;
  std::array<std::array<char, 3>, 256> m_BrailleMap{};
  bool m_NeedsFullClear = false;
  JobSystem *m_JobSystem = nullptr;
};

}
