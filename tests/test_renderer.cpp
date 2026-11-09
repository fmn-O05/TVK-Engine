#include "TestHarness.h"
#include "tvk/TVK.h"
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

using namespace tvk;

namespace {
int CountNonEmptyCells(const Renderer &r) {
  int count = 0;
  for (const auto &cell : r.GetBackBuffer()) {
    if (cell.utf8_len > 1 || cell.utf8[0] != ' ' || cell.has_bg) {
      ++count;
    }
  }
  return count;
}
} // namespace

TVK_TEST(Camera_OrbitUpdatesCacheAndLookAtPlusZ) {
  Camera cam;
  Mat4 v0 = cam.GetViewMatrix();
  cam.Orbit(1.2f, -0.3f);
  Mat4 v1 = cam.GetViewMatrix();
  // B-18: Orbit() must immediately update cached trig values
  TVK_EXPECT_TRUE( std::abs(v0.m[0][0] - v1.m[0][0]) > 1e-3f );

  // B-17: LookAt must place target at +Z in view space
  cam.LookAt({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 25.0f}, {0.0f, 1.0f, 0.0f});
  float cx = 0.0f, cy = 0.0f, cz = 0.0f;
  TVK_EXPECT_TRUE(cam.WorldToCamera({0.0f, 0.0f, 25.0f}, cx, cy, cz));
  TVK_EXPECT_NEAR(cx, 0.0f, 1e-5f);
  TVK_EXPECT_NEAR(cy, 0.0f, 1e-5f);
  TVK_EXPECT_NEAR(cz, 25.0f, 1e-5f);

  // B-19: Orthographic projection maps near -> -1 and far -> +1
  cam.SetOrthographic(20.0f, 2.0f, 102.0f);
  Mat4 ortho = cam.GetProjectionMatrix(1.5f);
  Vec3 nearNdc = VectorTransform({0.0f, 0.0f, 2.0f}, ortho);
  Vec3 farNdc = VectorTransform({0.0f, 0.0f, 102.0f}, ortho);
  TVK_EXPECT_NEAR(nearNdc.z, -1.0f, 1e-5f);
  TVK_EXPECT_NEAR(farNdc.z, 1.0f, 1e-5f);
}

TVK_TEST(Primitives_ClampDegenerateInputs) {
  // B-26: Degenerate arguments must clamp safely without div-by-zero
  Mesh s = primitives::CreateSphere(0, 0);
  Mesh c = primitives::CreateCylinder(0, 0);
  Mesh g = primitives::CreateGround(0);
  TVK_EXPECT_TRUE(s.GetVertices().size() > 0 && s.GetIndices().size() > 0);
  TVK_EXPECT_TRUE(c.GetVertices().size() > 0 && c.GetIndices().size() > 0);
  TVK_EXPECT_TRUE(g.GetVertices().size() > 0 && g.GetIndices().size() > 0);
}

TVK_TEST(Rasterizer_DynamicNearPlaneClipping) {
  // B-09: ClipAgainstNearPlane respects custom camera near plane (e.g. 0.5f)
  ShadeVertex tri[3]{};
  tri[0].position = {-1.0f, -1.0f, 1.0f};
  tri[1].position = {0.0f, 1.0f, 1.0f};
  tri[2].position = {1.0f, -1.0f, 1.0f};

  ShadeVertex out[2][3]{};
  // With default near plane 4.0f, z=1.0f is behind near plane -> 0 triangles
  TVK_EXPECT_EQ(Rasterizer::ClipAgainstNearPlane(tri, out, 4.0f), 0);
  // With custom near plane 0.5f, z=1.0f is in front -> 1 triangle
  TVK_EXPECT_EQ(Rasterizer::ClipAgainstNearPlane(tri, out, 0.5f), 1);
}

TVK_TEST(Renderer_VertexColorsAndMultiLightRGB) {
  RendererDescriptor desc;
  desc.HeadlessFallback = true;
  desc.FallbackWidth = 80;
  desc.FallbackHeight = 30;
  Renderer renderer(desc);

  Camera cam;
  cam.LookAt({0.0f, 0.0f, -20.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f});
  cam.SetPerspective(60.0f, 1.0f, 200.0f);

  // Create a quad facing -Z with distinct vertex colors (B-04, B-23)
  std::vector<Vertex> verts = {
      {{-8.0f, -8.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {255, 40, 20, 255}},
      {{-8.0f,  8.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {40, 255, 20, 255}},
      {{ 8.0f,  8.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {20, 40, 255, 255}},
      {{ 8.0f, -8.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {200, 180, 50, 255}},
  };
  std::vector<uint32_t> indices = {0, 1, 2, 0, 2, 3};
  Mesh quad(verts, indices);

  renderer.ClearLights();
  renderer.AddLight({0.0f, 0.0f, -25.0f}, {255, 255, 255, 255}, 1.0f);

  PipelineState pso;
  pso.cullMode = CullMode::None;
  pso.material = {0.2f, 0.8f, 0.0f, 8.0f};

  CommandBuffer cmd;
  cmd.Begin();
  cmd.BindPipeline(pso);
  cmd.DrawMesh(quad, Mat4Identity());
  cmd.End();

  renderer.BeginFrame(cam);
  renderer.Execute(cmd);
  renderer.Present();

  const FrameStats &stats = renderer.GetFrameStats();
  TVK_EXPECT_EQ(stats.trianglesSubmitted, 2u);
  TVK_EXPECT_EQ(stats.trianglesRasterized, 2u);
  TVK_EXPECT_TRUE(stats.activeTiles > 0u);
  TVK_EXPECT_TRUE(stats.ansiBytesEmitted > 0u);
  TVK_EXPECT_TRUE(CountNonEmptyCells(renderer) > 20);

  // Verify 24-bit color preservation (not truncated to multiples of 32 by & 0xE0, B-12)
  bool foundNonQuantizedBits = false;
  bool foundSolidBgCluster = false;
  for (const auto &cell : renderer.GetBackBuffer()) {
    if (cell.braille_char != 0) {
      if ((cell.fg_color.r & 0x1Fu) != 0 || (cell.fg_color.g & 0x1Fu) != 0 ||
          (cell.fg_color.b & 0x1Fu) != 0) {
        foundNonQuantizedBits = true;
      }
      if (cell.has_bg) {
        foundSolidBgCluster = true;
      }
    }
  }
  TVK_EXPECT_TRUE(foundNonQuantizedBits);
  TVK_EXPECT_TRUE(foundSolidBgCluster);

  // Verify DECSET 2026 synchronized output framing
  const std::string &ansi = renderer.GetLastOutput();
  TVK_EXPECT_TRUE(ansi.find("\x1b[?2026h") == 0);
  TVK_EXPECT_TRUE(ansi.find("\x1b[?2026l") != std::string::npos);
}

TVK_TEST(Renderer_OrientedCylinderCapsuleAnd3DLines) {
  RendererDescriptor desc;
  desc.HeadlessFallback = true;
  desc.FallbackWidth = 80;
  desc.FallbackHeight = 30;
  Renderer renderer(desc);

  Camera cam;
  cam.LookAt({0.0f, 0.0f, -30.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f});
  cam.SetPerspective(60.0f, 1.0f, 200.0f);

  // B-05: Horizontal cylinder along X axis from (-12, 0, 0) to (+12, 0, 0)
  CommandBuffer cmd;
  cmd.Begin();
  PipelineState pso;
  pso.cullMode = CullMode::Back;
  cmd.BindPipeline(pso);
  cmd.DrawCylinder({-12.0f, 0.0f, 0.0f}, {12.0f, 0.0f, 0.0f}, 2.0f,
                   Color{255, 80, 40, 255});
  cmd.End();

  renderer.BeginFrame(cam);
  renderer.Execute(cmd);
  renderer.Present();

  // Verify horizontal span is wider than vertical span for an X-axis cylinder
  int minX = 999, maxX = -1, minY = 999, maxY = -1;
  for (int y = 0; y < renderer.GetTermH(); ++y) {
    for (int x = 0; x < renderer.GetTermW(); ++x) {
      const TerminalCell &c = renderer.GetCell(x, y);
      if (c.braille_char != 0) {
        minX = std::min(minX, x);
        maxX = std::max(maxX, x);
        minY = std::min(minY, y);
        maxY = std::max(maxY, y);
      }
    }
  }
  TVK_EXPECT_TRUE((maxX - minX) > (maxY - minY) * 2);

  // B-06: 3D line rendering
  cmd.Begin();
  cmd.DrawLine({-10.0f, -5.0f, 0.0f}, {10.0f, 5.0f, 0.0f},
               Color{0, 255, 128, 255});
  cmd.End();

  renderer.BeginFrame(cam);
  renderer.Execute(cmd);
  renderer.Present();
  TVK_EXPECT_TRUE(CountNonEmptyCells(renderer) > 5);
}

TVK_TEST(Renderer_OrthographicAndDepthOpAlwaysOrder) {
  RendererDescriptor desc;
  desc.HeadlessFallback = true;
  desc.FallbackWidth = 60;
  desc.FallbackHeight = 24;
  Renderer renderer(desc);

  // B-08: Orthographic rendering
  Camera cam;
  cam.LookAt({0.0f, 0.0f, -20.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f});
  cam.SetOrthographic(15.0f, 1.0f, 100.0f);

  CommandBuffer cmd;
  cmd.Begin();
  PipelineState pso;
  pso.cullMode = CullMode::None;
  pso.depthOp = DepthOp::Always;
  pso.material = {1.0f, 0.0f, 0.0f, 1.0f};
  cmd.BindPipeline(pso);

  // B-24: Submit closer RED sphere first (z = -5), then farther BLUE sphere second (z = +5)
  // Under DepthOp::Always, the second sphere (BLUE) must overwrite the first (RED)!
  cmd.DrawSphere({0.0f, 0.0f, -5.0f}, 8.0f, Color{255, 0, 0, 255});
  cmd.DrawSphere({0.0f, 0.0f, 5.0f}, 8.0f, Color{0, 0, 255, 255});
  cmd.End();

  renderer.BeginFrame(cam);
  renderer.Execute(cmd);
  renderer.Present();

  const TerminalCell &center =
      renderer.GetCell(renderer.GetTermW() / 2, renderer.GetTermH() / 2);
  TVK_EXPECT_TRUE(center.braille_char != 0);
  TVK_EXPECT_TRUE(center.fg_color.b > 200);
  TVK_EXPECT_EQ(center.fg_color.r, 0u);
}

TVK_TEST(Renderer_AllFiveBlittersProduceValidOutput) {
  RendererDescriptor desc;
  desc.HeadlessFallback = true;
  desc.FallbackWidth = 60;
  desc.FallbackHeight = 24;
  Renderer renderer(desc);

  Camera cam;
  cam.LookAt({0.0f, 0.0f, -20.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f});
  cam.SetPerspective(60.0f, 1.0f, 100.0f);

  CommandBuffer cmd;
  cmd.Begin();
  cmd.DrawSphere({0.0f, 0.0f, 0.0f}, 6.0f, Color{220, 140, 60, 255});
  cmd.End();

  const BlitterType blitters[5] = {
      BlitterType::Braille, BlitterType::HalfBlock, BlitterType::Quadrant,
      BlitterType::Sextant, BlitterType::ASCII};

  for (BlitterType b : blitters) {
    renderer.SetBlitter(b);
    TVK_EXPECT_EQ(renderer.GetActiveBlitter(), b);
    renderer.BeginFrame(cam);
    renderer.Execute(cmd);
    renderer.Present();
    TVK_EXPECT_TRUE(CountNonEmptyCells(renderer) > 10);
    TVK_EXPECT_TRUE(renderer.GetFrameStats().ansiBytesEmitted > 20u);
  }
}

int main() { return TVK_RUN_ALL_TESTS(); }
