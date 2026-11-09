#include "tvk/TVK_Renderer.h"
#include "tvk/TVK_MathSIMD.h"
#include "tvk/TVK_PAL.h"
#include "tvk/TVK_Primitives.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <immintrin.h>

namespace tvk {

// =========================================================================
// Construction / Destruction
// =========================================================================

Renderer::Renderer(const RendererDescriptor &desc)
    : m_Headless(desc.HeadlessFallback), m_FallbackW(desc.FallbackWidth),
      m_FallbackH(desc.FallbackHeight), m_QualityPreset(desc.InitialQuality),
      m_EnableAdaptiveDetail(desc.EnableAdaptiveDetail), m_NumLights(0),
      m_ArenaMaxTris(desc.MaxTrisPerThread),
      m_ArenaMaxPts(desc.MaxPtsPerThread) {
  // ── Presenter selection ──────────────────────────────────────────────
#ifdef TVK_NOTCURSES_AVAILABLE
  if (desc.UseNotcurses) {
    m_Presenter = std::make_unique<NotcursesPresenter>(desc.HeadlessFallback);
  } else {
    m_Presenter = std::make_unique<TerminalPresenter>(desc.HeadlessFallback);
  }
#else
  m_Presenter = std::make_unique<TerminalPresenter>(desc.HeadlessFallback);
#endif

  // ── Job system ───────────────────────────────────────────────────────
  const int num_workers =
      std::max(1, static_cast<int>(std::thread::hardware_concurrency()));
  m_JobSystem = std::make_unique<JobSystem>(num_workers);
  m_NumWorkers = num_workers;

  // Inject the job system into the presenter
  m_Presenter->SetJobSystem(m_JobSystem.get());

  // ── Per-thread state ─────────────────────────────────────────────────
  m_ThreadStates.resize(num_workers);
  m_ThreadTriArenaSizes.resize(num_workers);
  m_ThreadPtArenaSizes.resize(num_workers);

  // Allocate arena storage
  m_GlobalTriArena.resize(static_cast<size_t>(m_ArenaMaxTris) * num_workers);
  m_GlobalPtArena.resize(static_cast<size_t>(m_ArenaMaxPts) * num_workers);

  // ── Default light ────────────────────────────────────────────────────
  m_Lights[0] = {{20.0f, 30.0f, -10.0f}, {255, 255, 255, 255}, 1.0f};
  m_NumLights = 1;

  // ── Shared primitive meshes ───────────────────────────────────────────
  m_PrimitiveManager.ApplyDetailProfile(1.0f, m_TermW, m_EnableAdaptiveDetail, static_cast<int>(m_QualityPreset));
  m_PrimitiveManager.RebuildSharedMeshes();

  // ── First terminal size detection ─────────────────────────────────────
  UpdateTerminalSize();
}

Renderer::~Renderer() = default;

// =========================================================================
// Light management
// =========================================================================

void Renderer::AddLight(const Vec3 &pos, Color col, float intensity) {
  if (m_NumLights >= kMaxLights)
    return;
  m_Lights[m_NumLights++] = {pos, col, intensity};
}

// =========================================================================
// Terminal / buffer management
// =========================================================================

void Renderer::UpdateTerminalSize() {
  // Let presenter sync its internal state (notcurses reads from its own API)
  m_Presenter->TryUpdateTerminalSize(m_FallbackW, m_FallbackH);

  // Use PAL as authoritative size source (consistent across both backends)
  int nw = 0, nh = 0;
  if (!pal::TryGetTerminalSize(nw, nh)) {
    if (m_TermW == 0) {
      nw = m_FallbackW;
      nh = m_FallbackH;
    } else
      return; // keep current
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
  
  m_GlobalTileTriHeads.assign(m_TotalTiles, -1);
  m_TileWasDirty.assign(m_TotalTiles, false);
  m_GlobalNodeArena.resize(m_MaxNodes);

  m_Presenter->ResizeBuffers(m_TermW, m_TermH);
  m_Presenter->InvalidateFrontBuffer();
  m_PrimitiveManager.ApplyDetailProfile(1.0f, m_TermW, m_EnableAdaptiveDetail, static_cast<int>(m_QualityPreset));
  m_PrimitiveManager.RebuildSharedMeshes();
}



// =========================================================================
// Tile grid (Removed)
// =========================================================================

// (CalculateTileGrid removed)

// =========================================================================
// BeginFrame
// =========================================================================

void Renderer::BeginFrame(const Camera &camera) {
  m_FrameStartNs = pal::GetTimestampNs();
  m_LastFrameStats = {};
  m_LastFrameStats.activeLights = m_NumLights;
  m_LastFrameStats.numWorkers = m_NumWorkers;

  m_CameraPtr = &camera;

  m_NodeCount = 0;
  if (!m_GlobalTileTriHeads.empty()) {
    std::fill(m_GlobalTileTriHeads.begin(), m_GlobalTileTriHeads.end(), -1);
  }

  // Reset arena counters
  for (auto &c : m_ThreadTriArenaSizes)
    c.count = 0;
  for (auto &c : m_ThreadPtArenaSizes)
    c.count = 0;

  // Transform lights to view space for vertex shader
  const Mat4 view = camera.GetViewMatrix();
  for (int i = 0; i < m_NumLights; ++i) {
    m_ViewLights[i].position = VectorTransform(m_Lights[i].position, view);
    m_ViewLights[i].color = m_Lights[i].color;
    m_ViewLights[i].intensity = m_Lights[i].intensity;
  }
}

// =========================================================================
// Execute — build tasks + run all three phases
// =========================================================================

void Renderer::Execute(const CommandBuffer &cmdBuffer) {
  if (!m_CameraPtr || m_PixW == 0)
    return;
  m_CurrentCmdBuffer = &cmdBuffer;

  const float aspect =
      (m_TermH > 0) ? static_cast<float>(m_PixW) / static_cast<float>(m_PixH)
                    : 1.77f;
  m_ProjectionMatrix = m_CameraPtr->GetProjectionMatrix(aspect);
  m_Viewport = {m_PixW * 0.5f, m_PixH * 0.5f, 0.0f, 0.0f};

  // Extract frustum planes from the combined VP matrix
  const Mat4 view = m_CameraPtr->GetViewMatrix();
  const Mat4 vp = Mat4Multiply(m_ProjectionMatrix, view);
  ExtractFrustumPlanes(vp, m_FrustumPlanes);

  BuildRenderTasks(cmdBuffer);
  m_LastFrameStats.drawCallCount =
      static_cast<uint32_t>(cmdBuffer.GetCommands().size());

  const int num_workers = m_NumWorkers;

  // ── VERTEX PHASE ────────────────────────────────────────────────────
  uint64_t t0 = pal::GetTimestampNs();
  m_VertexQueue.Reset(static_cast<int>(m_VertexTasks.size()));
  m_JobSystem->Execute([this](int wid) { ExecuteVertexPhase(wid); });
  m_VertexPhaseEndNs = pal::GetTimestampNs();
  m_LastFrameStats.vertexPhaseNs = m_VertexPhaseEndNs - t0;

  // ── GEOMETRY PHASE ──────────────────────────────────────────────────
  t0 = pal::GetTimestampNs();
  m_GeoQueue.Reset(static_cast<int>(m_GeoTasks.size()));
  m_JobSystem->Execute([this](int wid) { ExecuteGeometryPhase(wid); });
  m_GeoPhaseEndNs = pal::GetTimestampNs();
  m_LastFrameStats.geometryPhaseNs = m_GeoPhaseEndNs - t0;

  // ── RASTER AND PRESENT PHASE (Tiled Immediate-Mode) ─────────────────────────
  t0 = pal::GetTimestampNs();
  m_Presenter->PreFrame(num_workers);
  
  m_RasterQueue.Reset(m_TotalTiles);

  m_ActiveRasterCtx.framebuffer = nullptr;
  m_ActiveRasterCtx.zbuffer = nullptr;
  m_ActiveRasterCtx.pix_w = m_PixW;
  m_ActiveRasterCtx.pix_h = m_PixH;
  m_ActiveRasterCtx.tri_array = m_GlobalTriArena.data();
  m_ActiveRasterCtx.pt_array = m_GlobalPtArena.data();
  m_ActiveRasterCtx.num_workers = num_workers;
  m_ActiveRasterCtx.max_tris_per_thread = m_ArenaMaxTris;
  m_ActiveRasterCtx.max_pts_per_thread = m_ArenaMaxPts;
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

  // ── GENERATE ANSI PASS (Linear String Builder) ─────────────────────────
  t0 = pal::GetTimestampNs();
  m_JobSystem->Execute([this](int wid) { 
      int rows_per_worker = (m_TermH + m_NumWorkers - 1) / m_NumWorkers;
      int start_y = wid * rows_per_worker;
      int end_y = std::min(start_y + rows_per_worker, m_TermH);
      m_Presenter->BuildOutput(wid, start_y, end_y, m_TermW);
  });

  // Accumulate arena overflow count
  for (int i = 0; i < m_NumWorkers; ++i) {
    // Overflow = arena fully exhausted (detected by reaching max)
    if (m_ThreadTriArenaSizes[i].count >= m_ArenaMaxTris)
      ++m_LastFrameStats.arenaOverflows;
  }
}

// =========================================================================
// Present
// =========================================================================

void Renderer::Present() {
  const uint64_t t0 = pal::GetTimestampNs();
  m_Presenter->FlushOutput();
  const uint64_t t1 = pal::GetTimestampNs();
  m_LastFrameStats.presentNs = t1 - t0;
  m_LastFrameStats.frameTimeNs = t1 - m_FrameStartNs;
}

// =========================================================================
// BuildRenderTasks — issue VertexTask + GeometryTask per command
// =========================================================================

void Renderer::BuildRenderTasks(const CommandBuffer &cmdBuffer) {
  const auto &cmds = cmdBuffer.GetCommands();
  m_VertexTasks.clear();
  m_GeoTasks.clear();
  m_FrameFusedTransforms.clear();
  m_FrameNormalTransforms.clear();

  const Mat4 view = m_CameraPtr->GetViewMatrix();
  uint32_t vertex_cursor = 0;

  for (int i = 0; i < static_cast<int>(cmds.size()); ++i) {
    const RenderCommand &cmd = cmds[i];

    // ── Build transform for this command ────────────────────────────
    Mat4 model;
    const Mesh *mesh_ptr = nullptr;
    Color albedo = cmd.color;

    if (cmd.type == CommandType::DrawMesh) {
      model = cmd.transform;
      mesh_ptr = cmd.mesh;
    } else if (cmd.type == CommandType::DrawSphere) {
      model = Mat4Multiply(Mat4Translate(cmd.a),
                           Mat4Scale({cmd.radius, cmd.radius, cmd.radius}));
      mesh_ptr = &m_PrimitiveManager.GetSphere();
    } else if (cmd.type == CommandType::DrawCylinder) {
      Vec3 axis = Normalize(
          Sub(cmd.b, cmd.a)); // reserved for oriented cylinder transform
      (void)axis;
      Vec3 mid = {(cmd.a.x + cmd.b.x) * 0.5f, (cmd.a.y + cmd.b.y) * 0.5f,
                  (cmd.a.z + cmd.b.z) * 0.5f};
      float len = Length(Sub(cmd.b, cmd.a));
      model = Mat4Multiply(Mat4Translate(mid),
                           Mat4Scale({cmd.radius, len, cmd.radius}));
      mesh_ptr = &m_PrimitiveManager.GetCylinder();
    } else if (cmd.type == CommandType::DrawCapsule) {
      Vec3 mid = {(cmd.a.x + cmd.b.x) * 0.5f, (cmd.a.y + cmd.b.y) * 0.5f,
                  (cmd.a.z + cmd.b.z) * 0.5f};
      float len = Length(Sub(cmd.b, cmd.a));
      model = Mat4Multiply(Mat4Translate(mid),
                           Mat4Scale({cmd.radius, len, cmd.radius}));
      mesh_ptr = &m_PrimitiveManager.GetCylinder();
    } else if (cmd.type == CommandType::DrawGround) {
      float y = cmd.a.y, half_ext = cmd.b.y;
      model = Mat4Multiply(
          Mat4Multiply(Mat4Translate({0, y, 0}),
                       Mat4Scale({half_ext * 2.0f, 1.0f, half_ext * 2.0f})),
          Mat4Identity());
      mesh_ptr = &m_PrimitiveManager.GetGround();
    } else {
      continue; // DrawLine and others handled separately (future)
    }

    // ── AABB frustum cull ──────────────────────────────────────────
    if (mesh_ptr) {
      const AABB &obj_aabb = mesh_ptr->GetAABB();
      if (obj_aabb.IsValid()) {
        Vec3 ws_min, ws_max;
        TransformAABB(obj_aabb.min, obj_aabb.max, model, ws_min, ws_max);
        if (!IsAABBInsideFrustum(ws_min, ws_max, m_FrustumPlanes)) {
          ++m_LastFrameStats.drawCallsCulled;
          continue;
        }
      }
    }

    // ── Fused (view*model) transform ───────────────────────────────
    const Mat4 fused = Mat4Multiply(view, model);
    m_FrameFusedTransforms.push_back(fused);

    // 3×3 inverse-transpose for normal transform
    // For uniform-scale transforms: transpose of upper-left 3×3 of fused
    // 3×3 inverse-transpose for normal transform
    std::array<float, 9> nt;
    for (int r = 0; r < 3; ++r) {
      for (int c = 0; c < 3; ++c) {
        // لا تقم بعكس الصفوف والأعمدة هنا!
        nt[r * 3 + c] = fused.m[r][c];
      }
    }
    m_FrameNormalTransforms.push_back(nt);

    const int transform_idx =
        static_cast<int>(m_FrameFusedTransforms.size()) - 1;

    // ── Vertex task ────────────────────────────────────────────────
    const uint32_t nv = static_cast<uint32_t>(mesh_ptr->GetVertices().size());
    VertexTask vt;
    vt.mesh = mesh_ptr;
    vt.transform_idx = transform_idx;
    vt.start_vertex = 0;
    vt.num_vertices = nv;
    vt.output_offset = vertex_cursor;
    vt.albedo_override = albedo;
    m_VertexTasks.push_back(vt);

    // ── Geometry task ──────────────────────────────────────────────
    const uint32_t nt2 =
        static_cast<uint32_t>(mesh_ptr->GetIndices().size() / 3);
    GeometryTask gt;
    gt.mesh = mesh_ptr;
    gt.pso = cmd.pso;
    gt.start_tri = 0;
    gt.num_tris = nt2;
    gt.vertex_offset = vertex_cursor;
    m_GeoTasks.push_back(gt);

    // Ensure frame vertices buffer is large enough
    if (vertex_cursor + nv > m_FrameVertices.size())
      m_FrameVertices.resize(vertex_cursor + nv);
    vertex_cursor += nv;

    m_LastFrameStats.trianglesSubmitted += nt2;
  }
}

// =========================================================================
// Vertex phase — parallel transform of all meshes
// =========================================================================

void Renderer::ExecuteVertexPhase(int worker_id) {
  const int total = static_cast<int>(m_VertexTasks.size());
  while (true) {
    const int idx = m_VertexQueue.Next();
    if (idx >= total)
      break;
    const VertexTask &t = m_VertexTasks[idx];
    TransformMeshVertices(worker_id, t.transform_idx, *t.mesh,
                          m_FrameFusedTransforms[t.transform_idx],
                          t.start_vertex, t.num_vertices, t.output_offset,
                          t.albedo_override);
  }
}

// =========================================================================
// TransformMeshVertices — vertex shader (SIMD positions, AVX2 normals,
//                         multi-light Gouraud + specular half-vector)
// =========================================================================

void Renderer::TransformMeshVertices(int worker_id, int cmd_idx,
                                     const Mesh &mesh, const Mat4 &fused,
                                     uint32_t start_vertex,
                                     uint32_t num_vertices,
                                     uint32_t output_offset,
                                     Color albedo_override) {
  const auto &verts = mesh.GetVertices();
  SIMDWorkingBuffer &sb = m_ThreadStates[worker_id].simd_buffer;

  constexpr uint32_t BATCH = 1024;
  const uint32_t end_vertex = start_vertex + num_vertices;

  for (uint32_t base = start_vertex; base < end_vertex; base += BATCH) {
    const uint32_t count = std::min(BATCH, end_vertex - base);

    // ── Gather SoA ─────────────────────────────────────────────────
    for (uint32_t k = 0; k < count; ++k) {
      const Vertex &v = verts[base + k];
      sb.x[k] = v.position.x;
      sb.y[k] = v.position.y;
      sb.z[k] = v.position.z;
      sb.nx[k] = v.normal.x;
      sb.ny[k] = v.normal.y;
      sb.nz[k] = v.normal.z;
    }

    // ── Transform positions (AVX2) ─────────────────────────────────
    simd::VertexBatchSoA in_soa{sb.x, sb.y, sb.z, sb.nx, sb.ny, sb.nz, count};
    simd::VertexBatchSoA out_soa{sb.out_x,  sb.out_y,  sb.out_z, sb.out_nx,
                                 sb.out_ny, sb.out_nz, count};
    simd::TransformVerticesAVX2(in_soa, out_soa, fused);

    // ── Transform normals (AVX2) ───────────────────────────────────
    const auto &nt = m_FrameNormalTransforms[cmd_idx];
    simd::TransformNormalsAVX2(in_soa, out_soa, nt);

    // ── Scatter back + lighting per vertex ─────────────────────────
    for (uint32_t k = 0; k < count; ++k) {
      ShadeVertex &sv =
          m_FrameVertices[output_offset + base - start_vertex + k];

      sv.position = {sb.out_x[k], sb.out_y[k], sb.out_z[k]};
      sv.normal = {sb.out_nx[k], sb.out_ny[k], sb.out_nz[k]};
      sv.color = albedo_override;
      sv.base_shade = albedo_override.r;

      // ── Multi-light diffuse accumulation ───────────────────────
      float total_ndotl = 0.0f;
      for (int li = 0; li < m_NumLights; ++li) {
        Vec3 L = Normalize(Sub(m_ViewLights[li].position, sv.position));
        float nl = std::max(0.0f, Dot(sv.normal, L));
        // Weight by luminance of light colour
        float lum = (m_ViewLights[li].color.r * 0.2126f +
                     m_ViewLights[li].color.g * 0.7152f +
                     m_ViewLights[li].color.b * 0.0722f) /
                    255.0f;
        total_ndotl += nl * m_ViewLights[li].intensity * lum;
      }
      sv.ndotl = std::min(1.0f, total_ndotl);

      // ── Specular half-vector (primary light) ───────────────────
      if (m_NumLights > 0) {
        Vec3 L = Normalize(Sub(m_ViewLights[0].position, sv.position));
        // V = normalize(-position): view direction in view space
        Vec3 V = Normalize({-sv.position.x, -sv.position.y, -sv.position.z});
        Vec3 H = Normalize(Add(L, V));
        sv.nDotH = std::max(0.0f, Dot(sv.normal, H));
      } else {
        sv.nDotH = 0.0f;
      }
      sv.uv = verts[base + k].uv;
    }
  }
}

// =========================================================================
// Geometry phase — clip + project + bin triangles to tiles
// =========================================================================

void Renderer::ExecuteGeometryPhase(int worker_id) {
  const int total = static_cast<int>(m_GeoTasks.size());
  while (true) {
    const int idx = m_GeoQueue.Next();
    if (idx >= total)
      break;
    const GeometryTask &t = m_GeoTasks[idx];
    DrawMesh(worker_id, *t.mesh, t.pso, {0, 0, m_PixW - 1, m_PixH - 1},
             t.start_tri, t.num_tris, t.vertex_offset);
  }
}

void Renderer::DrawMesh(int worker_id, const Mesh &mesh,
                        const PipelineState &pso, const Rect & /*tile*/,
                        uint32_t start_tri, uint32_t num_tris,
                        uint32_t vertex_offset) {
  const auto &indices = mesh.GetIndices();

  uint32_t &tri_sz = m_ThreadTriArenaSizes[worker_id].count;

  ProjectedTriangle *tri_arr =
      m_GlobalTriArena.data() + worker_id * m_ArenaMaxTris;

  for (uint32_t ti = start_tri; ti < start_tri + num_tris; ++ti) {
    const uint32_t i0 = indices[ti * 3 + 0];
    const uint32_t i1 = indices[ti * 3 + 1];
    const uint32_t i2 = indices[ti * 3 + 2];

    const ShadeVertex raw_verts[3] = {m_FrameVertices[vertex_offset + i0],
                                      m_FrameVertices[vertex_offset + i1],
                                      m_FrameVertices[vertex_offset + i2]};

    ShadeVertex clipped[2][3];
    const int n_out = Rasterizer::ClipAgainstNearPlane(raw_verts, clipped);
    for (int ci = 0; ci < n_out; ++ci) {
      uint32_t old_sz = tri_sz;
      Rasterizer::RasterizeProjectedTriangle(
          worker_id, clipped[ci], pso, m_ProjectionMatrix, m_Viewport, m_PixW,
          m_PixH, tri_arr, tri_sz, m_ArenaMaxTris);
      
      // If triangle was successfully appended, bin it into intersecting tiles
      if (tri_sz > old_sz) {
        uint32_t global_tri_idx = worker_id * m_ArenaMaxTris + old_sz;
        const ProjectedTriangle& pt = tri_arr[old_sz];
        
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

        auto reject = [&](int t_min_x, int t_min_y, int t_max_x, int t_max_y, int ex, int ey, int edx, int edy) {
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
            
            if (reject(t_min_x, t_min_y, t_max_x, t_max_y, p0x, p0y, dx01, dy01) || 
                reject(t_min_x, t_min_y, t_max_x, t_max_y, p1x, p1y, dx12, dy12) || 
                reject(t_min_x, t_min_y, t_max_x, t_max_y, p2x, p2y, dx20, dy20)) {
               continue; 
            }

            int tile_idx = b_ty * m_TileCols + b_tx;
            uint32_t node_idx = __atomic_fetch_add(&m_NodeCount, 1, __ATOMIC_RELAXED);
            if (node_idx < m_MaxNodes) {
              TriangleNode& node = m_GlobalNodeArena[node_idx];
              node.tri_idx = global_tri_idx;
              int32_t old_head;
              do {
                old_head = __atomic_load_n(&m_GlobalTileTriHeads[tile_idx], __ATOMIC_RELAXED);
                node.next = old_head;
              } while (!__atomic_compare_exchange_n(&m_GlobalTileTriHeads[tile_idx], &old_head, node_idx, false, __ATOMIC_RELEASE, __ATOMIC_RELAXED));
            }
          }
        }
      }
    }
  }
}

// =========================================================================
// Raster phase — Tiled Immediate-Mode (L1 Cache Bounded)
// =========================================================================

void Renderer::ExecuteRasterPhase(int worker_id) {
  int total_tiles = m_TotalTiles;

  alignas(64) Pixel local_fb[TILE_SIZE * TILE_SIZE];
  alignas(64) float local_zb[TILE_SIZE * TILE_SIZE];

  while (true) {
    int tile_idx = m_RasterQueue.Next();
    if (tile_idx >= total_tiles)
      break;

    int tile_x = tile_idx % m_TileCols;
    int tile_y = tile_idx / m_TileCols;

    int32_t node_idx = m_GlobalTileTriHeads[tile_idx];

    // Dirty Rect Culling: Absolute zero processing for empty stable tiles.
    if (node_idx == -1 && !m_TileWasDirty[tile_idx]) {
      continue;
    }

    // Only zero depth cache (Terminal Presenter tests Z <= 0.0f, rendering fb useless to clear)
    std::memset(local_zb, 0, sizeof(local_zb));

    if (node_idx != -1) {
      // Phase 1: Sort geometry front-to-back (highest 1/z first) to maximize early-Z rejection later
      int32_t head = node_idx;
      
      // Extract into a small fast array for sorting
      // (Linked lists are slow to sort, arrays are cache-friendly)
      constexpr int MAX_NODES_PER_TILE = 4096; // typical empirical limit
      int32_t sort_array[MAX_NODES_PER_TILE];
      int num_nodes = 0;
      
      while (head != -1 && num_nodes < MAX_NODES_PER_TILE) {
          sort_array[num_nodes++] = head;
          head = m_GlobalNodeArena[head].next;
      }
      
      // Sort in descending order of max_inv_z (front-to-back)
      std::sort(&sort_array[0], &sort_array[num_nodes], [&](int32_t a, int32_t b) {
          float za = m_GlobalTriArena[m_GlobalNodeArena[a].tri_idx].max_inv_z;
          float zb = m_GlobalTriArena[m_GlobalNodeArena[b].tri_idx].max_inv_z;
          return za > zb; 
      });

      // Process sorted array of geometry nodes
      for (int i = 0; i < num_nodes; ++i) {
        int32_t n = sort_array[i];
        TriangleNode& node = m_GlobalNodeArena[n];
        Rasterizer::ProcessTileBand(m_ActiveRasterCtx, m_GlobalTriArena[node.tri_idx], 
                                    tile_x, tile_y, local_fb, local_zb, TILE_SIZE);
      }
      m_TileWasDirty[tile_idx] = true;
    } else {
      // Tile had geometry last frame, now empty. Must pass zeros to force terminal to output spaces.
      m_TileWasDirty[tile_idx] = false;
    }

    // Immediately present the tile from L1 cache
    m_Presenter->SampleTileToBackBuffer(tile_x, tile_y, m_TermW, m_TermH, local_fb, local_zb);
  }
}

// =========================================================================
// DrawTriangle — direct triangle submit (used by inline geometry helpers)
// =========================================================================

void Renderer::DrawTriangle(int worker_id, const ShadeVertex &v0,
                            const ShadeVertex &v1, const ShadeVertex &v2,
                            const PipelineState &pso,
                            const Rect & /*screenBounds*/) {
  const ShadeVertex raw[3] = {v0, v1, v2};
  ShadeVertex clipped[2][3];
  const int n = Rasterizer::ClipAgainstNearPlane(raw, clipped);

  uint32_t &tri_sz = m_ThreadTriArenaSizes[worker_id].count;
  ProjectedTriangle *ta = m_GlobalTriArena.data() + worker_id * m_ArenaMaxTris;

  for (int ci = 0; ci < n; ++ci) {
    uint32_t old_sz = tri_sz;
    Rasterizer::RasterizeProjectedTriangle(
        worker_id, clipped[ci], pso, m_ProjectionMatrix, m_Viewport, m_PixW,
        m_PixH, ta, tri_sz, m_ArenaMaxTris);
        
    if (tri_sz > old_sz) {
      uint32_t global_tri_idx = worker_id * m_ArenaMaxTris + old_sz;
      const ProjectedTriangle& pt = ta[old_sz];
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

      auto reject = [&](int t_min_x, int t_min_y, int t_max_x, int t_max_y, int ex, int ey, int edx, int edy) {
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
          
          if (reject(t_min_x, t_min_y, t_max_x, t_max_y, p0x, p0y, dx01, dy01) || 
              reject(t_min_x, t_min_y, t_max_x, t_max_y, p1x, p1y, dx12, dy12) || 
              reject(t_min_x, t_min_y, t_max_x, t_max_y, p2x, p2y, dx20, dy20)) {
             continue; 
          }

          int tile_idx = b_ty * m_TileCols + b_tx;
          uint32_t node_idx = __atomic_fetch_add(&m_NodeCount, 1, __ATOMIC_RELAXED);
          if (node_idx < m_MaxNodes) {
            TriangleNode& node = m_GlobalNodeArena[node_idx];
            node.tri_idx = global_tri_idx;
            int32_t old_head;
            do {
              old_head = __atomic_load_n(&m_GlobalTileTriHeads[tile_idx], __ATOMIC_RELAXED);
              node.next = old_head;
            } while (!__atomic_compare_exchange_n(&m_GlobalTileTriHeads[tile_idx], &old_head, node_idx, false, __ATOMIC_RELEASE, __ATOMIC_RELAXED));
          }
        }
      }
    }
  }
}

} // namespace tvk
