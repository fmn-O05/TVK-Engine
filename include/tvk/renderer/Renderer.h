#pragma once
#include "tvk/core/JobSystem.h"
#include "tvk/core/Stats.h"
#include "tvk/core/Types.h"
#include "tvk/math/Math.h"
#include "tvk/renderer/Camera.h"
#include "tvk/renderer/CommandBuffer.h"
#include "tvk/renderer/Fog.h"
#include "tvk/renderer/Mesh.h"
#include "tvk/renderer/Primitives.h"
#include "tvk/renderer/Rasterizer.h"
#include "tvk/renderer/TerminalPresenter.h"
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace tvk {

struct RendererDescriptor {
  QualityPreset InitialQuality = QualityPreset::Balanced;
  BlitterType Blitter = BlitterType::Braille;
  bool EnableAdaptiveDetail = true;
  bool HeadlessFallback = false;
  int FallbackWidth = 120;
  int FallbackHeight = 40;

  uint32_t MaxTrisPerThread = 65536;
  uint32_t MaxNodesPerThread = 262144;
  uint32_t MaxPtsPerThread = 16384;
  uint32_t MaxPtNodesPerThread = 16384;

  FogDescriptor Fog;
};

struct PointLight {
  Vec3 position = {0.0f, 100.0f, 0.0f};
  Color color = {255, 255, 255, 255};
  float intensity = 1.0f;
};

class Renderer {
public:
  explicit Renderer(const RendererDescriptor &desc);
  ~Renderer();

  void UpdateTerminalSize();

  void SetGlobalLight(const Vec3 &pos) {
    m_Lights[0] = {pos, {255, 255, 255, 255}, 1.0f};
    m_NumLights = std::max(m_NumLights, 1);
  }
  void AddLight(const Vec3 &pos, Color col = {255, 255, 255, 255},
                float intensity = 1.0f);
  void ClearLights() { m_NumLights = 0; }

  void SetFog(const FogDescriptor &fog) { m_Fog = fog; }
  const FogDescriptor &GetFog() const { return m_Fog; }

  void SetBlitter(BlitterType type) {
    if (m_Presenter)
      m_Presenter->SetBlitter(type);
  }

  void BeginFrame(const Camera &camera);
  void Execute(const CommandBuffer &cmdBuffer);
  void Present();

  const FrameStats &GetFrameStats() const { return m_LastFrameStats; }
  BlitterType GetActiveBlitter() const {
    return m_Presenter ? m_Presenter->GetActiveBlitter()
                       : BlitterType::Braille;
  }
  int GetTermW() const { return m_TermW; }
  int GetTermH() const { return m_TermH; }
  int GetPixW() const { return m_PixW; }
  int GetPixH() const { return m_PixH; }

  const std::vector<TerminalCell> &GetBackBuffer() const {
    return m_Presenter->GetBackBuffer();
  }
  const TerminalCell &GetCell(int x, int y) const {
    return m_Presenter->GetBackBuffer()[static_cast<size_t>(y * m_TermW + x)];
  }
  const std::string &GetLastOutput() const {
    return m_Presenter->GetLastOutput();
  }

  void DrawTriangle(int worker_id, const ShadeVertex &v0, const ShadeVertex &v1,
                    const ShadeVertex &v2, const PipelineState &pso,
                    const Rect &screenBounds);

private:
  bool m_Headless;
  int m_TermW = 0, m_TermH = 0;
  int m_PixW = 0, m_PixH = 0;
  int m_FallbackW, m_FallbackH;

  QualityPreset m_QualityPreset;
  bool m_EnableAdaptiveDetail;

  std::unique_ptr<IPresenter> m_Presenter;

  const Camera *m_CameraPtr = nullptr;
  Mat4 m_ProjectionMatrix{};
  ViewportTransform m_Viewport{};
  float m_FrustumPlanes[6][4]{};

  static constexpr int kMaxLights = 4;
  PointLight m_Lights[kMaxLights];
  PointLight m_ViewLights[kMaxLights];
  int m_NumLights = 0;

  FogDescriptor m_Fog;

  primitives::PrimitiveManager m_PrimitiveManager;

  void ResizeBuffers(int new_w, int new_h);

  int m_TileCols = 0;
  int m_TileRows = 0;
  int m_TotalTiles = 0;

  static constexpr int TILE_SIZE = 64;

  struct TriangleNode {
    uint32_t tri_idx;
    int32_t next;
  };

  std::vector<TriangleNode> m_GlobalNodeArena;
  std::atomic<uint32_t> m_NodeCount{0};
  uint32_t m_MaxNodes = 5000000;

  std::unique_ptr<std::atomic<int32_t>[]> m_GlobalTileTriHeads;
  std::vector<uint8_t> m_TileWasDirty;

  struct alignas(64) SIMDWorkingBuffer {
    alignas(32) float x[1024];
    alignas(32) float y[1024];
    alignas(32) float z[1024];
    alignas(32) float out_x[1024];
    alignas(32) float out_y[1024];
    alignas(32) float out_z[1024];
    alignas(32) float nx[1024];
    alignas(32) float ny[1024];
    alignas(32) float nz[1024];
    alignas(32) float out_nx[1024];
    alignas(32) float out_ny[1024];
    alignas(32) float out_nz[1024];
  };

  struct ThreadState {
    SIMDWorkingBuffer simd_buffer;
  };

  struct VertexTask {
    const Mesh *mesh;
    int transform_idx;
    uint32_t start_vertex;
    uint32_t num_vertices;
    uint32_t output_offset;
    Color albedo_override;
    MaterialState material;
  };

  struct GeometryTask {
    const Mesh *mesh;
    PipelineState pso;
    uint32_t start_tri;
    uint32_t num_tris;
    uint32_t vertex_offset;
    uint32_t submission_order_base;
  };

  struct alignas(64) CacheAlignedCounter {
    uint32_t count = 0;
    uint32_t culled = 0;
    uint32_t rasterized = 0;
    uint32_t active_tiles = 0;
  };

  std::vector<ProjectedTriangle> m_GlobalTriArena;
  std::vector<CacheAlignedCounter> m_ThreadTriArenaSizes;
  std::vector<ProjectedLine> m_FrameLines;

  uint32_t m_ArenaMaxTris;

  std::vector<ThreadState> m_ThreadStates;
  std::vector<ShadeVertex> m_FrameVertices;
  std::vector<Mat4> m_FrameFusedTransforms;
  std::vector<std::array<float, 9>> m_FrameNormalTransforms;
  const CommandBuffer *m_CurrentCmdBuffer = nullptr;

  std::vector<VertexTask> m_VertexTasks;
  std::vector<GeometryTask> m_GeoTasks;

  AtomicQueue m_VertexQueue;
  AtomicQueue m_GeoQueue;
  AtomicQueue m_RasterQueue;

  std::unique_ptr<JobSystem> m_JobSystem;

  void BuildRenderTasks(const CommandBuffer &cmdBuffer);
  void ExecuteVertexPhase(int worker_id);
  void ExecuteGeometryPhase(int worker_id);
  void ExecuteRasterPhase(int worker_id);

  void TransformMeshVertices(int worker_id, int cmd_idx, const Mesh &mesh,
                             const Mat4 &transform, uint32_t start_vertex,
                             uint32_t num_vertices, uint32_t output_offset,
                             Color albedo_override,
                             const MaterialState &material);
  void DrawMesh(int worker_id, const Mesh &mesh, const PipelineState &pso,
                const Rect &tile, uint32_t start_tri, uint32_t num_tris,
                uint32_t vertex_offset, uint32_t submission_order_base);

  int m_NumWorkers = 1;
  RasterContext m_ActiveRasterCtx{};

  FrameStats m_LastFrameStats{};
  uint64_t m_FrameStartNs = 0;
  uint64_t m_VertexPhaseEndNs = 0;
  uint64_t m_GeoPhaseEndNs = 0;
  uint64_t m_RasterPhaseEndNs = 0;
};

} // namespace tvk
