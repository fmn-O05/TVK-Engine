# Specification 03 — Multi-Blitter Terminal Presenter, Differential ANSI Encoder, `Canvas2D` & `DebugOverlay`

- **Layers Covered**: Layer 3 (`tvk/renderer/TerminalPresenter.h`), Layer 6 (`tvk/ui/Canvas2D.h`), Layer 9 (`tvk/app/DebugOverlay.h`)
- **Target Roadmap Phases**: **Baseline Foundation (M1 — 5 Blitters + `DECSET 2026` Active)** & **Phase 3 (`Canvas2D`, Bayer Dithering, OKLab LUT & `F1` Profiler HUD)**
- **Resolved Baseline Defects**: `B-11`, `B-12`, `B-27`, `B-28`, `B-30`
- **Industry Benchmark References**: Notcurses & Chafa (2-color FG+BG sub-pixel block/Braille clustering, OKLab perceptual quantization), Kitty / WezTerm / Ghostty (`DECSET 2026` Synchronized Output `\x1b[?2026h`..`\x1b[?2026l`).

---

## 1. Unified `TerminalCell` Memory Layout (`include/tvk/renderer/TerminalPresenter.h`)

To support both 3D dual-color sub-pixel blitting (`fg` + `bg` + Unicode glyph) and 2D/TUI text overlays in a single unified cell grid, `TerminalCell` is an aligned 16-byte structure (`sizeof(TerminalCell) == 16`, `alignof(TerminalCell) == 16`) so that 4 cells pack into a single 64-byte CPU cache line:

```cpp
enum CellFlags : uint8_t {
    CellFlag_None      = 0,
    CellFlag_HasBg     = 1 << 0, // Emit 24-bit background (\x1b[48;2;R;G;Bm); otherwise \x1b[49m default bg
    CellFlag_Bold      = 1 << 1, // Emit \x1b[1m
    CellFlag_Underline = 1 << 2, // Emit \x1b[4m
    CellFlag_UIOverlay = 1 << 3  // Written by Canvas2D / UIContext after 3D tile blitting
};

struct alignas(16) TerminalCell {
    char     utf8[4]  = {' ', '\0', '\0', '\0'}; // 4B: UTF-8 sequence (ASCII, Braille, Block, Sextant, or UI glyph)
    uint8_t  utf8_len = 1;                       // 1B: Byte length of utf8[] (1..4)
    uint8_t  flags    = CellFlag_None;           // 1B: CellFlags bitmask
    uint16_t reserved = 0;                       // 2B: Alignment padding
    Color    fg       = {255, 255, 255, 255};    // 4B: 24-bit foreground RGBA
    Color    bg       = {0, 0, 0, 255};          // 4B: 24-bit background RGBA
};
static_assert(sizeof(TerminalCell) == 16, "TerminalCell must be 16 bytes");
```

*(Note: In the M1 baseline `TerminalPresenter`, `TerminalCell` stores `fg_color`, `bg_color`, `has_bg`, `glyph` / `braille_char` with exact equality `operator==` so front-buffer diffing tracks both foreground and background colors without drift.)*

---

## 2. Five Runtime-Switchable Sub-Pixel Blitter Algorithms (`BlitterType`)

### 2.1 Sub-Pixel Grid & Cell-Aligned Tile Table (`B-28` Fix)

To prevent multi-threaded data races where a terminal cell straddles two vertical tiles (such as `Sextant` $2\times 3$ dividing a $64$-pixel tile at $64 / 3 = 21.33$), tile dimensions or sub-pixel buffers are aligned to exact multiples of the active blitter's sub-pixel cell dimensions $(S_x, S_y)$:

| `BlitterType` | Enum Value | Sub-Pixels / Cell $(S_x \times S_y)$ | Unicode Block Range | UTF-8 Bytes / Cell | Effective Resolution at `160×50` Cells |
| :--- | :---: | :---: | :--- | :---: | :---: |
| **`Braille`** | `0` | $2 \times 4$ (`8` sub-pixels) | `U+2800` – `U+28FF` (`⠀`–`⣿`) | `3 B` (`0xE2 0xA0..0xA3 0x80..0xBF`) | `320 × 200` |
| **`HalfBlock`** | `1` | $1 \times 2$ (`2` sub-pixels) | `U+2580` (`▀`) / `' '` | `3 B` (`\xE2\x96\x80`) or `1 B` | `160 × 100` |
| **`Quadrant`** | `2` | $2 \times 2$ (`4` sub-pixels) | `U+2580` – `U+259F` (`▀`..`▟`) | `3 B` (`0xE2 0x96 0x80..0x9F`) | `320 × 100` |
| **`Sextant`** | `3` | $2 \times 3$ (`6` sub-pixels) | `U+1FB00` – `U+1FB3B` | `4 B` (`0xF0 0x9F 0xAC 0x80..0xBB`) | `320 × 150` |
| **`ASCII`** | `4` | $1 \times 1$ (`1` cell) | `U+0020` – `U+007E` (`" .:-=+*#%@"`) | `1 B` | `160 × 50` |

---

### 2.2 `Braille` ($2\times 4$) 24-Bit 2-Color FG+BG Luminance Clustering (`B-12` Fix)

Standard Unicode Braille (`U+2800 + mask`) numbers the 8 dots in a $2\times 4$ grid as:

```text
  Col 0 (dx=0)   Col 1 (dx=1)
┌──────────────┬──────────────┐
│ Dot 1 (0x01) │ Dot 4 (0x08) │ Row 0 (dy=0)
├──────────────┼──────────────┤
│ Dot 2 (0x02) │ Dot 5 (0x10) │ Row 1 (dy=1)
├──────────────┼──────────────┤
│ Dot 3 (0x04) │ Dot 6 (0x20) │ Row 2 (dy=2)
├──────────────┼──────────────┤
│ Dot 7 (0x40) │ Dot 8 (0x80) │ Row 3 (dy=3)
└──────────────┴──────────────┘
```

The legacy presenter only emitted foreground color over a black background (`\x1b[49m`) and truncated RGB with `& 0xE0`, causing solid 3D surfaces to appear hollow with black pinholes between dots. The upgraded **2-Color Luminance Clustering** algorithm handles all 3 coverage regimes:

1. **Regime A — Empty Cell ($\text{hitCount} == 0$)**:
   - All 8 sub-pixels have `local_zb <= 0.0f`.
   - Output: `glyph = ' '`, `has_bg = false` (`\x1b[49m`).
2. **Regime B — Partial Silhouette Edge ($1 \le \text{hitCount} \le 7$)**:
   - Set Braille bit $k$ for each sub-pixel $k \in [0, 7]$ with `local_zb > 0.0f`.
   - `fg_color` = full 24-bit average RGB of the hit sub-pixels (no `& 0xE0` truncation!).
   - `has_bg = false` (background remains terminal default so object silhouettes against the sky remain crisp at $2\times 4$ resolution).
3. **Regime C — Solid Interior Surface ($\text{hitCount} == 8$, Zero Black Pinholes)**:
   - Compute Rec.709 integer luminance for each of the 8 sub-pixels:
     $$Y_k = (54 R_k + 183 G_k + 19 B_k) \gg 8 \in [0, 255]$$
   - Find $Y_{\min} = \min_k Y_k$ and $Y_{\max} = \max_k Y_k$.
   - **Flat-Color Fast Path ($Y_{\max} - Y_{\min} \le 8$)**:
     - All 8 sub-pixels have nearly uniform color $\overline{C}$.
     - Emit solid Braille `U+28FF` (`⣿`) with `fg_color = bg_color = \overline{C}`, `has_bg = true` (or `' '` with `bg_color = \overline{C}`). This completely eliminates black pinholes between Braille dots on solid surfaces!
   - **Bimodal Edge/Detail Path ($Y_{\max} - Y_{\min} > 8$)**:
     - Partition the 8 sub-pixels at threshold $Y_{\text{mid}} = (Y_{\min} + Y_{\max}) / 2$:
       - Sub-pixels with $Y_k > Y_{\text{mid}}$ set Braille bit $k = 1$ and accumulate into `fg_color` ($\overline{C}_{\text{light}}$).
       - Sub-pixels with $Y_k \le Y_{\text{mid}}$ leave Braille bit $k = 0$ and accumulate into `bg_color` ($\overline{C}_{\text{dark}}$), setting `has_bg = true`.

---

### 2.3 `HalfBlock` ($1\times 2$), `Quadrant` ($2\times 2$), `Sextant` ($2\times 3$) & `ASCII` ($1\times 1$) Blitters

1. **`HalfBlock` ($1\times 2$, `▀` `U+2580`)**:
   - Samples top sub-pixel $C_{\text{top}}$ and bottom sub-pixel $C_{\text{bot}}$.
   - Emits `▀` (`\xE2\x96\x80`) with `fg = C_top` and `bg = C_bot` (`has_bg = true` when bottom sub-pixel hit). Gives exact 24-bit RGB accuracy for every sub-pixel with zero clustering loss.
2. **`Quadrant` ($2\times 2$, `U+2580`–`U+259F`)**:
   - Partitions the 4 sub-pixels $(UL=1, UR=2, LL=4, LR=8)$ into a 4-bit pattern index `mask` $\in [0, 15]$ via luminance threshold $Y_{\text{mid}}$ (or hit mask on silhouette edges).
   - Maps `mask` via a 16-entry `constexpr` UTF-8 lookup table:
     `{" ", "▘", "▝", "▀", "▖", "▌", "▞", "▛", "▗", "▚", "▐", "▜", "▄", "▙", "▟", "█"}`.
3. **`Sextant` ($2\times 3$, `U+1FB00`–`U+1FB3B`)**:
   - Partitions the 6 sub-pixels $(b_0..b_5)$ into a 6-bit pattern `mask` $\in [0, 63]$.
   - `mask == 0` $\to$ `' '`, `mask == 21` (`0b010101` left column) $\to$ `'▌'`, `mask == 42` (`0b101010` right column) $\to$ `'▐'`, `mask == 63` $\to$ `'█'`; all other 60 patterns map monotonically to `U+1FB00`–`U+1FB3B`.
4. **`ASCII` ($1\times 1$)**:
   - Computes average RGB and luminance $Y \in [0, 255]$ of the cell, indexing into the 10-character luminance ramp:
     `static constexpr char kAsciiRamp[11] = " .:-=+*#%@";`
     with `fg_color` set to the cell's 24-bit RGB color and `has_bg = false`.

---

## 3. Optional 4×4 Ordered Bayer Dithering & OKLab Palette Quantization (Phase 3 Spec)

1. **4×4 Ordered Bayer Matrix**:
   Before clustering or 256-color quantization, when `PresenterOptions::enableDither` is active, sub-pixel $(x, y)$ adds a zero-mean dither offset:
   $$B_4 = \frac{1}{16} \begin{bmatrix} 0 & 8 & 2 & 10 \\ 12 & 4 & 14 & 6 \\ 3 & 11 & 1 & 9 \\ 15 & 7 & 13 & 5 \end{bmatrix} - 0.5 \in [-0.5, +0.4375]$$
   $$C'_c = \operatorname{clamp}\left(C_c + \operatorname{round}(B_4(x \bmod 4, y \bmod 4) \cdot S_{\text{dither}}), 0, 255\right)$$
2. **OKLab Perceptual 256-Color (`\x1b[38;5;Nm`) Fallback LUT**:
   For terminals lacking 24-bit TrueColor (`COLORTERM != "truecolor"`), a precomputed $32\times 32\times 32$ 3D LUT (`32 KB`) maps 15-bit RGB $(R_5, G_5, B_5)$ to the closest xterm-256 palette index under Euclidean distance in **OKLab** perceptual color space ($\Delta E_{\text{OKLab}}^2 = (L_1 - L_2)^2 + (a_1 - a_2)^2 + (b_1 - b_2)^2$).

---

## 4. Parallel Differential ANSI Encoder & `DECSET 2026` Synchronized Output (`B-11`, `B-27` Fixes)

1. **Compile-Time `ByteToDecTable` (`B-11` Fix)**:
   Replaces the legacy `static bool init_lut` data race with a `constexpr` or constructor-initialized 256-entry decimal string table (`"0"`..`"255"` + lengths `1..3`), allowing worker threads in `BuildOutput()` to format `\x1b[38;2;R;G;Bm` and `\x1b[48;2;R;G;Bm` with zero division/modulo instructions and zero shared mutable state.
2. **Exact Front-Buffer State Tracking (`B-27` Fix)**:
   - Each worker thread processes a disjoint band of terminal rows $[r_{\text{start}}, r_{\text{end}})$.
   - At the start of its slice, the worker resets `cursor_valid = false`, `fg_valid = false`, `bg_valid = false` and emits `\x1b[0m`.
   - For each cell $(x, y)$ where `m_BackBuffer[idx] != m_FrontBuffer[idx]`:
     - If `!cursor_valid`, emits `\x1b[<y+1>;<x+1>H`.
     - If `!fg_valid || back.fg_color != current_fg`, emits `\x1b[38;2;R;G;Bm` and sets `current_fg = back.fg_color; fg_valid = true;`.
     - If `back.has_bg`: if `!bg_valid || !current_has_bg || back.bg_color != current_bg`, emits `\x1b[48;2;R;G;Bm` and sets `current_bg = back.bg_color; current_has_bg = true; bg_valid = true;`.
     - Else (`!back.has_bg`): if `!bg_valid || current_has_bg`, emits `\x1b[49m` and sets `current_has_bg = false; bg_valid = true;`.
     - Emits the UTF-8 glyph and assigns `m_FrontBuffer[idx] = back`.
3. **Atomic `DECSET 2026` Synchronized Flush (`FlushOutput()`)**:
   `m_FinalOutput` is pre-allocated (`reserve(512 * 1024)`) and framed with Begin/End Synchronized Update:
   ```cpp
   m_FinalOutput.clear();
   m_FinalOutput.append("\x1b[?2026h", 8); // Begin Synchronized Update (BSU)
   if (m_NeedsFullClear) {
       m_FinalOutput.append("\x1b[2J", 4);
       m_NeedsFullClear = false;
   }
   for (const auto& slice : m_ThreadOutputs) {
       m_FinalOutput.append(slice);
   }
   m_FinalOutput.append("\x1b[0m\x1b[?2026l", 12); // Reset SGR + End Synchronized Update (ESU)
   pal::FastPrint(m_FinalOutput.data(), static_cast<unsigned int>(m_FinalOutput.size()));
   ```

---

## 5. `Canvas2D` & `DebugOverlay` (`include/tvk/ui/Canvas2D.h`, `include/tvk/app/DebugOverlay.h` — Phase 3 Spec)

### 5.1 `Canvas2D` Cell-Space Compositor Interface
```cpp
#pragma once
#include <cstdint>
#include <string_view>
#include "tvk/core/Types.h"

namespace tvk {

enum class BgMode : uint8_t {
    Transparent = 0, // Preserves underlying 3D cell's background/average color!
    Solid       = 1  // Overwrites cell background with specified bg Color
};

enum class BorderStyle : uint8_t {
    Single  = 0, // ┌ ─ ┐ │ └ ┘
    Double  = 1, // ╔ ═ ╗ ║ ╚ ╝
    Rounded = 2, // ╭ ─ ╮ │ ╰ ╯
    Heavy   = 3, // ┏ ━ ┓ ┃ ┗ ┛
    ASCII   = 4  // + - + | + +
};

class Canvas2D {
public:
    void SetCell(int x, int y, const char* utf8, uint8_t len, Color fg, Color bg, BgMode mode = BgMode::Transparent);
    void DrawText(int x, int y, std::string_view utf8Text, Color fg, Color bg = {0,0,0,255}, BgMode mode = BgMode::Transparent);
    void FillRect(int x, int y, int w, int h, Color bg);
    void DrawBox(int x, int y, int w, int h, BorderStyle style, Color fg, Color bg, BgMode mode = BgMode::Solid, std::string_view title = {});
    void DrawProgressBar(int x, int y, int w, float fraction01, Color fgFill, Color bgEmpty);
    void DrawSparkline(int x, int y, int w, const float* values, int count, float minVal, float maxVal, Color fg);
};

} // namespace tvk
```
- **Transparent Text Background Preservation**: When `DrawText` writes a character at `(x, y)` with `BgMode::Transparent` over a 3D-rendered cell `back`, it sets:
  `back.bg = back.has_bg ? back.bg : Color{back.fg.r / 2, back.fg.g / 2, back.fg.b / 2, 255}; back.has_bg = true;`
  so HUD text floats cleanly over the 3D scene without black rectangular cutouts or raw `printf` tearing (`B-31`).

---

## 6. Verification Criteria (`test_renderer` & `test_presenter_canvas`)

1. **All 5 Blitters Produce Valid Non-Empty UTF-8 Output (`tests/test_renderer.cpp`)**:
   - Rendering a lit 3D sphere in headless mode across `BlitterType::{Braille, HalfBlock, Quadrant, Sextant, ASCII}` populates non-space glyphs and accurate 24-bit RGB colors in `TerminalPresenter` without crashes or out-of-bounds writes.
2. **Solid-Surface Braille 2-Color Clustering (`tests/test_renderer.cpp`)**:
   - A fully covered $2\times 4$ cell sets `has_bg = true` and emits both `\x1b[38;2;...` and `\x1b[48;2;...`, eliminating black pinholes on solid geometry.
3. **Differential Front-Buffer Idempotency (`tests/test_renderer.cpp`)**:
   - Rendering two identical consecutive frames produces `0` cell diffs on the second frame (only the 20-byte `\x1b[?2026h\x1b[0m\x1b[?2026l` wrapper is emitted).
