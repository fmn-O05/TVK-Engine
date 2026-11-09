#include "tvk/renderer/Renderer.h"
#include "tvk/math/MathSIMD.h"
#include "tvk/platform/PAL.h"
#include "tvk/renderer/Primitives.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace tvk {

Renderer::Renderer(const RendererDescriptor &desc)
    : m_Headless(desc.HeadlessFallback), m_FallbackW(desc.FallbackWidth),
      m_FallbackH(desc.FallbackHeight), m_QualityPreset(desc.InitialQuality),
      m_EnableAdaptiveDetail(desc.EnableAdaptiveDetail), m_NumLights(0),
      m_Fog(desc.Fog), m_ArenaMaxTris(desc.MaxTrisPerThread) {

  m_Presenter = std::make_unique<TerminalPresenter>(desc.HeadlessFallback);
  m_Presenter->SetBlitter(desc.Blitter);

  const int num_workers =
      std::max(1, static_cast<int>(std::thread::hardware_concurrency()));
  m_JobSystem = std::make_unique<JobSystem>(num_workers);
  m_NumWorkers = num_workers;

  m_MaxNodes = std::max(1024u, desc.MaxNodesPerThread) *
               static_cast<uint32_t>(num_workers);

  m_Presenter->SetJobSystem(m_JobSystem.get());

  m_ThreadStates.resize(static_cast<size_t>(num_workers));
  m_ThreadTriArenaSizes.resize(static_cast<size_t>(num_workers));

  m_GlobalTriArena.resize(static_cast<size_t>(m_ArenaMaxTris) *
                          static_cast<size_t>(num_workers));

  m_Lights[0] = {{20.0f, 30.0f, -10.0f}, {255, 255, 255, 255}, 1.0f};
  m_NumLights = 1;

  m_PrimitiveManager.ApplyDetailProfile(
      1.0f, m_FallbackW, m_EnableAdaptiveDetail, m_QualityPreset);
  m_PrimitiveManager.RebuildSharedMeshes();

  UpdateTerminalSize();
}

Renderer::~Renderer() = default;

void Renderer::AddLight(const Vec3 &pos, Color col, float intensity) {
  if (m_NumLights >= kMaxLights)
    return;
  m_Lights[m_NumLights++] = {pos, col, intensity};
}

void Renderer::UpdateTerminalSize() {
  m_Presenter->TryUpdateTerminalSize(m_FallbackW, m_FallbackH);

  int nw = 0, nh = 0;
  if (m_Headless) {
    nw = m_FallbackW;
    nh = m_FallbackH;
  } else if (!pal::TryGetTerminalSize(nw, nh)) {
    if (m_TermW == 0) {
      nw = m_FallbackW;
      nh = m_FallbackH;
    } else {
      return;
    }
  }
  if (nw == m_TermW && nh == m_TermH && m_PixW > 0)
    return;
  ResizeBuffers(nw, nh);
}

void Renderer::ResizeBuffers(int nw, int nh) {
  if (nw <= 0 || nh <= 0)
    return;
  m_TermW = nw;
  m_TermH = nh;
  m_PixW = m_TermW * 2;
  m_PixH = m_TermH * 4;

  m_TileCols = (m_PixW + TILE_SIZE - 1) / TILE_SIZE;
  m_TileRows = (m_PixH + TILE_SIZE - 1) / TILE_SIZE;
  m_TotalTiles = m_TileCols * m_TileRows;

  m_GlobalTileTriHeads =
      std::make_unique<std::atomic<int32_t>[]>(static_cast<size_t>(m_TotalTiles));
  for (int i = 0; i < m_TotalTiles; ++i) {
    m_GlobalTileTriHeads[static_cast<size_t>(i)].store(
        -1, std::memory_order_relaxed);
  }
  m_TileWasDirty.assign(static_cast<size_t>(m_TotalTiles), 0u);
  m_GlobalNodeArena.resize(m_MaxNodes);

  m_Presenter->ResizeBuffers(m_TermW, m_TermH);
  m_Presenter->InvalidateFrontBuffer();
  m_PrimitiveManager.ApplyDetailProfile(
      1.0f, m_TermW, m_EnableAdaptiveDetail, m_QualityPreset);
  m_PrimitiveManager.RebuildSharedMeshes();
}

void Renderer::BeginFrame(const Camera &camera) {
  m_FrameStartNs = pal::GetTimestampNs();
  m_LastFrameStats = {};
  m_LastFrameStats.activeLights = m_NumLights;
  m_LastFrameStats.numWorkers = m_NumWorkers;

  m_CameraPtr = &camera;

  m_NodeCount.store(0, std::memory_order_relaxed);
  if (m_GlobalTileTriHeads) {
    for (int i = 0; i < m_TotalTiles; ++i) {
      m_GlobalTileTriHeads[static_cast<size_t>(i)].store(
          -1, std::memory_order_relaxed);
    }
  }

  for (auto &c : m_ThreadTriArenaSizes) {
    c.count = 0;
    c.culled = 0;
    c.rasterized = 0;
    c.active_tiles = 0;
  }

  const Mat4 view = camera.GetViewMatrix();
  for (int i = 0; i < m_NumLights; ++i) {
    m_ViewLights[i].position = VectorTransform(m_Lights[i].position, view);
    m_ViewLights[i].color = m_Lights[i].color;
    m_ViewLights[i].intensity = m_Lights[i].intensity;
  }
}

void Renderer::Execute(const CommandBuffer &cmdBuffer) {
  if (!m_CameraPtr || m_PixW == 0)
    return;
  m_CurrentCmdBuffer = &cmdBuffer;

  const float aspect =
      (m_TermH > 0)
          ? (static_cast<float>(m_TermW) * 0.5f) / static_cast<float>(m_TermH)
          : 1.77f;
  m_ProjectionMatrix = m_CameraPtr->GetProjectionMatrix(aspect);
  m_Viewport = {m_PixW * 0.5f, m_PixH * 0.5f, 0.0f, 0.0f};

  const Mat4 view = m_CameraPtr->GetViewMatrix();
  const Mat4 vp = Mat4Multiply(m_ProjectionMatrix, view);
  ExtractFrustumPlanes(vp, m_FrustumPlanes);

  BuildRenderTasks(cmdBuffer);
  m_LastFrameStats.drawCallCount =
      static_cast<uint32_t>(cmdBuffer.GetCommands().size());

  const int num_workers = m_NumWorkers;

  uint64_t t0 = pal::GetTimestampNs();
  m_VertexQueue.Reset(static_cast<int>(m_VertexTasks.size()));
  m_JobSystem->Execute([this](int wid) { ExecuteVertexPhase(wid); });
  m_VertexPhaseEndNs = pal::GetTimestampNs();
  m_LastFrameStats.vertexPhaseNs = m_VertexPhaseEndNs - t0;

  t0 = pal::GetTimestampNs();
  m_GeoQueue.Reset(static_cast<int>(m_GeoTasks.size()));
  m_JobSystem->Execute([this](int wid) { ExecuteGeometryPhase(wid); });
  m_GeoPhaseEndNs = pal::GetTimestampNs();
  m_LastFrameStats.geometryPhaseNs = m_GeoPhaseEndNs - t0;

  t0 = pal::GetTimestampNs();
  m_Presenter->PreFrame(num_workers);

  m_RasterQueue.Reset(m_TotalTiles);

  m_ActiveRasterCtx.framebuffer = nullptr;
  m_ActiveRasterCtx.zbuffer = nullptr;
  m_ActiveRasterCtx.pix_w = m_PixW;
  m_ActiveRasterCtx.pix_h = m_PixH;
  m_ActiveRasterCtx.tri_array = m_GlobalTriArena.data();
  m_ActiveRasterCtx.num_workers = num_workers;
  m_ActiveRasterCtx.max_tris_per_thread = m_ArenaMaxTris;
  m_ActiveRasterCtx.fog_enabled = m_Fog.enabled;
  m_ActiveRasterCtx.fog_start = m_Fog.start;
  m_ActiveRasterCtx.fog_inv_range = (m_Fog.end > m_Fog.start + 1e-3f)
                                        ? 1.0f / (m_Fog.end - m_Fog.start)
                                        : 0.0f;
  m_ActiveRasterCtx.fog_r = static_cast<float>(m_Fog.fogColor.r);
  m_ActiveRasterCtx.fog_g = static_cast<float>(m_Fog.fogColor.g);
  m_ActiveRasterCtx.fog_b = static_cast<float>(m_Fog.fogColor.b);

  m_JobSystem->Execute([this](int wid) { ExecuteRasterPhase(wid); });
  m_RasterPhaseEndNs = pal::GetTimestampNs();
  m_LastFrameStats.rasterPhaseNs = m_RasterPhaseEndNs - t0;

  m_JobSystem->Execute([this](int wid) {
    int rows_per_worker = (m_TermH + m_NumWorkers - 1) / m_NumWorkers;
    int start_y = wid * rows_per_worker;
    int end_y = std::min(start_y + rows_per_worker, m_TermH);
    m_Presenter->BuildOutput(wid, start_y, end_y, m_TermW);
  });

  for (int i = 0; i < m_NumWorkers; ++i) {
    const auto &tc = m_ThreadTriArenaSizes[static_cast<size_t>(i)];
    m_LastFrameStats.trianglesCulled += tc.culled;
    m_LastFrameStats.trianglesRasterized += tc.rasterized;
    m_LastFrameStats.activeTiles += tc.active_tiles;
    if (tc.count >= m_ArenaMaxTris)
      ++m_LastFrameStats.arenaOverflows;
  }
  if (m_NodeCount.load(std::memory_order_relaxed) >= m_MaxNodes) {
    ++m_LastFrameStats.arenaOverflows;
  }
}

void Renderer::Present() {
  const uint64_t t0 = pal::GetTimestampNs();
  m_Presenter->FlushOutput();
  const uint64_t t1 = pal::GetTimestampNs();
  m_LastFrameStats.presentNs = t1 - t0;
  m_LastFrameStats.frameTimeNs = t1 - m_FrameStartNs;
  m_LastFrameStats.frameTimeMs =
      static_cast<float>(m_LastFrameStats.frameTimeNs) * 1e-6f;
  m_LastFrameStats.ansiBytesEmitted = m_Presenter->GetLastAnsiBytesEmitted();
}

void Renderer::BuildRenderTasks(const CommandBuffer &cmdBuffer) {
  const auto &cmds = cmdBuffer.GetCommands();
  m_VertexTasks.clear();
  m_GeoTasks.clear();
  m_FrameLines.clear();
  m_FrameFusedTransforms.clear();
  m_FrameNormalTransforms.clear();

  const Mat4 view = m_CameraPtr->GetViewMatrix();
  const float near_plane = m_CameraPtr->GetNear();
  uint32_t vertex_cursor = 0;
  uint32_t submission_cursor = 0;

  auto emitMeshInstance = [&](const Mesh *mesh_ptr, const Mat4 &model,
                              const PipelineState &pso, Color albedo) {
    if (!mesh_ptr)
      return;
    const uint32_t nt2 =
        static_cast<uint32_t>(mesh_ptr->GetIndices().size() / 3);
    m_LastFrameStats.trianglesSubmitted += nt2;

    const AABB &obj_aabb = mesh_ptr->GetAABB();
    if (obj_aabb.IsValid()) {
      Vec3 ws_min, ws_max;
      TransformAABB(obj_aabb.min, obj_aabb.max, model, ws_min, ws_max);
      if (!IsAABBInsideFrustum(ws_min, ws_max, m_FrustumPlanes)) {
        ++m_LastFrameStats.drawCallsCulled;
        m_LastFrameStats.trianglesCulled += nt2;
        return;
      }
    }

    const Mat4 fused = Mat4Multiply(view, model);
    m_FrameFusedTransforms.push_back(fused);
    m_FrameNormalTransforms.push_back(Mat4NormalMatrix(fused).ToArray());

    const int transform_idx =
        static_cast<int>(m_FrameFusedTransforms.size()) - 1;
    const uint32_t nv = static_cast<uint32_t>(mesh_ptr->GetVertices().size());

    VertexTask vt;
    vt.mesh = mesh_ptr;
    vt.transform_idx = transform_idx;
    vt.start_vertex = 0;
    vt.num_vertices = nv;
    vt.output_offset = vertex_cursor;
    vt.albedo_override = albedo;
    vt.material = pso.material;
    m_VertexTasks.push_back(vt);

    GeometryTask gt;
    gt.mesh = mesh_ptr;
    gt.pso = pso;
    gt.start_tri = 0;
    gt.num_tris = nt2;
    gt.vertex_offset = vertex_cursor;
    gt.submission_order_base = submission_cursor;
    m_GeoTasks.push_back(gt);

    if (vertex_cursor + nv > m_FrameVertices.size())
      m_FrameVertices.resize(vertex_cursor + nv);
    vertex_cursor += nv;
    submission_cursor += nt2;
  };

  auto makeCylinderTransform = [](const Vec3 &a, const Vec3 &b,
                                  float radius) -> Mat4 {
    const Vec3 delta = Sub(b, a);
    const float len = Length(delta);
    const Vec3 mid = {(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f,
                      (a.z + b.z) * 0.5f};
    const Vec3 upAxis =
        (len > 1e-6f) ? Mul(delta, 1.0f / len) : Vec3{0.0f, 1.0f, 0.0f};
    Vec3 rightAxis{}, forwardAxis{};
    BuildOrthonormalBasis(upAxis, rightAxis, forwardAxis);

    Mat4 basis = Mat4Identity();
    basis.m[0][0] = rightAxis.x * radius;
    basis.m[0][1] = upAxis.x * len;
    basis.m[0][2] = forwardAxis.x * radius;
    basis.m[0][3] = mid.x;

    basis.m[1][0] = rightAxis.y * radius;
    basis.m[1][1] = upAxis.y * len;
    basis.m[1][2] = forwardAxis.y * radius;
    basis.m[1][3] = mid.y;

    basis.m[2][0] = rightAxis.z * radius;
    basis.m[2][1] = upAxis.z * len;
    basis.m[2][2] = forwardAxis.z * radius;
    basis.m[2][3] = mid.z;
    return basis;
  };

  for (size_t i = 0; i < cmds.size(); ++i) {
    const RenderCommand &cmd = cmds[i];

    if (cmd.type == CommandType::DrawMesh) {
      emitMeshInstance(cmd.mesh, cmd.transform, cmd.pso, cmd.color);
    } else if (cmd.type == CommandType::DrawSphere) {
      Mat4 model = Mat4Multiply(Mat4Translate(cmd.a),
                                Mat4Scale({cmd.radius, cmd.radius, cmd.radius}));
      emitMeshInstance(&m_PrimitiveManager.GetSphere(), model, cmd.pso,
                       cmd.color);
    } else if (cmd.type == CommandType::DrawCylinder) {
      Mat4 model = makeCylinderTransform(cmd.a, cmd.b, cmd.radius);
      emitMeshInstance(&m_PrimitiveManager.GetCylinder(), model, cmd.pso,
                       cmd.color);
    } else if (cmd.type == CommandType::DrawCapsule) {
      Mat4 bodyModel = makeCylinderTransform(cmd.a, cmd.b, cmd.radius);
      emitMeshInstance(&m_PrimitiveManager.GetCylinder(), bodyModel, cmd.pso,
                       cmd.color);
      Mat4 capA = Mat4Multiply(Mat4Translate(cmd.a),
                               Mat4Scale({cmd.radius, cmd.radius, cmd.radius}));
      Mat4 capB = Mat4Multiply(Mat4Translate(cmd.b),
                               Mat4Scale({cmd.radius, cmd.radius, cmd.radius}));
      emitMeshInstance(&m_PrimitiveManager.GetSphere(), capA, cmd.pso,
                       cmd.color);
      emitMeshInstance(&m_PrimitiveManager.GetSphere(), capB, cmd.pso,
                       cmd.color);
    } else if (cmd.type == CommandType::DrawGround) {
      float y = cmd.a.y, half_ext = cmd.b.y;
      Mat4 model = Mat4Multiply(
          Mat4Translate({0.0f, y, 0.0f}),
          Mat4Scale({half_ext * 2.0f, 1.0f, half_ext * 2.0f}));
      emitMeshInstance(&m_PrimitiveManager.GetGround(), model, cmd.pso,
                       cmd.color);
    } else if (cmd.type == CommandType::DrawLine) {
      const Vec3 va = VectorTransform(cmd.a, view);
      const Vec3 vb = VectorTransform(cmd.b, view);
      ProjectedLine pl{};
      if (Rasterizer::ClipAndProjectLine(
              va, vb, cmd.color, cmd.pso.depthOp, near_plane,
              m_ProjectionMatrix, m_Viewport, m_PixW, m_PixH,
              submission_cursor++, pl)) {
        m_FrameLines.push_back(pl);
      } else {
        ++m_LastFrameStats.drawCallsCulled;
      }
    }
  }
}

void Renderer::ExecuteVertexPhase(int worker_id) {
  const int total = static_cast<int>(m_VertexTasks.size());
  while (true) {
    const int idx = m_VertexQueue.Next();
    if (idx >= total)
      break;
    const VertexTask &t = m_VertexTasks[static_cast<size_t>(idx)];
    TransformMeshVertices(worker_id, t.transform_idx, *t.mesh,
                          m_FrameFusedTransforms[static_cast<size_t>(t.transform_idx)],
                          t.start_vertex, t.num_vertices, t.output_offset,
                          t.albedo_override, t.material);
  }
}

void Renderer::TransformMeshVertices(int worker_id, int cmd_idx,
                                     const Mesh &mesh, const Mat4 &fused,
                                     uint32_t start_vertex,
                                     uint32_t num_vertices,
                                     uint32_t output_offset,
                                     Color albedo_override,
                                     const MaterialState &material) {
  const auto &verts = mesh.GetVertices();
  SIMDWorkingBuffer &sb =
      m_ThreadStates[static_cast<size_t>(worker_id)].simd_buffer;

  constexpr uint32_t BATCH = 1024;
  const uint32_t end_vertex = start_vertex + num_vertices;
  const float shininess = std::max(1.0f, material.shininess);

  for (uint32_t base = start_vertex; base < end_vertex; base += BATCH) {
    const uint32_t count = std::min(BATCH, end_vertex - base);

    for (uint32_t k = 0; k < count; ++k) {
      const Vertex &v = verts[base + k];
      sb.x[k] = v.position.x;
      sb.y[k] = v.position.y;
      sb.z[k] = v.position.z;
      sb.nx[k] = v.normal.x;
      sb.ny[k] = v.normal.y;
      sb.nz[k] = v.normal.z;
    }

    simd::VertexBatchSoA in_soa{sb.x, sb.y, sb.z, sb.nx, sb.ny, sb.nz, count};
    simd::VertexBatchSoA out_soa{sb.out_x,  sb.out_y,  sb.out_z, sb.out_nx,
                                 sb.out_ny, sb.out_nz, count};
    simd::TransformVerticesAVX2(in_soa, out_soa, fused);

    const auto &nt = m_FrameNormalTransforms[static_cast<size_t>(cmd_idx)];
    simd::TransformNormalsAVX2(in_soa, out_soa, nt);

    for (uint32_t k = 0; k < count; ++k) {
      const Vertex &src_v = verts[base + k];
      ShadeVertex &sv =
          m_FrameVertices[output_offset + base - start_vertex + k];

      sv.position = {sb.out_x[k], sb.out_y[k], sb.out_z[k]};
      sv.normal = {sb.out_nx[k], sb.out_ny[k], sb.out_nz[k]};
      sv.color = Color::Modulate(src_v.color, albedo_override);
      sv.base_shade = sv.color.r;

      const Vec3 V =
          Normalize({-sv.position.x, -sv.position.y, -sv.position.z});

      float diff_r = 0.0f, diff_g = 0.0f, diff_b = 0.0f;
      float spec_r = 0.0f, spec_g = 0.0f, spec_b = 0.0f;
      float total_ndotl = 0.0f;
      float max_ndoth = 0.0f;

      for (int li = 0; li < m_NumLights; ++li) {
        const PointLight &light = m_ViewLights[li];
        const Vec3 L = Normalize(Sub(light.position, sv.position));
        const float nl = std::max(0.0f, Dot(sv.normal, L));

        const float lr = (light.color.r / 255.0f) * light.intensity;
        const float lg = (light.color.g / 255.0f) * light.intensity;
        const float lb = (light.color.b / 255.0f) * light.intensity;

        diff_r += nl * lr;
        diff_g += nl * lg;
        diff_b += nl * lb;

        const float lum =
            (light.color.r * 0.2126f + light.color.g * 0.7152f +
             light.color.b * 0.0722f) /
            255.0f;
        total_ndotl += nl * light.intensity * lum;

        const Vec3 H = Normalize(Add(L, V));
        const float nh = std::max(0.0f, Dot(sv.normal, H));
        max_ndoth = std::max(max_ndoth, nh);
        if (nh > 0.0f) {
          const float sp = std::pow(nh, shininess);
          spec_r += sp * lr;
          spec_g += sp * lg;
          spec_b += sp * lb;
        }
      }

      sv.ndotl = std::min(1.0f, total_ndotl);
      sv.nDotH = max_ndoth;
      sv.diff_rgb = {diff_r, diff_g, diff_b};
      sv.spec_rgb = {spec_r, spec_g, spec_b};
      sv.has_rgb_light = true;
      sv.uv = src_v.uv;
    }
  }
}

void Renderer::ExecuteGeometryPhase(int worker_id) {
  const int total = static_cast<int>(m_GeoTasks.size());
  while (true) {
    const int idx = m_GeoQueue.Next();
    if (idx >= total)
      break;
    const GeometryTask &t = m_GeoTasks[static_cast<size_t>(idx)];
    DrawMesh(worker_id, *t.mesh, t.pso, {0, 0, m_PixW - 1, m_PixH - 1},
             t.start_tri, t.num_tris, t.vertex_offset,
             t.submission_order_base);
  }
}

void Renderer::DrawMesh(int worker_id, const Mesh &mesh,
                        const PipelineState &pso, const Rect &,
                        uint32_t start_tri, uint32_t num_tris,
                        uint32_t vertex_offset,
                        uint32_t submission_order_base) {
  const auto &indices = mesh.GetIndices();
  CacheAlignedCounter &counter =
      m_ThreadTriArenaSizes[static_cast<size_t>(worker_id)];
  uint32_t &tri_sz = counter.count;

  ProjectedTriangle *tri_arr =
      m_GlobalTriArena.data() +
      static_cast<size_t>(worker_id) * static_cast<size_t>(m_ArenaMaxTris);
  const float near_plane = m_CameraPtr ? m_CameraPtr->GetNear() : NEAR_PLANE;

  for (uint32_t ti = start_tri; ti < start_tri + num_tris; ++ti) {
    const uint32_t i0 = indices[ti * 3 + 0];
    const uint32_t i1 = indices[ti * 3 + 1];
    const uint32_t i2 = indices[ti * 3 + 2];

    const ShadeVertex raw_verts[3] = {m_FrameVertices[vertex_offset + i0],
                                      m_FrameVertices[vertex_offset + i1],
                                      m_FrameVertices[vertex_offset + i2]};

    ShadeVertex clipped[2][3];
    const int n_out =
        Rasterizer::ClipAgainstNearPlane(raw_verts, clipped, near_plane);
    if (n_out == 0) {
      ++counter.culled;
      continue;
    }

    bool any_emitted = false;
    for (int ci = 0; ci < n_out; ++ci) {
      uint32_t old_sz = tri_sz;
      Rasterizer::RasterizeProjectedTriangle(
          worker_id, clipped[ci], pso, m_ProjectionMatrix, m_Viewport, m_PixW,
          m_PixH, tri_arr, tri_sz, m_ArenaMaxTris,
          submission_order_base + ti);

      if (tri_sz > old_sz) {
        any_emitted = true;
        ++counter.rasterized;
        uint32_t global_tri_idx =
            static_cast<uint32_t>(worker_id) * m_ArenaMaxTris + old_sz;
        const ProjectedTriangle &pt = tri_arr[old_sz];

        int min_tx = std::max(0, pt.bounding_box.min_x / TILE_SIZE);
        int max_tx =
            std::min(m_TileCols - 1, pt.bounding_box.max_x / TILE_SIZE);
        int min_ty = std::max(0, pt.bounding_box.min_y / TILE_SIZE);
        int max_ty =
            std::min(m_TileRows - 1, pt.bounding_box.max_y / TILE_SIZE);

        int p0x = static_cast<int>(std::round(pt.p0.x - 0.5f));
        int p0y = static_cast<int>(std::round(pt.p0.y - 0.5f));
        int p1x = static_cast<int>(std::round(pt.p1.x - 0.5f));
        int p1y = static_cast<int>(std::round(pt.p1.y - 0.5f));
        int p2x = static_cast<int>(std::round(pt.p2.x - 0.5f));
        int p2y = static_cast<int>(std::round(pt.p2.y - 0.5f));

        int dx01 = p0y - p1y, dy01 = p1x - p0x;
        int dx12 = p1y - p2y, dy12 = p2x - p1x;
        int dx20 = p2y - p0y, dy20 = p0x - p2x;

        auto reject = [&](int t_min_x, int t_min_y, int t_max_x, int t_max_y,
                          int ex, int ey, int edx, int edy) {
          auto edgeMath = [&](long long px, long long py) {
            return (px - ex) * edx + (py - ey) * edy;
          };
          return edgeMath(t_min_x, t_min_y) < 0 &&
                 edgeMath(t_max_x, t_min_y) < 0 &&
                 edgeMath(t_min_x, t_max_y) < 0 &&
                 edgeMath(t_max_x, t_max_y) < 0;
        };

        for (int b_ty = min_ty; b_ty <= max_ty; ++b_ty) {
          int t_min_y = b_ty * TILE_SIZE;
          int t_max_y = t_min_y + TILE_SIZE - 1;

          for (int b_tx = min_tx; b_tx <= max_tx; ++b_tx) {
            int t_min_x = b_tx * TILE_SIZE;
            int t_max_x = t_min_x + TILE_SIZE - 1;

            if (reject(t_min_x, t_min_y, t_max_x, t_max_y, p0x, p0y, dx01,
                       dy01) ||
                reject(t_min_x, t_min_y, t_max_x, t_max_y, p1x, p1y, dx12,
                       dy12) ||
                reject(t_min_x, t_min_y, t_max_x, t_max_y, p2x, p2y, dx20,
                       dy20)) {
              continue;
            }

            int tile_idx = b_ty * m_TileCols + b_tx;
            uint32_t node_idx =
                m_NodeCount.fetch_add(1, std::memory_order_relaxed);
            if (node_idx < m_MaxNodes) {
              TriangleNode &node = m_GlobalNodeArena[node_idx];
              node.tri_idx = global_tri_idx;
              auto &head_atom =
                  m_GlobalTileTriHeads[static_cast<size_t>(tile_idx)];
              int32_t old_head = head_atom.load(std::memory_order_relaxed);
              do {
                node.next = old_head;
              } while (!head_atom.compare_exchange_weak(
                  old_head, static_cast<int32_t>(node_idx),
                  std::memory_order_release, std::memory_order_relaxed));
            }
          }
        }
      }
    }
    if (!any_emitted) {
      ++counter.culled;
    }
  }
}

void Renderer::ExecuteRasterPhase(int worker_id) {
  int total_tiles = m_TotalTiles;
  CacheAlignedCounter &counter =
      m_ThreadTriArenaSizes[static_cast<size_t>(worker_id)];

  alignas(64) Pixel local_fb[TILE_SIZE * TILE_SIZE];
  alignas(64) float local_zb[TILE_SIZE * TILE_SIZE];

  while (true) {
    int tile_idx = m_RasterQueue.Next();
    if (tile_idx >= total_tiles)
      break;

    int tile_x = tile_idx % m_TileCols;
    int tile_y = tile_idx / m_TileCols;
    const Rect tile_rect = {tile_x * TILE_SIZE, tile_y * TILE_SIZE,
                            (tile_x + 1) * TILE_SIZE - 1,
                            (tile_y + 1) * TILE_SIZE - 1};

    int32_t node_idx = m_GlobalTileTriHeads[static_cast<size_t>(tile_idx)].load(
        std::memory_order_acquire);

    bool has_lines = false;
    for (const auto &line : m_FrameLines) {
      if (line.bounding_box.Overlaps(tile_rect)) {
        has_lines = true;
        break;
      }
    }

    if (node_idx == -1 && !has_lines &&
        m_TileWasDirty[static_cast<size_t>(tile_idx)] == 0u) {
      continue;
    }

    std::memset(local_zb, 0, sizeof(local_zb));

    if (node_idx != -1 || has_lines) {
      ++counter.active_tiles;

      if (node_idx != -1) {
        int32_t head = node_idx;

        constexpr int MAX_NODES_PER_TILE = 4096;
        int32_t sort_array[MAX_NODES_PER_TILE];
        int num_nodes = 0;

        while (head != -1 && num_nodes < MAX_NODES_PER_TILE) {
          sort_array[num_nodes++] = head;
          head = m_GlobalNodeArena[static_cast<size_t>(head)].next;
        }

        // Sort DepthOp::Less triangles front-to-back for Early-Z rejection,
        // while preserving submission order for DepthOp::Always triangles (B-24).
        std::sort(&sort_array[0], &sort_array[num_nodes],
                  [&](int32_t a, int32_t b) {
                    const ProjectedTriangle &ta =
                        m_GlobalTriArena[m_GlobalNodeArena[static_cast<size_t>(a)].tri_idx];
                    const ProjectedTriangle &tb =
                        m_GlobalTriArena[m_GlobalNodeArena[static_cast<size_t>(b)].tri_idx];
                    if (ta.depthOp != tb.depthOp) {
                      return ta.depthOp == DepthOp::Less;
                    }
                    if (ta.depthOp == DepthOp::Always) {
                      return ta.submission_order < tb.submission_order;
                    }
                    if (ta.max_inv_z != tb.max_inv_z) {
                      return ta.max_inv_z > tb.max_inv_z;
                    }
                    return ta.submission_order < tb.submission_order;
                  });

        for (int i = 0; i < num_nodes; ++i) {
          int32_t n = sort_array[i];
          TriangleNode &node = m_GlobalNodeArena[static_cast<size_t>(n)];
          Rasterizer::ProcessTileBand(
              m_ActiveRasterCtx, m_GlobalTriArena[node.tri_idx], tile_x, tile_y,
              local_fb, local_zb, TILE_SIZE);
        }
      }

      if (has_lines) {
        for (const auto &line : m_FrameLines) {
          Rasterizer::RasterizeLineInTile(m_ActiveRasterCtx, line, tile_x,
                                          tile_y, local_fb, local_zb,
                                          TILE_SIZE);
        }
      }

      m_TileWasDirty[static_cast<size_t>(tile_idx)] = 1u;
    } else {
      m_TileWasDirty[static_cast<size_t>(tile_idx)] = 0u;
    }

    m_Presenter->SampleTileToBackBuffer(tile_x, tile_y, m_TermW, m_TermH,
                                        local_fb, local_zb);
  }
}

void Renderer::DrawTriangle(int worker_id, const ShadeVertex &v0,
                            const ShadeVertex &v1, const ShadeVertex &v2,
                            const PipelineState &pso, const Rect &) {
  const ShadeVertex raw[3] = {v0, v1, v2};
  ShadeVertex clipped[2][3];
  const float near_plane = m_CameraPtr ? m_CameraPtr->GetNear() : NEAR_PLANE;
  const int n = Rasterizer::ClipAgainstNearPlane(raw, clipped, near_plane);

  uint32_t &tri_sz =
      m_ThreadTriArenaSizes[static_cast<size_t>(worker_id)].count;
  ProjectedTriangle *ta =
      m_GlobalTriArena.data() +
      static_cast<size_t>(worker_id) * static_cast<size_t>(m_ArenaMaxTris);

  for (int ci = 0; ci < n; ++ci) {
    uint32_t old_sz = tri_sz;
    Rasterizer::RasterizeProjectedTriangle(
        worker_id, clipped[ci], pso, m_ProjectionMatrix, m_Viewport, m_PixW,
        m_PixH, ta, tri_sz, m_ArenaMaxTris, old_sz);

    if (tri_sz > old_sz) {
      uint32_t global_tri_idx =
          static_cast<uint32_t>(worker_id) * m_ArenaMaxTris + old_sz;
      const ProjectedTriangle &pt = ta[old_sz];
      int min_tx = std::max(0, pt.bounding_box.min_x / TILE_SIZE);
      int max_tx = std::min(m_TileCols - 1, pt.bounding_box.max_x / TILE_SIZE);
      int min_ty = std::max(0, pt.bounding_box.min_y / TILE_SIZE);
      int max_ty = std::min(m_TileRows - 1, pt.bounding_box.max_y / TILE_SIZE);

      int p0x = static_cast<int>(std::round(pt.p0.x - 0.5f));
      int p0y = static_cast<int>(std::round(pt.p0.y - 0.5f));
      int p1x = static_cast<int>(std::round(pt.p1.x - 0.5f));
      int p1y = static_cast<int>(std::round(pt.p1.y - 0.5f));
      int p2x = static_cast<int>(std::round(pt.p2.x - 0.5f));
      int p2y = static_cast<int>(std::round(pt.p2.y - 0.5f));

      int dx01 = p0y - p1y, dy01 = p1x - p0x;
      int dx12 = p1y - p2y, dy12 = p2x - p1x;
      int dx20 = p2y - p0y, dy20 = p0x - p2x;

      auto reject = [&](int t_min_x, int t_min_y, int t_max_x, int t_max_y,
                        int ex, int ey, int edx, int edy) {
        auto edgeMath = [&](long long px, long long py) {
          return (px - ex) * edx + (py - ey) * edy;
        };
        return edgeMath(t_min_x, t_min_y) < 0 &&
               edgeMath(t_max_x, t_min_y) < 0 &&
               edgeMath(t_min_x, t_max_y) < 0 &&
               edgeMath(t_max_x, t_max_y) < 0;
      };

      for (int b_ty = min_ty; b_ty <= max_ty; ++b_ty) {
        int t_min_y = b_ty * TILE_SIZE;
        int t_max_y = t_min_y + TILE_SIZE - 1;

        for (int b_tx = min_tx; b_tx <= max_tx; ++b_tx) {
          int t_min_x = b_tx * TILE_SIZE;
          int t_max_x = t_min_x + TILE_SIZE - 1;

          if (reject(t_min_x, t_min_y, t_max_x, t_max_y, p0x, p0y, dx01,
                     dy01) ||
              reject(t_min_x, t_min_y, t_max_x, t_max_y, p1x, p1y, dx12,
                     dy12) ||
              reject(t_min_x, t_min_y, t_max_x, t_max_y, p2x, p2y, dx20,
                     dy20)) {
            continue;
          }

          int tile_idx = b_ty * m_TileCols + b_tx;
          uint32_t node_idx =
              m_NodeCount.fetch_add(1, std::memory_order_relaxed);
          if (node_idx < m_MaxNodes) {
            TriangleNode &node = m_GlobalNodeArena[node_idx];
            node.tri_idx = global_tri_idx;
            auto &head_atom =
                m_GlobalTileTriHeads[static_cast<size_t>(tile_idx)];
            int32_t old_head = head_atom.load(std::memory_order_relaxed);
            do {
              node.next = old_head;
            } while (!head_atom.compare_exchange_weak(
                old_head, static_cast<int32_t>(node_idx),
                std::memory_order_release, std::memory_order_relaxed));
          }
        }
      }
    }
  }
}

} // namespace tvk
