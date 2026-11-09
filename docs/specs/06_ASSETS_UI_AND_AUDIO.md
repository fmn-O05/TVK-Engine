# Specification 06 — Asset Pipeline (`.OBJ`/`.MTL`, Textures), Immediate-Mode Terminal UI (`UIContext`) & Retro Synth Audio

- **Layers Covered**: Layer 6 (`tvk/ui`), Layer 7 (`tvk/assets`), Layer 8 (`tvk/audio`)
- **Target Roadmap Phases**: **Phase 6 (Assets & Immediate-Mode TUI)** & **Phase 7 (Retro Synth & Audio Subsystem)**
- **Dependencies**: Layer 0 (`tvk/core/Handle.h`, `Result.h`), Layer 1 (`tvk/platform/Input.h`), Layer 2 (`tvk/math`), Layer 3 (`tvk/renderer`)
- **Industry Benchmark References**: Sokol (`Handle<Tag>` resource pools), Dear ImGui & Clay (immediate-mode FNV-1a ID stack, `hotId`/`activeId`/`focusedId`, single-pass box layout), `sfxr` / PICO-8 (procedural waveform + ADSR chiptune synthesis).

---

## 1. Layer 7: Asset Pipeline, `.OBJ`/`.MTL` Parser & 2D Textures (`include/tvk/assets/`)

### 1.1 Type-Safe Resource Handles & `AssetManager` (`include/tvk/assets/AssetManager.h`)

Built directly on Layer 0's `Handle<Tag>` and `HandlePool<T, Tag>` (`include/tvk/core/Handle.h`):

```cpp
#pragma once
#include <string_view>
#include "tvk/core/Handle.h"
#include "tvk/core/Result.h"
#include "tvk/renderer/Mesh.h"
#include "tvk/renderer/CommandBuffer.h"

namespace tvk {

struct MeshTag {};
struct TextureTag {};
struct MaterialTag {};
struct SoundTag {};

using MeshHandle     = Handle<MeshTag>;
using TextureHandle  = Handle<TextureTag>;
using MaterialHandle = Handle<MaterialTag>;
using SoundHandle    = Handle<SoundTag>;

class Texture;

class AssetManager {
public:
    [[nodiscard]] Result<MeshHandle> LoadObjMesh(std::string_view filePath);
    [[nodiscard]] MeshHandle RegisterMesh(Mesh&& mesh);
    [[nodiscard]] Mesh* GetMesh(MeshHandle h) noexcept { return m_Meshes.Get(h); }
    [[nodiscard]] const Mesh* GetMesh(MeshHandle h) const noexcept { return m_Meshes.Get(h); }
    bool DestroyMesh(MeshHandle h) noexcept { return m_Meshes.Destroy(h); }

    [[nodiscard]] TextureHandle RegisterTexture(Texture&& tex);
    [[nodiscard]] Texture* GetTexture(TextureHandle h) noexcept { return m_Textures.Get(h); }
    [[nodiscard]] const Texture* GetTexture(TextureHandle h) const noexcept { return m_Textures.Get(h); }
    bool DestroyTexture(TextureHandle h) noexcept { return m_Textures.Destroy(h); }

private:
    HandlePool<Mesh, MeshTag> m_Meshes;
    HandlePool<Texture, TextureTag> m_Textures;
};

} // namespace tvk
```

---

### 1.2 Zero-Dependency Wavefront `.OBJ` / `.MTL` Parser Grammar (`include/tvk/assets/ObjLoader.h`)

`ObjLoader::ParseFromMemory(std::string_view objText, std::string_view mtlText = {}) -> Result<Mesh, ErrorCode>` parses standard Wavefront `.obj` files in a single zero-exception pass:

1. **Supported Directives**:
   - `v x y z [r g b]`: 3D vertex position, plus optional per-vertex RGB float color $\in [0, 1]$ (converted to `Color{r*255, g*255, b*255, 255}`).
   - `vt u v`: 2D texture coordinate.
   - `vn nx ny nz`: 3D vertex normal.
   - `f v1[/vt1][/vn1] v2[/vt2][/vn2] v3[/vt3][/vn3] ...`: Polygon face supporting all 4 standard index formats (`v`, `v/vt`, `v//vn`, `v/vt/vn`) and **negative relative indices** ($i < 0 \implies \text{count} + i$, 1-based $i > 0 \implies i - 1$).
   - `mtllib`, `usemtl`, `Kd r g b`, `Ks r g b`, `Ns shininess`.
2. **$N$-Gon Fan Triangulation & Automatic Normal Synthesis**:
   - Any face with $K \ge 3$ vertices $(V_0, V_1, \dots, V_{K-1})$ is fan-triangulated into $K - 2$ triangles:
     $$(V_0, V_1, V_2),\; (V_0, V_2, V_3),\; \dots,\; (V_0, V_{K-2}, V_{K-1})$$
   - If a face omits normal indices (`vn`), the geometric face normal $\hat{n} = \operatorname{normalize}((p_1 - p_0) \times (p_2 - p_0))$ is computed automatically.

---

### 1.3 2D `Texture` Storage, UV Sampling & Procedural Generators (`include/tvk/assets/Texture.h`)

```cpp
#pragma once
#include <cstdint>
#include <vector>
#include "tvk/core/Types.h"

namespace tvk {

enum class WrapMode : uint8_t { Repeat = 0, Clamp = 1 };
enum class FilterMode : uint8_t { Nearest = 0, Bilinear = 1 };

class Texture {
public:
    Texture() = default;
    Texture(uint32_t width, uint32_t height, Color fill = {255, 255, 255, 255});

    [[nodiscard]] Color Sample(float u, float v,
                               WrapMode wrap = WrapMode::Repeat,
                               FilterMode filter = FilterMode::Nearest) const noexcept;

    // Built-in zero-file-dependency procedural texture generators:
    [[nodiscard]] static Texture CreateCheckerboard(uint32_t size, uint32_t cellSize, Color a, Color b);
    [[nodiscard]] static Texture CreateBrick(uint32_t width, uint32_t height, Color brick, Color mortar);
    [[nodiscard]] static Texture CreateValueNoise(uint32_t size, uint32_t seed, Color dark, Color light);
    [[nodiscard]] static Texture CreateGradient(uint32_t width, uint32_t height, Color top, Color bottom);

    [[nodiscard]] uint32_t GetWidth() const noexcept { return m_Width; }
    [[nodiscard]] uint32_t GetHeight() const noexcept { return m_Height; }
    [[nodiscard]] const std::vector<Color>& GetPixels() const noexcept { return m_Pixels; }

private:
    uint32_t m_Width = 0;
    uint32_t m_Height = 0;
    std::vector<Color> m_Pixels;
};

} // namespace tvk
```
- **Perspective-Correct Scanline UV Interpolation**:
  At each triangle vertex $i \in \{0, 1, 2\}$, precompute $(u'_i, v'_i) = (u_i \cdot w_{\text{inv}, i},\; v_i \cdot w_{\text{inv}, i})$. Step $(u', v')$ linearly across scanlines using constant screen-space gradients $(\partial u'/\partial x, \partial v'/\partial x)$ and recover $(u, v) = (u' / w_{\text{inv}}, v' / w_{\text{inv}})$ at each pixel before calling `Texture::Sample(u, v)` and modulating with vertex lighting.

---

## 2. Layer 6: Immediate-Mode Terminal UI (`include/tvk/ui/UIContext.h`, `Theme.h`)

### 2.1 FNV-1a ID Stack & Widget Interaction State Machine
Every interactive widget receives a deterministic 64-bit `WidgetId` computed via 64-bit FNV-1a hashing seeded by the top of `m_IdStack`:

$$H_0 = \text{m\_IdStack.back()} \quad (\text{default } \text{0xCBF29CE484222325}_{\text{16}}), \quad H_{k+1} = (H_k \oplus \text{byte}_k) \times \text{0x100000001B3}_{\text{16}}$$

`UIContext` tracks three IDs across frames:
- `m_HotId`: Widget currently hovered by SGR mouse cell position `(mouseCellX, mouseCellY)`.
- `m_ActiveId`: Widget currently held down by Left Mouse Button or `Key::Enter` / `Key::Space`.
- `m_FocusedId`: Widget currently selected via keyboard `Tab` / `Shift+Tab` / `Up` / `Down` focus ring.

### 2.2 Complete `UIContext` Widget API

```cpp
#pragma once
#include <cstdint>
#include <string_view>
#include "tvk/ui/Canvas2D.h"

namespace tvk {

struct UITheme {
    BorderStyle panelBorder  = BorderStyle::Rounded;
    Color panelBg            = {18, 22, 32, 255};
    Color panelBorderFg      = {90, 120, 180, 255};
    Color textFg             = {230, 235, 245, 255};
    Color widgetIdleBg       = {35, 42, 60, 255};
    Color widgetHotBg        = {55, 75, 115, 255};
    Color widgetActiveBg     = {85, 130, 210, 255};
    Color accentFg           = {100, 220, 160, 255};
};

class UIContext {
public:
    void BeginFrame(Canvas2D& canvas, int mouseCellX, int mouseCellY, bool mouseDown, bool keyTab, bool keyEnter);
    void EndFrame();

    void PushID(std::string_view strId);
    void PushID(int intId);
    void PopID();

    bool BeginPanel(std::string_view title, int cellX, int cellY, int cellW, int cellH);
    void EndPanel();

    void Label(std::string_view text);
    void Separator();
    bool Button(std::string_view label, int width = 0);
    bool Checkbox(std::string_view label, bool& inOutValue);
    bool SliderFloat(std::string_view label, float& inOutValue, float minVal, float maxVal);
    void ProgressBar(std::string_view label, float fraction01);
    bool ListBox(std::string_view label, const std::string_view* items, int itemCount, int& inOutSelected);
};

} // namespace tvk
```

---

## 3. Layer 8: Retro Synth & Audio Subsystem (`include/tvk/audio/AudioEngine.h`, `Sound.h`)

### 3.1 Procedural Chiptune / SFX Synthesizer (`SynthPatch`)

Terminal games should be able to emit rich sound effects (laser shots, explosions, jumps, coin pickups, UI clicks) with **zero external `.wav` files required**. `SynthPatch` defines a procedural synthesizer patch (`sizeof(SynthPatch) == 48 B`) rendered into 44.1 kHz 16-bit stereo PCM:

```cpp
#pragma once
#include <cstdint>

namespace tvk {

enum class Waveform : uint8_t {
    Square    = 0, // 50% duty cycle square wave
    Pulse25   = 1, // 25% duty cycle NES-style pulse wave
    Triangle  = 2, // Linear triangle wave (bass / flute)
    Sawtooth  = 3, // Bright sawtooth wave
    WhiteNoise= 4, // Uniform PRNG white noise
    LFSRNoise = 5  // 15-bit metallic retro LFSR noise (explosions / percussion)
};

struct ADSREnvelope {
    float attackSec  = 0.01f;
    float decaySec   = 0.08f;
    float sustainLvl = 0.40f; // [0.0, 1.0]
    float releaseSec = 0.15f;
};

struct SynthPatch {
    Waveform     waveform       = Waveform::Square;
    uint8_t      reserved[3]    = {0, 0, 0};
    float        startFreqHz    = 440.0f;
    float        minFreqHz      = 40.0f;
    float        slideHzPerSec  = 0.0f;   // Positive = pitch up (coin/jump); negative = laser/fall
    float        vibratoDepthHz = 0.0f;
    float        vibratoRateHz  = 0.0f;
    float        volume         = 0.5f;   // [0.0, 1.0]
    ADSREnvelope envelope{};
};

} // namespace tvk
```

### 3.2 Instantaneous Oscillator & ADSR Sample Equation
At sample index $k$ ($t = k / 44100.0\text{ s}$), instantaneous frequency and phase evolve as:
$$f(t) = \max\left(f_{\min},\; f_0 + s_{\text{slide}} \cdot t + A_{\text{vib}} \sin(2\pi f_{\text{vib}} t)\right), \quad \phi_{k} = \left(\phi_{k-1} + \frac{f(t)}{44100.0}\right) \bmod 1.0$$
The oscillator value $w(\phi_k) \in [-1, +1]$ is multiplied by the piece-wise linear 4-stage `ADSR` envelope $E(t) \in [0, 1]$ and soft-clipped via $\tanh$-like rational clamping across up to 16 concurrent polyphonic voices.

### 3.3 Zero-Dependency Cross-Platform Audio Backends
- **Linux**: Runtime `dlopen("libasound.so.2")` (ALSA non-blocking PCM ring) or `libpulse-simple.so.0`, without requiring header/link-time dependencies if absent.
- **Windows**: `winmm.dll` (`waveOutOpen` / double-buffered `WAVEHDR`).
- **macOS**: `AudioToolbox` (`AudioQueueNewOutput`).
- **Headless / SSH Fallback (`NullAudioBackend`)**: If no audio device is available or `EngineConfig::headless == true`, `AudioEngine` runs its sample mixer into a silent discard buffer so all gameplay audio calls succeed without errors.

---

## 4. Verification Criteria (`test_assets_ui` & `test_audio`)

1. **Wavefront `.OBJ` Parser**: Parsing an in-memory `.obj` string with negative indices (`f -4/-4/-4 -3/-3/-3 -2/-2/-2 -1/-1/-1`) triangulates the quad into 2 triangles (6 indices) with exact positions, UVs, and normals.
2. **Procedural Textures & Bilinear Sampling**: `Texture::CreateCheckerboard(64, 8, Red, Blue)` returns exact `Red`/`Blue` at cell centers and `127.5` blended purple at cell boundaries under `FilterMode::Bilinear`.
3. **`UIContext` Interaction**: Simulating mouse hover + click inside a `Button("Start", 12)` cell bounding box returns `true` on the click release frame and `false` when clicked outside.
4. **ADSR Synthesizer**: Generating 4,410 samples (`100 ms`) of a 440 Hz `Waveform::Square` patch produces bounded 16-bit PCM samples with non-zero RMS energy and zero `NaN`/clipping overflows.
