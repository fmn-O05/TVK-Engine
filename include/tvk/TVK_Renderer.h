#pragma once
#include "tvk/TVK_Camera.h"
#include "tvk/TVK_CommandBuffer.h"
#include "tvk/TVK_Fog.h"
#include "tvk/TVK_JobSystem.h"
#include "tvk/TVK_Math.h"
#include "tvk/TVK_Mesh.h"
#include "tvk/TVK_Primitives.h"
#include "tvk/TVK_Rasterizer.h"
#include "tvk/TVK_Stats.h"
#include "tvk/TVK_TerminalPresenter.h"
#include "tvk/TVK_Types.h"
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#ifdef TVK_NOTCURSES_AVAILABLE
#include "tvk/TVK_NotcursesPresenter.h"
#endif

namespace tvk {

// =========================================================================
// RendererDescriptor
// =========================================================================

struct RendererDescriptor {
  QualityPreset InitialQuality = QualityPreset::Balanced;
  bool EnableAdaptiveDetail = true;
  bool HeadlessFallback = false;
  bool UseNotcurses = true; ///< Prefer notcurses backend when available
  int FallbackWidth = 120;
  int FallbackHeight = 40;

  /// Arena capacities (tune for dense scenes; each is per-worker)
  uint32_t MaxTrisPerThread = 2000000;  ///< Increased for 1080p
  uint32_t MaxNodesPerThread = 8000000; ///< Increased for 1080p density
  uint32_t MaxPtsPerThread = 1000000;
  uint32_t MaxPtNodesPerThread = 1000000;

  FogDescriptor Fog; ///< Initial fog settings (disabled by default)
};

// =========================================================================
// Multi-light
// =========================================================================

/// Point/directional light in world space.
struct PointLight {
  Vec3 position = {0.0f, 100.0f, 0.0f};
  Color color = {255, 255, 255, 255};
  float intensity = 1.0f;
};

// =========================================================================
// Renderer
// =========================================================================

class Renderer {
public:
  explicit Renderer(const RendererDescriptor &desc);
  ~Renderer();

  // ── Terminal management ──────────────────────────────────────────────
  void UpdateTerminalSize();

  // ── Lighting API ─────────────────────────────────────────────────────
  /// Compatibility shim: sets light[0] to a white point light at pos.
  void SetGlobalLight(const Vec3 &pos) {
    m_Lights[0] = {pos, {255, 255, 255, 255}, 1.0f};
    m_NumLights = std::max(m_NumLights, 1);
  }
  void AddLight(const Vec3 &pos, Color col = {255, 255, 255, 255},
                float intensity = 1.0f);
  void ClearLights() { m_NumLights = 0; }

  // ── Fog ──────────────────────────────────────────────────────────────
  void SetFog(const FogDescriptor &fog) { m_Fog = fog; }
  const FogDescriptor &GetFog() const { return m_Fog; }

  // ── Frame API ────────────────────────────────────────────────────────
  void BeginFrame(const Camera &camera);
  void Execute(const CommandBuffer &cmdBuffer);
  void Present();

  // ── Query ────────────────────────────────────────────────────────────
  const FrameStats &GetFrameStats() const { return m_LastFrameStats; }
  BlitterType GetActiveBlitter() const {
    return m_Presenter ? m_Presenter->GetActiveBlitter()
                       : BlitterType::BrailleCustom;
  }
  int GetTermW() const { return m_TermW; }
  int GetTermH() const { return m_TermH; }

  // ── Internal — used by Renderer + Rasterizer dispatch ─────────────
  void DrawTriangle(int worker_id, const ShadeVertex &v0, const ShadeVertex &v1,
                    const ShadeVertex &v2, const PipelineState &pso,
                    const Rect &screenBounds);


private:
  // ── Core state ───────────────────────────────────────────────────────
  bool m_Headless;
  int m_TermW = 0, m_TermH = 0;
  int m_PixW = 0, m_PixH = 0;
  int m_FallbackW, m_FallbackH;

  QualityPreset m_QualityPreset;
  bool m_EnableAdaptiveDetail;

  std::unique_ptr<IPresenter> m_Presenter;

  // ── Camera / projection ──────────────────────────────────────────────
  const Camera *m_CameraPtr = nullptr;
  Mat4 m_ProjectionMatrix{};
  ViewportTransform m_Viewport{};
  float m_FrustumPlanes[6][4]{}; ///< View-space frustum planes (Gribb-Hartmann)

  // ── Lighting ─────────────────────────────────────────────────────────
  static constexpr int kMaxLights = 4;
  PointLight m_Lights[kMaxLights];
  PointLight m_ViewLights[kMaxLights]; ///< m_Lights transformed to view space
  int m_NumLights = 0;

  // ── Fog ──────────────────────────────────────────────────────────────
  FogDescriptor m_Fog;

  // ── Primitives ───────────────────────────────────────────────────────
  primitives::PrimitiveManager m_PrimitiveManager;

  void ResizeBuffers(int new_w, int new_h);

  // ── Tile grid ────────────────────────────────────────────────────────
  int m_TileCols = 0;
  int m_TileRows = 0;
  int m_TotalTiles = 0;
  
  static constexpr int TILE_SIZE = 64;

  struct TriangleNode {
      uint32_t tri_idx;
      int32_t next;
  };

  std::vector<TriangleNode> m_GlobalNodeArena;
  uint32_t m_NodeCount = 0;
  uint32_t m_MaxNodes = 5000000; // Expanded to 5M elements for robust capacity
  
  std::vector<int32_t> m_GlobalTileTriHeads;
  std::vector<bool> m_TileWasDirty;


  // ── SIMD working buffer (one per thread) ─────────────────────────────
  struct alignas(64) SIMDWorkingBuffer {
    alignas(32) float x[1024];
    alignas(32) float y[1024];
    alignas(32) float z[1024];
    alignas(32) float out_x[1024];
    alignas(32) float out_y[1024];
    alignas(32) float out_z[1024];
    alignas(32) float nx[1024]; ///< Normal channels for vectorized transform
    alignas(32) float ny[1024];
    alignas(32) float nz[1024];
    alignas(32) float out_nx[1024];
    alignas(32) float out_ny[1024];
    alignas(32) float out_nz[1024];
  };

  struct ThreadState {
    std::vector<Vertex> scratch_vertices;
    SIMDWorkingBuffer simd_buffer;
  };

  // ── Task structs ─────────────────────────────────────────────────────
  struct VertexTask {
    const Mesh *mesh;
    int transform_idx;
    uint32_t start_vertex;
    uint32_t num_vertices;
    uint32_t output_offset;
    Color albedo_override; ///< Per-command colour override
  };

  struct GeometryTask {
    const Mesh *mesh;
    PipelineState pso;
    uint32_t start_tri;
    uint32_t num_tris;
    uint32_t vertex_offset;
  };

  // ── Bump allocator arenas ────────────────────────────────────────────
  struct alignas(64) CacheAlignedCounter {
    uint32_t count = 0;
  };

  std::vector<ProjectedTriangle> m_GlobalTriArena;
  std::vector<CacheAlignedCounter> m_ThreadTriArenaSizes;

  std::vector<ProjectedPoint> m_GlobalPtArena;
  std::vector<CacheAlignedCounter> m_ThreadPtArenaSizes;

  uint32_t m_ArenaMaxTris, m_ArenaMaxPts;

  // ── Frame data ───────────────────────────────────────────────────────
  std::vector<ThreadState> m_ThreadStates;
  std::vector<ShadeVertex> m_FrameVertices;
  std::vector<Mat4> m_FrameFusedTransforms;
  std::vector<std::array<float, 9>> m_FrameNormalTransforms;
  const CommandBuffer *m_CurrentCmdBuffer = nullptr;

  // ── Job system ───────────────────────────────────────────────────────
  std::vector<VertexTask> m_VertexTasks;
  std::vector<GeometryTask> m_GeoTasks;

  AtomicQueue m_VertexQueue;
  AtomicQueue m_GeoQueue;
  AtomicQueue m_RasterQueue;

  std::unique_ptr<JobSystem> m_JobSystem;

  // ── Phase methods ────────────────────────────────────────────────────
  void BuildRenderTasks(const CommandBuffer &cmdBuffer);
  void ExecuteVertexPhase(int worker_id);
  void ExecuteGeometryPhase(int worker_id);
  void ExecuteRasterPhase(int worker_id);

  void TransformMeshVertices(int worker_id, int cmd_idx, const Mesh &mesh,
                             const Mat4 &transform, uint32_t start_vertex,
                             uint32_t num_vertices, uint32_t output_offset,
                             Color albedo_override);
  void DrawMesh(int worker_id, const Mesh &mesh, const PipelineState &pso,
                const Rect &tile, uint32_t start_tri, uint32_t num_tris,
                uint32_t vertex_offset);

  // ── Stats ────────────────────────────────────────────────────────────
  int m_NumWorkers = 1; ///< Cached hardware thread count
  RasterContext
      m_ActiveRasterCtx{}; ///< Shared RasterContext for raster-phase workers

  FrameStats m_LastFrameStats{};
  uint64_t m_FrameStartNs = 0;
  uint64_t m_VertexPhaseEndNs = 0;
  uint64_t m_GeoPhaseEndNs = 0;
  uint64_t m_RasterPhaseEndNs = 0;
};

} // namespace tvk
