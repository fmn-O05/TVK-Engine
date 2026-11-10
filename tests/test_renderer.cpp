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

TVK_TEST(ExtendedPrimitives_GenerationAndNormals) {
  // 1. Box
  Mesh box = primitives::CreateBox(2.0f, 4.0f, 6.0f, {200, 100, 50, 255});
  TVK_EXPECT_EQ(box.GetVertices().size(), 24u);
  TVK_EXPECT_EQ(box.GetIndices().size(), 36u);
  const AABB boxAabb = box.GetAABB();
  TVK_EXPECT_TRUE(boxAabb.IsValid());
  TVK_EXPECT_NEAR(boxAabb.min.x, -1.0f, 1e-4f);
  TVK_EXPECT_NEAR(boxAabb.max.x,  1.0f, 1e-4f);
  TVK_EXPECT_NEAR(boxAabb.min.y, -2.0f, 1e-4f);
  TVK_EXPECT_NEAR(boxAabb.max.y,  2.0f, 1e-4f);
  TVK_EXPECT_NEAR(boxAabb.min.z, -3.0f, 1e-4f);
  TVK_EXPECT_NEAR(boxAabb.max.z,  3.0f, 1e-4f);
  for (const auto& v : box.GetVertices()) {
    TVK_EXPECT_NEAR(Length(v.normal), 1.0f, 1e-5f);
  }

  // Degenerate Box
  Mesh degenBox = primitives::CreateBox(-2.0f, 0.0f, -4.0f);
  TVK_EXPECT_EQ(degenBox.GetVertices().size(), 24u);
  TVK_EXPECT_EQ(degenBox.GetIndices().size(), 36u);
  TVK_EXPECT_TRUE(degenBox.GetAABB().IsValid());

  // 2. Cone
  const int coneSegs = 16;
  Mesh cone = primitives::CreateCone(3.0f, 5.0f, coneSegs, {100, 200, 255, 255});
  TVK_EXPECT_TRUE(cone.GetVertices().size() > 0);
  TVK_EXPECT_EQ(cone.GetIndices().size(), static_cast<size_t>(coneSegs * 6));
  const AABB coneAabb = cone.GetAABB();
  TVK_EXPECT_TRUE(coneAabb.IsValid());
  TVK_EXPECT_NEAR(coneAabb.min.y, -2.5f, 1e-4f);
  TVK_EXPECT_NEAR(coneAabb.max.y,  2.5f, 1e-4f);
  for (const auto& v : cone.GetVertices()) {
    TVK_EXPECT_NEAR(Length(v.normal), 1.0f, 1e-4f);
  }

  // Degenerate Cone
  Mesh degenCone = primitives::CreateCone(0.0f, -10.0f, 1);
  TVK_EXPECT_TRUE(degenCone.GetVertices().size() > 0);
  TVK_EXPECT_TRUE(degenCone.GetIndices().size() >= 18u); // at least 3 segments * 6 indices
  TVK_EXPECT_TRUE(degenCone.GetAABB().IsValid());

  // 3. Torus
  const int majorSegs = 12;
  const int minorSegs = 8;
  Mesh torus = primitives::CreateTorus(5.0f, 1.5f, majorSegs, minorSegs);
  TVK_EXPECT_EQ(torus.GetVertices().size(), static_cast<size_t>((majorSegs + 1) * (minorSegs + 1)));
  TVK_EXPECT_EQ(torus.GetIndices().size(), static_cast<size_t>(majorSegs * minorSegs * 6));
  const AABB torusAabb = torus.GetAABB();
  TVK_EXPECT_TRUE(torusAabb.IsValid());
  TVK_EXPECT_NEAR(torusAabb.min.x, -6.5f, 1e-4f);
  TVK_EXPECT_NEAR(torusAabb.max.x,  6.5f, 1e-4f);
  TVK_EXPECT_NEAR(torusAabb.min.y, -1.5f, 1e-4f);
  TVK_EXPECT_NEAR(torusAabb.max.y,  1.5f, 1e-4f);
  for (const auto& v : torus.GetVertices()) {
    TVK_EXPECT_NEAR(Length(v.normal), 1.0f, 1e-4f);
  }

  // Degenerate Torus
  Mesh degenTorus = primitives::CreateTorus(-5.0f, 0.0f, 0, 1);
  TVK_EXPECT_TRUE(degenTorus.GetVertices().size() > 0);
  TVK_EXPECT_TRUE(degenTorus.GetAABB().IsValid());

  // 4. Plane
  const int gridX = 4;
  const int gridZ = 6;
  Mesh plane = primitives::CreatePlane(8.0f, 12.0f, gridX, gridZ);
  TVK_EXPECT_EQ(plane.GetVertices().size(), static_cast<size_t>((gridZ + 1) * (gridX + 1)));
  TVK_EXPECT_EQ(plane.GetIndices().size(), static_cast<size_t>(gridZ * gridX * 6));
  const AABB planeAabb = plane.GetAABB();
  TVK_EXPECT_TRUE(planeAabb.IsValid());
  TVK_EXPECT_NEAR(planeAabb.min.x, -4.0f, 1e-4f);
  TVK_EXPECT_NEAR(planeAabb.max.x,  4.0f, 1e-4f);
  TVK_EXPECT_NEAR(planeAabb.min.y,  0.0f, 1e-4f);
  TVK_EXPECT_NEAR(planeAabb.max.y,  0.0f, 1e-4f);
  TVK_EXPECT_NEAR(planeAabb.min.z, -6.0f, 1e-4f);
  TVK_EXPECT_NEAR(planeAabb.max.z,  6.0f, 1e-4f);
  for (const auto& v : plane.GetVertices()) {
    TVK_EXPECT_NEAR(v.normal.x, 0.0f, 1e-5f);
    TVK_EXPECT_NEAR(v.normal.y, 1.0f, 1e-5f);
    TVK_EXPECT_NEAR(v.normal.z, 0.0f, 1e-5f);
  }

  // Degenerate Plane
  Mesh degenPlane = primitives::CreatePlane(0.0f, -5.0f, 0, -2);
  TVK_EXPECT_TRUE(degenPlane.GetVertices().size() > 0);
  TVK_EXPECT_TRUE(degenPlane.GetAABB().IsValid());
}

TVK_TEST(CommandBuffer_3DWireframeDebugDraw) {
  CommandBuffer cmd;
  cmd.Begin();

  // 1. DrawWireBox (min, max) -> 12 lines
  cmd.DrawWireBox({-2.0f, -2.0f, -2.0f}, {2.0f, 2.0f, 2.0f}, {255, 255, 0, 255});
  TVK_EXPECT_EQ(cmd.GetCommands().size(), 12u);

  // 2. DrawWireBox (AABB) -> +12 lines
  AABB aabb{{-5.0f, -5.0f, -5.0f}, {5.0f, 5.0f, 5.0f}};
  cmd.DrawWireBox(aabb, {0, 255, 255, 255});
  TVK_EXPECT_EQ(cmd.GetCommands().size(), 24u);

  // 3. DrawWireSphere -> 3 orthogonal circles, 10 segments each = 30 lines
  cmd.DrawWireSphere({0.0f, 0.0f, 0.0f}, 4.0f, {255, 0, 255, 255}, 10);
  TVK_EXPECT_EQ(cmd.GetCommands().size(), 54u);

  // 4. DrawAxes -> 3 lines (+X: Red, +Y: Green, +Z: Blue)
  cmd.DrawAxes(Mat4Identity(), 2.0f);
  TVK_EXPECT_EQ(cmd.GetCommands().size(), 57u);
  const auto& cmds = cmd.GetCommands();
  TVK_EXPECT_EQ(cmds[54].color.r, 255u); // +X Red
  TVK_EXPECT_EQ(cmds[54].color.g, 0u);
  TVK_EXPECT_EQ(cmds[55].color.g, 255u); // +Y Green
  TVK_EXPECT_EQ(cmds[55].color.r, 0u);
  TVK_EXPECT_EQ(cmds[56].color.b, 255u); // +Z Blue
  TVK_EXPECT_EQ(cmds[56].color.r, 0u);

  // 5. DrawFrustum -> 12 lines
  Mat4 proj = Mat4Scale({1.0f, 1.0f, 1.0f});
  Mat4 invProj;
  TVK_EXPECT_TRUE(Mat4Invert(proj, invProj));
  cmd.DrawFrustum(invProj, {255, 255, 255, 255});
  TVK_EXPECT_EQ(cmd.GetCommands().size(), 69u);

  // 6. DrawLines
  const Vec3 linePairs[4] = {
      {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f},
      {2.0f, 2.0f, 2.0f}, {3.0f, 3.0f, 3.0f}
  };
  cmd.DrawLines(linePairs, 4, {128, 128, 128, 255});
  TVK_EXPECT_EQ(cmd.GetCommands().size(), 71u);

  // 7. Verify all emitted commands are CommandType::DrawLine
  for (const auto& c : cmd.GetCommands()) {
    TVK_EXPECT_EQ(c.type, CommandType::DrawLine);
  }

  cmd.End();

  // 8. Recording guard: drawing outside Begin/End is ignored
  const size_t countBefore = cmd.GetCommands().size();
  cmd.DrawWireBox({-1, -1, -1}, {1, 1, 1}, {255, 255, 255, 255});
  TVK_EXPECT_EQ(cmd.GetCommands().size(), countBefore);
}

int main() { return TVK_RUN_ALL_TESTS(); }
