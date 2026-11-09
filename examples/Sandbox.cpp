// =============================================================================
// MUSHROOM KINGDOM — TVK Terminal Renderer Demo
// Features: Bright daylight, green pipes, spinning coins, floating brick
// blocks,
//           fluffy clouds, and a 2.5D platformer cinematic camera.
// =============================================================================

#include "tvk/TVK_Camera.h"
#include "tvk/TVK_CommandBuffer.h"
#include "tvk/TVK_Fog.h"
#include "tvk/TVK_Math.h"
#include "tvk/TVK_Mesh.h"
#include "tvk/TVK_PAL.h"
#include "tvk/TVK_Renderer.h"
#include <algorithm>
#include <chrono>
#include <clocale>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <thread>
#include <vector>


using namespace tvk;

// ─────────────────────────────────────────────────────────────────────────────
// Math helpers
// ─────────────────────────────────────────────────────────────────────────────
static constexpr float kPi = 3.14159265359f;
static constexpr float kTau = 6.28318530718f;

static uint8_t ByteScale(uint8_t v, float t) {
  float f = static_cast<float>(v) * t;
  return static_cast<uint8_t>(f < 0.f ? 0.f : (f > 255.f ? 255.f : f));
}
static Color ColorScale(Color c, float t) {
  return {ByteScale(c.r, t), ByteScale(c.g, t), ByteScale(c.b, t), 255};
}

static Mat4 MakeTransform(Vec3 pos, float yaw = 0.f, float pitch = 0.f,
                          float roll = 0.f, float scale = 1.f) {
  float cy = std::cos(yaw), sy = std::sin(yaw);
  float cp = std::cos(pitch), sp = std::sin(pitch);
  float cr = std::cos(roll), sr = std::sin(roll);
  Mat4 m{};
  m.m[0][0] = scale * (cy * cr + sy * sp * sr);
  m.m[0][1] = scale * (-cy * sr + sy * sp * cr);
  m.m[0][2] = scale * (sy * cp);
  m.m[0][3] = pos.x;
  m.m[1][0] = scale * (cp * sr);
  m.m[1][1] = scale * (cp * cr);
  m.m[1][2] = scale * (-sp);
  m.m[1][3] = pos.y;
  m.m[2][0] = scale * (-sy * cr + cy * sp * sr);
  m.m[2][1] = scale * (sy * sr + cy * sp * cr);
  m.m[2][2] = scale * (cy * cp);
  m.m[2][3] = pos.z;
  m.m[3][3] = 1.f;
  return m;
}

// ─────────────────────────────────────────────────────────────────────────────
// Materials
// ─────────────────────────────────────────────────────────────────────────────
static PipelineState MakePSO(float ambient, float diffuse, float specular,
                             float shiny, CullMode cull = CullMode::Back) {
  PipelineState p;
  p.cullMode = cull;
  p.depthOp = DepthOp::Less;
  p.material.ambient = ambient;
  p.material.diffuse = diffuse;
  p.material.specular = specular;
  p.material.shininess = shiny;
  return p;
}

// ألوان زاهية كرتونية (Ambient عالي عشان ميبقاش فيه ضلمة كئيبة)
static PipelineState PSO_ToonMatte() {
  return MakePSO(0.40f, 0.70f, 0.05f, 4.f);
}
// للعملات الذهبية عشان تلمع
static PipelineState PSO_Coin() { return MakePSO(0.30f, 0.60f, 1.20f, 64.f); }
// للسماء الساطعة
static PipelineState PSO_Sky() { return MakePSO(1.5f, 0.0f, 0.0f, 1.f); }

// ─────────────────────────────────────────────────────────────────────────────
// Palette (Mario Style)
// ─────────────────────────────────────────────────────────────────────────────
static const Color SKY_BLUE = {92, 148, 252, 255};
static const Color GROUND_BRN = {200, 76, 12, 255};
static const Color GRASS_GRN = {0, 168, 0, 255};
static const Color PIPE_GRN = {0, 128, 0, 255}; // أغمق شوية من العشب
static const Color COIN_GOLD = {252, 216, 0, 255};
static const Color BRICK_BROWN = {160, 50, 0, 255};
static const Color CLOUD_WHT = {240, 240, 255, 255};
static const Color QUESTION_YLW = {255, 200, 50, 255};

// ─────────────────────────────────────────────────────────────────────────────
// Mesh Builders
// ─────────────────────────────────────────────────────────────────────────────
static tvk::Mesh BuildColoredBox(Color col, float sx, float sy, float sz) {
  auto t = [&](float f) -> Color { return ColorScale(col, f); };
  std::vector<Vertex> v = {
      {{-sx, -sy, -sz}, {0, 0, -1}, t(0.80f)},
      {{sx, -sy, -sz}, {0, 0, -1}, t(0.80f)},
      {{sx, sy, -sz}, {0, 0, -1}, t(0.80f)},
      {{-sx, sy, -sz}, {0, 0, -1}, t(0.80f)},
      {{sx, -sy, sz}, {0, 0, 1}, t(1.00f)},
      {{-sx, -sy, sz}, {0, 0, 1}, t(1.00f)},
      {{-sx, sy, sz}, {0, 0, 1}, t(1.00f)},
      {{sx, sy, sz}, {0, 0, 1}, t(1.00f)},
      {{-sx, -sy, sz}, {-1, 0, 0}, t(0.65f)},
      {{-sx, -sy, -sz}, {-1, 0, 0}, t(0.65f)},
      {{-sx, sy, -sz}, {-1, 0, 0}, t(0.65f)},
      {{-sx, sy, sz}, {-1, 0, 0}, t(0.65f)},
      {{sx, -sy, -sz}, {1, 0, 0}, t(0.90f)},
      {{sx, -sy, sz}, {1, 0, 0}, t(0.90f)},
      {{sx, sy, sz}, {1, 0, 0}, t(0.90f)},
      {{sx, sy, -sz}, {1, 0, 0}, t(0.90f)},
      {{-sx, sy, -sz}, {0, 1, 0}, t(1.10f)},
      {{sx, sy, -sz}, {0, 1, 0}, t(1.10f)},
      {{sx, sy, sz}, {0, 1, 0}, t(1.10f)},
      {{-sx, sy, sz}, {0, 1, 0}, t(1.10f)},
      {{-sx, -sy, sz}, {0, -1, 0}, t(0.45f)},
      {{sx, -sy, sz}, {0, -1, 0}, t(0.45f)},
      {{sx, -sy, -sz}, {0, -1, 0}, t(0.45f)},
      {{-sx, -sy, -sz}, {0, -1, 0}, t(0.45f)},
  };
  std::vector<uint32_t> idx = {0,  1,  2,  2,  3,  0,  4,  5,  6,  6,  7,  4,
                               8,  9,  10, 10, 11, 8,  12, 13, 14, 14, 15, 12,
                               16, 17, 18, 18, 19, 16, 20, 21, 22, 22, 23, 20};
  return tvk::Mesh(std::move(v), std::move(idx));
}

// ─────────────────────────────────────────────────────────────────────────────
// Mushroom Kingdom Scene
// ─────────────────────────────────────────────────────────────────────────────
namespace platformer {

class MarioScene {
public:
  MarioScene() {
    // بناء المجسمات الأساسية
    m_BrickBlock = BuildColoredBox(BRICK_BROWN, 1.5f, 1.5f, 1.5f);
    m_QuestionBlock = BuildColoredBox(QUESTION_YLW, 1.5f, 1.5f, 1.5f);

    // أرضية
    m_GroundGrass = BuildColoredBox(GRASS_GRN, 40.f, 1.0f, 10.f);
    m_GroundDirt = BuildColoredBox(GROUND_BRN, 40.f, 4.0f, 10.f);

    // أجزاء الأنبوب
    m_PipeBody = BuildColoredBox(PIPE_GRN, 2.5f, 4.0f, 2.5f);
    m_PipeTop = BuildColoredBox(PIPE_GRN, 2.8f, 1.0f, 2.8f);

    // عملة ذهبية (مكعب مبطط)
    m_Coin = BuildColoredBox(COIN_GOLD, 1.0f, 1.2f, 0.2f);
  }

  void Update(float dt, float t, tvk::Camera &cam) {
    m_Time = t;

    // حركة كاميرا "سلايد" زي ألعاب الـ Platformers
    // بتمشي يمين وشمال بالراحة مع الوقت
    float panX = 15.f * std::sin(t * 0.3f);

    // نركز الكاميرا على زاوية 2.5D (بتبص من قدام ومن فوق شوية)
    cam.Orbit(kPi * 0.15f, -0.2f); // زاوية جانبية خفيفة
    cam.Zoom(50.f);

    // نحرك مركز الكاميرا عشان تدي تأثير المشي (لو الـ Camera class يدعم ده،
    // وإلا الكاميرا هتلف حوالين المركز) هنعتمد على الـ Orbit الحالي بس هيدي
    // إحساس حلو.
    cam.UpdateCache();
  }

  void EncodeCommands(tvk::CommandBuffer &cmd) const {
    const float t = m_Time;
    cmd.Begin();

    // ── 1. سماء زرقاء صافية ──────────────────────────────────────────
    cmd.BindPipeline(PSO_Sky());
    cmd.DrawSphere({0.f, 0.f, 200.f}, 300.f, SKY_BLUE);

    // ── 2. السحب (أشكال بيضاوية بيضاء) ──────────────────────────────
    cmd.BindPipeline(PSO_ToonMatte());
    // سحابة 1
    cmd.DrawSphere({-15.f, 18.f, -10.f}, 3.5f, CLOUD_WHT);
    cmd.DrawSphere({-12.f, 19.f, -10.f}, 4.5f, CLOUD_WHT);
    cmd.DrawSphere({-9.f, 18.f, -10.f}, 3.5f, CLOUD_WHT);
    // سحابة 2 (بتتحرك ببطء)
    float cloudMove = 10.f * std::sin(t * 0.2f);
    cmd.DrawSphere({15.f + cloudMove, 22.f, -20.f}, 4.0f, CLOUD_WHT);
    cmd.DrawSphere({19.f + cloudMove, 23.f, -20.f}, 5.5f, CLOUD_WHT);
    cmd.DrawSphere({23.f + cloudMove, 22.f, -20.f}, 4.0f, CLOUD_WHT);

    // ── 3. الأرضية (عشب فوق وتربة تحت) ───────────────────────────────
    cmd.BindPipeline(PSO_ToonMatte());
    cmd.DrawMesh(m_GroundGrass, MakeTransform({0.f, -5.f, 0.f}));
    cmd.DrawMesh(m_GroundDirt, MakeTransform({0.f, -10.f, 0.f}));

    // ── 4. الأنابيب الخضراء ───────────────────────────────────────────
    // أنبوب يمين
    cmd.DrawMesh(m_PipeBody, MakeTransform({12.f, -1.f, 2.f}));
    cmd.DrawMesh(m_PipeTop, MakeTransform({12.f, 4.f, 2.f}));

    // أنبوب شمال أصغر
    cmd.DrawMesh(m_PipeBody, MakeTransform({-18.f, -2.f, -2.f}));
    cmd.DrawMesh(m_PipeTop, MakeTransform({-18.f, 3.f, -2.f}));

    // ── 5. مكعبات الطوب وعلامات الاستفهام ────────────────────────────
    float bounce = std::max(0.f, std::sin(t * 5.f)) *
                   0.5f; // تأثير نط خفيف للمكعب اللي في النص

    cmd.DrawMesh(m_BrickBlock, MakeTransform({-4.f, 8.f, 0.f}));
    cmd.DrawMesh(m_QuestionBlock, MakeTransform({0.f, 8.f + bounce, 0.f}));
    cmd.DrawMesh(m_BrickBlock, MakeTransform({4.f, 8.f, 0.f}));

    // بلوك عالي شوية
    cmd.DrawMesh(m_QuestionBlock, MakeTransform({8.f, 14.f, 0.f}));

    // ── 6. العملات الذهبية (بتلف) ─────────────────────────────────────
    cmd.BindPipeline(PSO_Coin());
    float coinYaw = t * 3.0f; // سرعة الدوران

    // عملة فوق علامة الاستفهام بتنط معاها
    if (bounce > 0.1f) {
      cmd.DrawMesh(m_Coin,
                   MakeTransform({0.f, 12.f + bounce * 2.f, 0.f}, coinYaw));
    }

    // عملات حرة على الأرض
    cmd.DrawMesh(m_Coin, MakeTransform({-8.f, -2.f, 2.f}, coinYaw));
    cmd.DrawMesh(m_Coin, MakeTransform({-11.f, -2.f, 2.f}, coinYaw + 0.5f));

    // ── 7. شخصية بسيطة تشبه ماريو (شكل تجريدي بكرات) ──────────────────
    cmd.BindPipeline(PSO_ToonMatte());
    float walkBounce = std::abs(std::sin(t * 8.f)) * 0.5f;
    float charX = -10.f + t * 4.f; // بيمشي لقدام
    // لو مشي بره الشاشة نرجعه تاني
    charX = std::fmod(charX + 25.f, 50.f) - 25.f;

    // الجسم (أزرق) والراس (أحمر)
    cmd.DrawSphere({charX, -2.f + walkBounce, 0.f}, 1.2f,
                   {0, 50, 200, 255}); // Overall
    cmd.DrawSphere({charX, 0.5f + walkBounce, 0.f}, 1.0f,
                   {220, 20, 20, 255}); // Cap/Head

    cmd.End();
  }

private:
  float m_Time = 0.f;
  tvk::Mesh m_BrickBlock, m_QuestionBlock, m_GroundGrass, m_GroundDirt,
      m_PipeBody, m_PipeTop, m_Coin;
};

} // namespace platformer

// =============================================================================
// main
// =============================================================================

int main() {
  setlocale(LC_ALL, "");

  tvk::RendererDescriptor desc;
  desc.EnableAdaptiveDetail = false;
  desc.InitialQuality = tvk::QualityPreset::Balanced;

  tvk::pal::SetupTerminal();

  tvk::Renderer renderer(desc);
  tvk::Camera camera;

  // ==========================================
  // إضاءة نهارية مشرقة زي ألعاب النينتندو
  // ==========================================
  renderer.ClearLights();

  // شمس قوية من فوق وبزاوية
  renderer.AddLight({-100.f, 200.f, -100.f}, {255, 250, 240, 255}, 2.50f);

  // إضاءة محيطية (Fill Light) عشان الظلال متكونش سودة تماماً
  renderer.AddLight({100.f, 50.f, 100.f}, {200, 220, 255, 255}, 0.80f);

  platformer::MarioScene scene;

  auto lastTime = std::chrono::high_resolution_clock::now();
  float totalTime = 0.f;
  bool running = true;

  tvk::CommandBuffer cmd;
  renderer.UpdateTerminalSize();

  // Create absolute performance dump log
  std::ofstream perf_log("perf_log.csv");
  perf_log << "Frame,Time,FPS,VertexNs,GeometryNs,RasterNs,PresentNs,Triangles,"
              "Culled,ArenaOverflows\n";
  int frameCount = 0;

  while (running) {
    auto now = std::chrono::high_resolution_clock::now();
    float dt = std::chrono::duration<float>(now - lastTime).count();
    lastTime = now;
    dt = std::min(dt, 0.05f);

    scene.Update(dt, totalTime, camera);
    scene.EncodeCommands(cmd);

    renderer.BeginFrame(camera);
    renderer.Execute(cmd);
    renderer.Present();

    renderer.UpdateTerminalSize();
    totalTime += dt;

    // Log performance metrics roughly twice a second to track exact bottlenecks
    if (frameCount % 30 == 0) {
      const tvk::FrameStats &stats = renderer.GetFrameStats();
      perf_log << frameCount << "," << totalTime << ","
               << (dt > 0.0f ? (1.0f / dt) : 0.0f) << "," << stats.vertexPhaseNs
               << "," << stats.geometryPhaseNs << "," << stats.rasterPhaseNs
               << "," << stats.presentNs << "," << stats.trianglesSubmitted
               << "," << stats.drawCallsCulled << "," << stats.arenaOverflows
               << "\n";
      perf_log.flush();
    }
    frameCount++;

    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now() - now);
    const auto target = std::chrono::microseconds(16666); // ~60 FPS
    if (elapsed < target)
      std::this_thread::sleep_for(target - elapsed);
  }

  return 0;
}