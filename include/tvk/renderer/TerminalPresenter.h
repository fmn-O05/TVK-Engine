#pragma once
#include "tvk/core/Types.h"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace tvk {
class JobSystem;

struct TerminalCell {
  char utf8[4] = {' ', 0, 0, 0};
  uint8_t utf8_len = 1;
  uint8_t braille_char = 0;
  bool has_bg = false;
  Color fg_color = {0, 0, 0, 255};
  Color bg_color = {0, 0, 0, 255};

  bool operator==(const TerminalCell &o) const noexcept {
    if (utf8_len != o.utf8_len || braille_char != o.braille_char ||
        has_bg != o.has_bg || fg_color != o.fg_color) {
      return false;
    }
    if (has_bg && bg_color != o.bg_color) {
      return false;
    }
    for (uint8_t i = 0; i < utf8_len; ++i) {
      if (utf8[i] != o.utf8[i])
        return false;
    }
    return true;
  }

  bool operator!=(const TerminalCell &o) const noexcept {
    return !(*this == o);
  }
};

struct IPresenter {
  virtual void ResizeBuffers(int new_w, int new_h) = 0;
  virtual bool TryUpdateTerminalSize(int fallback_w, int fallback_h) = 0;
  virtual void Present(const std::vector<Pixel> &framebuffer,
                       const std::vector<float> &zbuffer, int pix_w, int term_w,
                       int term_h) = 0;
  virtual void PreFrame(int num_workers) = 0;
  virtual void SampleTileToBackBuffer(int tile_x, int tile_y, int term_w,
                                      int term_h, const Pixel *local_fb,
                                      const float *local_zb) = 0;
  virtual void BuildOutput(int worker_id, int start_y, int end_y,
                           int term_w) = 0;
  virtual void FlushOutput() = 0;
  virtual void InvalidateFrontBuffer() = 0;
  virtual void SetDirtyTiles(const std::vector<bool> &tile_is_dirty, int tile_w,
                             int tile_h, int num_tiles_x) {
    (void)tile_is_dirty;
    (void)tile_w;
    (void)tile_h;
    (void)num_tiles_x;
  }
  virtual void SetHudRows(int n) { (void)n; }
  virtual void SetBlitter(BlitterType type) = 0;
  virtual BlitterType GetActiveBlitter() const = 0;
  virtual uint32_t GetLastAnsiBytesEmitted() const { return 0; }
  virtual const std::vector<TerminalCell> &GetBackBuffer() const = 0;
  virtual const std::string &GetLastOutput() const = 0;
  virtual void SetJobSystem(JobSystem *js) { (void)js; }
  virtual ~IPresenter() = default;
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

  void PreFrame(int num_workers) override;
  void SampleTileToBackBuffer(int tile_x, int tile_y, int term_w, int term_h,
                              const Pixel *local_fb,
                              const float *local_zb) override;
  void BuildOutput(int worker_id, int start_y, int end_y, int term_w) override;
  void FlushOutput() override;

  void InvalidateFrontBuffer() override;

  void SetHudRows(int n) override {
    if (m_HudRows != n) {
      m_HudRows = n;
      m_NeedsFullClear = true;
    }
  }

  void SetBlitter(BlitterType type) override {
    if (m_Blitter != type) {
      m_Blitter = type;
      InvalidateFrontBuffer();
    }
  }

  BlitterType GetActiveBlitter() const override { return m_Blitter; }

  uint32_t GetLastAnsiBytesEmitted() const override {
    return m_LastAnsiBytesEmitted;
  }

  const std::vector<TerminalCell> &GetBackBuffer() const override {
    return m_BackBuffer;
  }

  const std::string &GetLastOutput() const override { return m_FinalOutput; }

  void SetJobSystem(JobSystem *js) override { m_JobSystem = js; }

private:
  void BuildTables();
  void SampleBrailleCell(int r0, int r1, int r2, int r3,
                         const Pixel *framebuffer, const float *zbuffer,
                         TerminalCell &out_cell) const;
  void SampleHalfBlockCell(int r0, int r1, int r2, int r3,
                           const Pixel *framebuffer, const float *zbuffer,
                           TerminalCell &out_cell) const;
  void SampleQuadrantCell(int r0, int r1, int r2, int r3,
                          const Pixel *framebuffer, const float *zbuffer,
                          TerminalCell &out_cell) const;
  void SampleSextantCell(int r0, int r1, int r2, int r3,
                         const Pixel *framebuffer, const float *zbuffer,
                         TerminalCell &out_cell) const;
  void SampleAsciiCell(int r0, int r1, int r2, int r3,
                       const Pixel *framebuffer, const float *zbuffer,
                       TerminalCell &out_cell) const;

  bool m_Headless;
  BlitterType m_Blitter = BlitterType::Braille;
  int m_TermW = 0;
  int m_TermH = 0;
  int m_HudRows = 0;
  std::vector<TerminalCell> m_FrontBuffer;
  std::vector<TerminalCell> m_BackBuffer;

  std::vector<std::string> m_ThreadOutputs;
  std::string m_FinalOutput;
  uint32_t m_LastAnsiBytesEmitted = 0;

  Color m_CurrentForeground;
  std::array<std::array<char, 3>, 256> m_BrailleMap{};
  char m_ByteToDecStr[256][4]{};
  int m_ByteToDecLen[256]{};
  bool m_NeedsFullClear = false;
  JobSystem *m_JobSystem = nullptr;
};

} // namespace tvk
