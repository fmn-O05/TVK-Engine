#pragma once
#include <cstdint>
#include <vector>
#include "tvk/core/Types.h"
#include "tvk/math/Math.h"
#include "tvk/renderer/CommandBuffer.h"

namespace tvk {

static constexpr float NEAR_PLANE = 4.0f;

struct ViewportTransform {
  float half_w;
  float half_h;
  float offset_x;
  float offset_y;
};

struct Rect {
  int min_x, min_y;
  int max_x, max_y;
  bool Overlaps(const Rect &o) const {
    return !(min_x > o.max_x || max_x < o.min_x || min_y > o.max_y ||
             max_y < o.min_y);
  }
};

struct ShadeVertex {
  Vec3 position{};
  Vec3 normal{};
  Color color{};
  float ndotl = 0.0f;
  float nDotH = 0.0f;
  uint8_t base_shade = 0;
  Vec2 uv = {0.0f, 0.0f};
  Vec3 diff_rgb = {0.0f, 0.0f, 0.0f};
  Vec3 spec_rgb = {0.0f, 0.0f, 0.0f};
  bool has_rgb_light = false;
};

struct ProjectedTriangle {
  Vec3 p0, p1, p2;

  float ooz[3];
  float ndotl[3];
  float spec[3];

  float col_r[3];
  float col_g[3];
  float col_b[3];

  float uv_u[3]; // u / z
  float uv_v[3]; // v / z

  Color albedo;
  float ambient;
  float diffuse;
  float specular;

  float dz_dx, dz_dy;
  float dndotl_dx, dndotl_dy;
  float dspec_dx, dspec_dy;

  float dr_dx, dr_dy;
  float dg_dx, dg_dy;
  float db_dx, db_dy;

  float du_dx, du_dy;
  float dv_dx, dv_dy;

  float max_inv_z;

  uint32_t submission_order = 0;
  uint8_t base_shade = 0;
  DepthOp depthOp = DepthOp::Less;
  CullMode cullMode = CullMode::Back;
  Rect bounding_box{};
  float inv_area = 0.0f;
};

struct ProjectedLine {
  int32_t x0 = 0, y0 = 0;
  int32_t x1 = 0, y1 = 0;
  float ooz0 = 0.0f, ooz1 = 0.0f;
  Color color{};
  DepthOp depthOp = DepthOp::Less;
  Rect bounding_box{};
  uint32_t submission_order = 0;
};

struct RasterContext {
  Pixel *framebuffer = nullptr;
  float *zbuffer = nullptr;
  int pix_w = 0;
  int pix_h = 0;

  const ProjectedTriangle *tri_array = nullptr;

  int num_workers = 1;
  uint32_t max_tris_per_thread = 0;

  bool fog_enabled = false;
  float fog_start = 50.0f;
  float fog_inv_range = 1.0f / 150.0f;
  float fog_r = 8.0f;
  float fog_g = 8.0f;
  float fog_b = 16.0f;
};

class Rasterizer {
public:
  static void ProcessTileBand(const RasterContext &ctx,
                              const ProjectedTriangle &tri, int tile_x,
                              int tile_y, Pixel *local_fb, float *local_zb,
                              int tile_size);

  static void RasterizeLineInTile(const RasterContext &ctx,
                                  const ProjectedLine &line, int tile_x,
                                  int tile_y, Pixel *local_fb, float *local_zb,
                                  int tile_size);

  static int ClipAgainstNearPlane(const ShadeVertex in_v[3],
                                  ShadeVertex out_triangles[2][3],
                                  float near_plane = NEAR_PLANE);

  static bool ClipAndProjectLine(const Vec3 &view_a, const Vec3 &view_b,
                                 Color color, DepthOp depthOp,
                                 float near_plane, const Mat4 &proj_matrix,
                                 const ViewportTransform &viewport, int pix_w,
                                 int pix_h, uint32_t submission_order,
                                 ProjectedLine &out_line);

  static void RasterizeProjectedTriangle(
      int worker_id, const ShadeVertex view_verts[3], const PipelineState &pso,
      const Mat4 &proj_matrix, const ViewportTransform &viewport, int pix_w,
      int pix_h, ProjectedTriangle *tri_array, uint32_t &tri_arena_size,
      uint32_t max_tris_per_thread, uint32_t submission_order = 0);
};

} // namespace tvk
