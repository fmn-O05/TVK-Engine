#include "tvk/renderer/Rasterizer.h"
#include <algorithm>
#include <cmath>
#include <cstring>

#if defined(TVK_SIMD_AVX2) && defined(__AVX2__)
#include <immintrin.h>
#endif

namespace tvk {

namespace {

constexpr float kGuardBand = 16384.0f;

inline void ProjectPoint(const Vec3 &pos, const Mat4 &proj_matrix,
                         const ViewportTransform &viewport, Vec3 &out_screen,
                         float &out_ooz) {
  const float z = pos.z;
  const float safe_z = std::max(z, 1e-4f);
  const float inv_z = 1.0f / safe_z;
  const bool is_ortho = (std::abs(proj_matrix.m[3][2]) < 1e-6f);

  float cx = 0.0f;
  float cy = 0.0f;
  if (is_ortho) {
    cx = proj_matrix.m[0][0] * pos.x + proj_matrix.m[0][1] * pos.y +
         proj_matrix.m[0][2] * z + proj_matrix.m[0][3];
    cy = proj_matrix.m[1][0] * pos.x + proj_matrix.m[1][1] * pos.y +
         proj_matrix.m[1][2] * z + proj_matrix.m[1][3];
  } else {
    cx = (proj_matrix.m[0][0] * pos.x + proj_matrix.m[0][2] * z +
          proj_matrix.m[0][3]) *
         inv_z;
    cy = (proj_matrix.m[1][1] * pos.y + proj_matrix.m[1][2] * z +
          proj_matrix.m[1][3]) *
         inv_z;
  }

  const float sx = (cx + 1.0f) * viewport.half_w + viewport.offset_x;
  const float sy = (-cy + 1.0f) * viewport.half_h + viewport.offset_y;

  out_screen.x = ClampFloat(sx, -kGuardBand, kGuardBand);
  out_screen.y = ClampFloat(sy, -kGuardBand, kGuardBand);
  out_screen.z = inv_z;
  out_ooz = inv_z;
}

inline int32_t TopLeftBias(int32_t dx, int32_t dy) noexcept {
  // Edge function E(x, y) = (x - x0)*dx + (y - y0)*dy where dx = y0 - y1, dy = x1 - x0.
  // Top edge: dy < 0 and dx == 0 (horizontal going left in screen space with y down).
  // Left edge: dx > 0 (y0 > y1, going up in screen space).
  return (dx > 0 || (dx == 0 && dy < 0)) ? 0 : -1;
}

} // namespace

int Rasterizer::ClipAgainstNearPlane(const ShadeVertex in_v[3],
                                     ShadeVertex out_triangles[2][3],
                                     float near_plane) {
  const float NP = std::max(1e-3f, near_plane);
  const bool inside[3] = {in_v[0].position.z >= NP, in_v[1].position.z >= NP,
                          in_v[2].position.z >= NP};
  const int cnt = static_cast<int>(inside[0]) + static_cast<int>(inside[1]) +
                  static_cast<int>(inside[2]);

  if (cnt == 0)
    return 0;
  if (cnt == 3) {
    out_triangles[0][0] = in_v[0];
    out_triangles[0][1] = in_v[1];
    out_triangles[0][2] = in_v[2];
    return 1;
  }

  auto lerpSV = [](const ShadeVertex &a, const ShadeVertex &b,
                   float t) -> ShadeVertex {
    ShadeVertex r;
    r.position = {a.position.x + t * (b.position.x - a.position.x),
                  a.position.y + t * (b.position.y - a.position.y),
                  a.position.z + t * (b.position.z - a.position.z)};
    r.normal = Normalize({a.normal.x + t * (b.normal.x - a.normal.x),
                          a.normal.y + t * (b.normal.y - a.normal.y),
                          a.normal.z + t * (b.normal.z - a.normal.z)});
    r.color = {
        static_cast<uint8_t>(a.color.r + t * (static_cast<float>(b.color.r) - a.color.r)),
        static_cast<uint8_t>(a.color.g + t * (static_cast<float>(b.color.g) - a.color.g)),
        static_cast<uint8_t>(a.color.b + t * (static_cast<float>(b.color.b) - a.color.b)),
        255};
    r.ndotl = a.ndotl + t * (b.ndotl - a.ndotl);
    r.nDotH = a.nDotH + t * (b.nDotH - a.nDotH);
    r.base_shade = static_cast<uint8_t>(
        a.base_shade + t * (static_cast<float>(b.base_shade) - a.base_shade));
    r.uv = {a.uv.u + t * (b.uv.u - a.uv.u), a.uv.v + t * (b.uv.v - a.uv.v)};
    r.diff_rgb = {a.diff_rgb.x + t * (b.diff_rgb.x - a.diff_rgb.x),
                  a.diff_rgb.y + t * (b.diff_rgb.y - a.diff_rgb.y),
                  a.diff_rgb.z + t * (b.diff_rgb.z - a.diff_rgb.z)};
    r.spec_rgb = {a.spec_rgb.x + t * (b.spec_rgb.x - a.spec_rgb.x),
                  a.spec_rgb.y + t * (b.spec_rgb.y - a.spec_rgb.y),
                  a.spec_rgb.z + t * (b.spec_rgb.z - a.spec_rgb.z)};
    r.has_rgb_light = a.has_rgb_light || b.has_rgb_light;
    return r;
  };

  auto clipT = [&](const ShadeVertex &a, const ShadeVertex &b) -> float {
    const float denom = b.position.z - a.position.z;
    if (std::abs(denom) < 1e-9f)
      return 0.0f;
    return ClampFloat((NP - a.position.z) / denom, 0.0f, 1.0f);
  };

  if (cnt == 2) {
    int out_i = inside[0] ? (inside[1] ? 2 : 1) : 0;
    const ShadeVertex &O = in_v[out_i];
    const ShadeVertex &A = in_v[(out_i + 1) % 3];
    const ShadeVertex &B = in_v[(out_i + 2) % 3];
    ShadeVertex CA = lerpSV(A, O, clipT(A, O));
    ShadeVertex CB = lerpSV(B, O, clipT(B, O));
    out_triangles[0][0] = A;
    out_triangles[0][1] = B;
    out_triangles[0][2] = CA;
    out_triangles[1][0] = B;
    out_triangles[1][1] = CB;
    out_triangles[1][2] = CA;
    return 2;
  } else {
    int in_i = inside[0] ? 0 : (inside[1] ? 1 : 2);
    const ShadeVertex &I = in_v[in_i];
    const ShadeVertex &A = in_v[(in_i + 1) % 3];
    const ShadeVertex &B = in_v[(in_i + 2) % 3];
    out_triangles[0][0] = I;
    out_triangles[0][1] = lerpSV(I, A, clipT(I, A));
    out_triangles[0][2] = lerpSV(I, B, clipT(I, B));
    return 1;
  }
}

bool Rasterizer::ClipAndProjectLine(const Vec3 &view_a, const Vec3 &view_b,
                                    Color color, DepthOp depthOp,
                                    float near_plane, const Mat4 &proj_matrix,
                                    const ViewportTransform &viewport,
                                    int pix_w, int pix_h,
                                    uint32_t submission_order,
                                    ProjectedLine &out_line) {
  const float NP = std::max(1e-3f, near_plane);
  Vec3 a = view_a;
  Vec3 b = view_b;

  if (a.z < NP && b.z < NP)
    return false;

  if (a.z < NP) {
    const float t = (NP - a.z) / (b.z - a.z);
    a.x = a.x + t * (b.x - a.x);
    a.y = a.y + t * (b.y - a.y);
    a.z = NP;
  } else if (b.z < NP) {
    const float t = (NP - b.z) / (a.z - b.z);
    b.x = b.x + t * (a.x - b.x);
    b.y = b.y + t * (a.y - b.y);
    b.z = NP;
  }

  Vec3 sa{}, sb{};
  float ooz0 = 0.0f, ooz1 = 0.0f;
  ProjectPoint(a, proj_matrix, viewport, sa, ooz0);
  ProjectPoint(b, proj_matrix, viewport, sb, ooz1);

  int32_t x0 = static_cast<int32_t>(std::round(sa.x - 0.5f));
  int32_t y0 = static_cast<int32_t>(std::round(sa.y - 0.5f));
  int32_t x1 = static_cast<int32_t>(std::round(sb.x - 0.5f));
  int32_t y1 = static_cast<int32_t>(std::round(sb.y - 0.5f));

  const int min_x = std::max(0, static_cast<int>(std::min(x0, x1)));
  const int min_y = std::max(0, static_cast<int>(std::min(y0, y1)));
  const int max_x = std::min(pix_w - 1, static_cast<int>(std::max(x0, x1)));
  const int max_y = std::min(pix_h - 1, static_cast<int>(std::max(y0, y1)));
  if (min_x > max_x || min_y > max_y)
    return false;

  out_line.x0 = x0;
  out_line.y0 = y0;
  out_line.x1 = x1;
  out_line.y1 = y1;
  out_line.ooz0 = ooz0;
  out_line.ooz1 = ooz1;
  out_line.color = color;
  out_line.depthOp = depthOp;
  out_line.bounding_box = {min_x, min_y, max_x, max_y};
  out_line.submission_order = submission_order;
  return true;
}

void Rasterizer::RasterizeLineInTile(const RasterContext &ctx,
                                     const ProjectedLine &line, int tile_x,
                                     int tile_y, Pixel *local_fb,
                                     float *local_zb, int tile_size) {
  const int t_min_x = tile_x * tile_size;
  const int t_min_y = tile_y * tile_size;
  const int t_max_x = t_min_x + tile_size - 1;
  const int t_max_y = t_min_y + tile_size - 1;

  if (!line.bounding_box.Overlaps({t_min_x, t_min_y, t_max_x, t_max_y}))
    return;

  const int32_t dx = std::abs(line.x1 - line.x0);
  const int32_t dy = std::abs(line.y1 - line.y0);
  const int32_t steps = std::max(dx, dy);

  if (steps == 0) {
    if (line.x0 >= t_min_x && line.x0 <= t_max_x && line.y0 >= t_min_y &&
        line.y0 <= t_max_y) {
      const int idx =
          (line.y0 - t_min_y) * tile_size + (line.x0 - t_min_x);
      if (line.depthOp == DepthOp::Always || line.ooz0 > local_zb[idx]) {
        local_zb[idx] = line.ooz0;
        local_fb[idx] = {line.color.r, line.color.g, line.color.b, 255};
      }
    }
    return;
  }

  const float inv_steps = 1.0f / static_cast<float>(steps);
  for (int32_t s = 0; s <= steps; ++s) {
    const float t = static_cast<float>(s) * inv_steps;
    const int px = static_cast<int>(
        std::round(static_cast<float>(line.x0) + t * (line.x1 - line.x0)));
    const int py = static_cast<int>(
        std::round(static_cast<float>(line.y0) + t * (line.y1 - line.y0)));

    if (px < t_min_x || px > t_max_x || py < t_min_y || py > t_max_y)
      continue;

    const float ooz = line.ooz0 + t * (line.ooz1 - line.ooz0);
    const int idx = (py - t_min_y) * tile_size + (px - t_min_x);

    if (line.depthOp == DepthOp::Always || ooz > local_zb[idx]) {
      local_zb[idx] = ooz;
      uint8_t r = line.color.r;
      uint8_t g = line.color.g;
      uint8_t b = line.color.b;
      if (ctx.fog_enabled) {
        const float vz = 1.0f / std::max(ooz, 1e-6f);
        const float fog_t =
            ClampFloat((vz - ctx.fog_start) * ctx.fog_inv_range, 0.0f, 1.0f);
        r = static_cast<uint8_t>(r + fog_t * (ctx.fog_r - r));
        g = static_cast<uint8_t>(g + fog_t * (ctx.fog_g - g));
        b = static_cast<uint8_t>(b + fog_t * (ctx.fog_b - b));
      }
      local_fb[idx] = {r, g, b, 255};
    }
  }
}

void Rasterizer::RasterizeProjectedTriangle(
    int, const ShadeVertex view_verts[3], const PipelineState &pso,
    const Mat4 &proj_matrix, const ViewportTransform &viewport, int pix_w,
    int pix_h, ProjectedTriangle *tri_array, uint32_t &tri_arena_size,
    uint32_t max_tris_per_thread, uint32_t submission_order) {
  if (tri_arena_size >= max_tris_per_thread)
    return;

  Vec3 p[3];
  float ooz[3];
  ProjectPoint(view_verts[0].position, proj_matrix, viewport, p[0], ooz[0]);
  ProjectPoint(view_verts[1].position, proj_matrix, viewport, p[1], ooz[1]);
  ProjectPoint(view_verts[2].position, proj_matrix, viewport, p[2], ooz[2]);

  const float area = (p[1].x - p[0].x) * (p[2].y - p[0].y) -
                     (p[2].x - p[0].x) * (p[1].y - p[0].y);
  if (pso.cullMode == CullMode::Back && area >= 0.0f)
    return;
  if (pso.cullMode == CullMode::Front && area <= 0.0f)
    return;
  if (std::abs(area) < 0.5f)
    return;

  const int min_x =
      std::max(0, static_cast<int>(std::min({p[0].x, p[1].x, p[2].x})));
  const int min_y =
      std::max(0, static_cast<int>(std::min({p[0].y, p[1].y, p[2].y})));
  const int max_x = std::min(
      pix_w - 1, static_cast<int>(std::max({p[0].x, p[1].x, p[2].x})) + 1);
  const int max_y = std::min(
      pix_h - 1, static_cast<int>(std::max({p[0].y, p[1].y, p[2].y})) + 1);
  if (min_x > max_x || min_y > max_y)
    return;

  const uint32_t tri_idx = tri_arena_size++;
  ProjectedTriangle &pt = tri_array[tri_idx];

  pt.p0 = p[0];
  pt.p1 = p[1];
  pt.p2 = p[2];
  for (int i = 0; i < 3; ++i)
    pt.ooz[i] = ooz[i];
  pt.ndotl[0] = view_verts[0].ndotl;
  pt.ndotl[1] = view_verts[1].ndotl;
  pt.ndotl[2] = view_verts[2].ndotl;

  const float shin = pso.material.shininess;
  float vr[3], vg[3], vb[3];
  for (int i = 0; i < 3; ++i) {
    const float h = std::max(0.0f, view_verts[i].nDotH);
    pt.spec[i] = pso.material.specular * std::pow(h, shin);

    float dr, dg, db, sr, sg, sb;
    if (view_verts[i].has_rgb_light) {
      dr = view_verts[i].diff_rgb.x;
      dg = view_verts[i].diff_rgb.y;
      db = view_verts[i].diff_rgb.z;
      sr = pso.material.specular * view_verts[i].spec_rgb.x;
      sg = pso.material.specular * view_verts[i].spec_rgb.y;
      sb = pso.material.specular * view_verts[i].spec_rgb.z;
    } else {
      dr = dg = db = view_verts[i].ndotl;
      sr = sg = sb = pt.spec[i];
    }

    const float diff_factor_r =
        std::min(1.5f, pso.material.ambient + pso.material.diffuse * dr);
    const float diff_factor_g =
        std::min(1.5f, pso.material.ambient + pso.material.diffuse * dg);
    const float diff_factor_b =
        std::min(1.5f, pso.material.ambient + pso.material.diffuse * db);

    vr[i] = ClampFloat(
        static_cast<float>(view_verts[i].color.r) * diff_factor_r + 255.0f * sr,
        0.0f, 255.0f);
    vg[i] = ClampFloat(
        static_cast<float>(view_verts[i].color.g) * diff_factor_g + 255.0f * sg,
        0.0f, 255.0f);
    vb[i] = ClampFloat(
        static_cast<float>(view_verts[i].color.b) * diff_factor_b + 255.0f * sb,
        0.0f, 255.0f);
  }

  if (area < 0.0f) {
    std::swap(pt.p1, pt.p2);
    std::swap(pt.ooz[1], pt.ooz[2]);
    std::swap(pt.ndotl[1], pt.ndotl[2]);
    std::swap(pt.spec[1], pt.spec[2]);
    std::swap(vr[1], vr[2]);
    std::swap(vg[1], vg[2]);
    std::swap(vb[1], vb[2]);
  }

  int32_t p0x = static_cast<int32_t>(std::round(pt.p0.x - 0.5f));
  int32_t p0y = static_cast<int32_t>(std::round(pt.p0.y - 0.5f));
  int32_t p1x = static_cast<int32_t>(std::round(pt.p1.x - 0.5f));
  int32_t p1y = static_cast<int32_t>(std::round(pt.p1.y - 0.5f));
  int32_t p2x = static_cast<int32_t>(std::round(pt.p2.x - 0.5f));
  int32_t p2y = static_cast<int32_t>(std::round(pt.p2.y - 0.5f));

  const int64_t area64 =
      static_cast<int64_t>(p1x - p0x) * static_cast<int64_t>(p2y - p0y) -
      static_cast<int64_t>(p2x - p0x) * static_cast<int64_t>(p1y - p0y);
  if (area64 <= 0) {
    tri_arena_size--;
    return;
  }
  pt.inv_area = 1.0f / static_cast<float>(area64);

  const float dx12 = static_cast<float>(p1y - p2y);
  const float dy12 = static_cast<float>(p2x - p1x);
  const float dx20 = static_cast<float>(p2y - p0y);
  const float dy20 = static_cast<float>(p0x - p2x);
  const float dx01 = static_cast<float>(p0y - p1y);
  const float dy01 = static_cast<float>(p1x - p0x);

  pt.dz_dx =
      (dx12 * pt.ooz[0] + dx20 * pt.ooz[1] + dx01 * pt.ooz[2]) * pt.inv_area;
  pt.dz_dy =
      (dy12 * pt.ooz[0] + dy20 * pt.ooz[1] + dy01 * pt.ooz[2]) * pt.inv_area;

  const float nd0 = pt.ndotl[0] * pt.ooz[0];
  const float nd1 = pt.ndotl[1] * pt.ooz[1];
  const float nd2 = pt.ndotl[2] * pt.ooz[2];
  pt.dndotl_dx = (dx12 * nd0 + dx20 * nd1 + dx01 * nd2) * pt.inv_area;
  pt.dndotl_dy = (dy12 * nd0 + dy20 * nd1 + dy01 * nd2) * pt.inv_area;

  const float sp0 = pt.spec[0] * pt.ooz[0];
  const float sp1 = pt.spec[1] * pt.ooz[1];
  const float sp2 = pt.spec[2] * pt.ooz[2];
  pt.dspec_dx = (dx12 * sp0 + dx20 * sp1 + dx01 * sp2) * pt.inv_area;
  pt.dspec_dy = (dy12 * sp0 + dy20 * sp1 + dy01 * sp2) * pt.inv_area;

  for (int i = 0; i < 3; ++i) {
    pt.col_r[i] = vr[i] * pt.ooz[i];
    pt.col_g[i] = vg[i] * pt.ooz[i];
    pt.col_b[i] = vb[i] * pt.ooz[i];
  }
  pt.dr_dx = (dx12 * pt.col_r[0] + dx20 * pt.col_r[1] + dx01 * pt.col_r[2]) *
             pt.inv_area;
  pt.dr_dy = (dy12 * pt.col_r[0] + dy20 * pt.col_r[1] + dy01 * pt.col_r[2]) *
             pt.inv_area;
  pt.dg_dx = (dx12 * pt.col_g[0] + dx20 * pt.col_g[1] + dx01 * pt.col_g[2]) *
             pt.inv_area;
  pt.dg_dy = (dy12 * pt.col_g[0] + dy20 * pt.col_g[1] + dy01 * pt.col_g[2]) *
             pt.inv_area;
  pt.db_dx = (dx12 * pt.col_b[0] + dx20 * pt.col_b[1] + dx01 * pt.col_b[2]) *
             pt.inv_area;
  pt.db_dy = (dy12 * pt.col_b[0] + dy20 * pt.col_b[1] + dy01 * pt.col_b[2]) *
             pt.inv_area;

  pt.albedo = view_verts[0].color;
  pt.max_inv_z = std::max({pt.ooz[0], pt.ooz[1], pt.ooz[2]});
  pt.ambient = pso.material.ambient;
  pt.diffuse = pso.material.diffuse;
  pt.specular = pso.material.specular;
  pt.submission_order = submission_order;
  pt.depthOp = pso.depthOp;
  pt.cullMode = pso.cullMode;
  pt.bounding_box = {min_x, min_y, max_x, max_y};
  pt.base_shade = view_verts[0].base_shade;
}

namespace {
template <bool FogEnabled, DepthOp DOp>
void ProcessTileBandImpl(const RasterContext &ctx, const ProjectedTriangle &tri,
                         int tile_x, int tile_y, Pixel *local_fb,
                         float *local_zb, int tile_size) {
  const Vec3 &p0 = tri.p0;
  const Vec3 &p1 = tri.p1;
  const Vec3 &p2 = tri.p2;
  const Rect &bb = tri.bounding_box;

  const int t_min_x = tile_x * tile_size;
  const int t_min_y = tile_y * tile_size;

  const int x0 = std::max(bb.min_x, t_min_x);
  const int y0 = std::max(bb.min_y, t_min_y);
  const int x1 = std::min(bb.max_x, t_min_x + tile_size - 1);
  const int y1 = std::min(bb.max_y, t_min_y + tile_size - 1);
  if (x0 > x1 || y0 > y1)
    return;

  const int32_t p0x = static_cast<int32_t>(std::round(p0.x - 0.5f));
  const int32_t p0y = static_cast<int32_t>(std::round(p0.y - 0.5f));
  const int32_t p1x = static_cast<int32_t>(std::round(p1.x - 0.5f));
  const int32_t p1y = static_cast<int32_t>(std::round(p1.y - 0.5f));
  const int32_t p2x = static_cast<int32_t>(std::round(p2.x - 0.5f));
  const int32_t p2y = static_cast<int32_t>(std::round(p2.y - 0.5f));

  const int32_t dx01 = p0y - p1y, dy01 = p1x - p0x;
  const int32_t dx12 = p1y - p2y, dy12 = p2x - p1x;
  const int32_t dx20 = p2y - p0y, dy20 = p0x - p2x;

  const int32_t bias01 = TopLeftBias(dx01, dy01);
  const int32_t bias12 = TopLeftBias(dx12, dy12);
  const int32_t bias20 = TopLeftBias(dx20, dy20);

#if defined(TVK_SIMD_AVX2) && defined(__AVX2__)
  const __m256 v_255 = _mm256_set1_ps(255.0f);
  const __m256 v_zero = _mm256_setzero_ps();
  const __m256 v_two = _mm256_set1_ps(2.0f);

  const __m256 v_fog_start = _mm256_set1_ps(ctx.fog_start);
  const __m256 v_fog_inv_r = _mm256_set1_ps(ctx.fog_inv_range);
  const __m256 v_fog_r_col = _mm256_set1_ps(ctx.fog_r);
  const __m256 v_fog_g_col = _mm256_set1_ps(ctx.fog_g);
  const __m256 v_fog_b_col = _mm256_set1_ps(ctx.fog_b);
#endif

  for (int py = y0; py <= y1; ++py) {
    int32_t e01_row = static_cast<int32_t>(
        static_cast<int64_t>(x0 - p0x) * dx01 +
        static_cast<int64_t>(py - p0y) * dy01 + bias01);
    int32_t e12_row = static_cast<int32_t>(
        static_cast<int64_t>(x0 - p1x) * dx12 +
        static_cast<int64_t>(py - p1y) * dy12 + bias12);
    int32_t e20_row = static_cast<int32_t>(
        static_cast<int64_t>(x0 - p2x) * dx20 +
        static_cast<int64_t>(py - p2y) * dy20 + bias20);

    int scan_min_x = x0;
    int scan_max_x = x1;

    auto intersect = [&](int32_t e_row, int32_t dx) {
      if (dx > 0) {
        if (e_row < 0) {
          int steps = (-e_row + dx - 1) / dx;
          scan_min_x = std::max(scan_min_x, x0 + steps);
        }
      } else if (dx < 0) {
        if (e_row < 0) {
          scan_max_x = -1;
        } else {
          int steps = e_row / (-dx);
          scan_max_x = std::min(scan_max_x, x0 + steps);
        }
      } else {
        if (e_row < 0)
          scan_max_x = -1;
      }
    };

    intersect(e01_row, dx01);
    intersect(e12_row, dx12);
    intersect(e20_row, dx20);

    if (scan_min_x > scan_max_x)
      continue;

    const int local_py = py - t_min_y;
    const int row_idx = local_py * tile_size;

#if defined(TVK_SIMD_AVX2) && defined(__AVX2__)
    int aligned_min_x = scan_min_x & ~7;
    if (aligned_min_x > scan_max_x)
      continue;

    int step_delta = (aligned_min_x - x0);
    e01_row += step_delta * dx01;
    e12_row += step_delta * dx12;
    e20_row += step_delta * dx20;

    const __m256i v_dx01 = _mm256_set1_epi32(dx01 * 8);
    const __m256i v_dx12 = _mm256_set1_epi32(dx12 * 8);
    const __m256i v_dx20 = _mm256_set1_epi32(dx20 * 8);

    const __m256i v_step_e01 = _mm256_set_epi32(
        dx01 * 7, dx01 * 6, dx01 * 5, dx01 * 4, dx01 * 3, dx01 * 2, dx01, 0);
    const __m256i v_step_e12 = _mm256_set_epi32(
        dx12 * 7, dx12 * 6, dx12 * 5, dx12 * 4, dx12 * 3, dx12 * 2, dx12, 0);
    const __m256i v_step_e20 = _mm256_set_epi32(
        dx20 * 7, dx20 * 6, dx20 * 5, dx20 * 4, dx20 * 3, dx20 * 2, dx20, 0);

    __m256i ve01 = _mm256_add_epi32(_mm256_set1_epi32(e01_row), v_step_e01);
    __m256i ve12 = _mm256_add_epi32(_mm256_set1_epi32(e12_row), v_step_e12);
    __m256i ve20 = _mm256_add_epi32(_mm256_set1_epi32(e20_row), v_step_e20);

    const __m256i v_minus_one = _mm256_set1_epi32(-1);
    const __m256 v_ramp = _mm256_setr_ps(0, 1, 2, 3, 4, 5, 6, 7);

    float vooz_row = tri.ooz[0] + (aligned_min_x - p0x) * tri.dz_dx +
                     (py - p0y) * tri.dz_dy;
    float r_row = tri.col_r[0] + (aligned_min_x - p0x) * tri.dr_dx +
                  (py - p0y) * tri.dr_dy;
    float g_row = tri.col_g[0] + (aligned_min_x - p0x) * tri.dg_dx +
                  (py - p0y) * tri.dg_dy;
    float b_row = tri.col_b[0] + (aligned_min_x - p0x) * tri.db_dx +
                  (py - p0y) * tri.db_dy;

    __m256 v_vooz_base = _mm256_add_ps(
        _mm256_set1_ps(vooz_row),
        _mm256_mul_ps(v_ramp, _mm256_set1_ps(tri.dz_dx)));
    __m256 v_r_base = _mm256_add_ps(
        _mm256_set1_ps(r_row),
        _mm256_mul_ps(v_ramp, _mm256_set1_ps(tri.dr_dx)));
    __m256 v_g_base = _mm256_add_ps(
        _mm256_set1_ps(g_row),
        _mm256_mul_ps(v_ramp, _mm256_set1_ps(tri.dg_dx)));
    __m256 v_b_base = _mm256_add_ps(
        _mm256_set1_ps(b_row),
        _mm256_mul_ps(v_ramp, _mm256_set1_ps(tri.db_dx)));

    const __m256 v_dz_dx_8 = _mm256_set1_ps(tri.dz_dx * 8.0f);
    const __m256 v_dr_dx_8 = _mm256_set1_ps(tri.dr_dx * 8.0f);
    const __m256 v_dg_dx_8 = _mm256_set1_ps(tri.dg_dx * 8.0f);
    const __m256 v_db_dx_8 = _mm256_set1_ps(tri.db_dx * 8.0f);

    for (int px = aligned_min_x; px <= scan_max_x; px += 8,
             ve01 = _mm256_add_epi32(ve01, v_dx01),
             ve12 = _mm256_add_epi32(ve12, v_dx12),
             ve20 = _mm256_add_epi32(ve20, v_dx20),
             v_vooz_base = _mm256_add_ps(v_vooz_base, v_dz_dx_8),
             v_r_base = _mm256_add_ps(v_r_base, v_dr_dx_8),
             v_g_base = _mm256_add_ps(v_g_base, v_dg_dx_8),
             v_b_base = _mm256_add_ps(v_b_base, v_db_dx_8)) {
      const __m256i mask_i = _mm256_and_si256(
          _mm256_and_si256(_mm256_cmpgt_epi32(ve01, v_minus_one),
                           _mm256_cmpgt_epi32(ve12, v_minus_one)),
          _mm256_cmpgt_epi32(ve20, v_minus_one));

      __m256i bounds_mask_i = _mm256_set1_epi32(-1);
      int strict_end = std::min(scan_max_x, x1);
      if (px + 8 > strict_end + 1) {
        const int valid = (strict_end + 1) - px;
        const __m256i v_indices = _mm256_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7);
        bounds_mask_i = _mm256_cmpgt_epi32(_mm256_set1_epi32(valid), v_indices);
      }

      const __m256i final_mask_i = _mm256_and_si256(mask_i, bounds_mask_i);
      __m256 mask = _mm256_castsi256_ps(final_mask_i);
      int imask = _mm256_movemask_ps(mask);

      if (imask == 0)
        continue;

      int local_px = px - t_min_x;
      __m256 old_z = _mm256_loadu_ps(&local_zb[row_idx + local_px]);
      __m256 z_mask;
      if constexpr (DOp == DepthOp::Less) {
        z_mask = _mm256_cmp_ps(v_vooz_base, old_z, _CMP_GT_OQ);
      } else {
        z_mask = _mm256_castsi256_ps(_mm256_set1_epi32(-1));
      }

      __m256 valid_mask = _mm256_and_ps(z_mask, mask);
      int valid_mask_int = _mm256_movemask_ps(valid_mask);
      if (valid_mask_int == 0)
        continue;

      __m256 v_safe_vooz = _mm256_max_ps(v_vooz_base, _mm256_set1_ps(1e-6f));
      __m256 v_inv_vooz = _mm256_rcp_ps(v_safe_vooz);
      v_inv_vooz = _mm256_mul_ps(
          v_inv_vooz, _mm256_fnmadd_ps(v_safe_vooz, v_inv_vooz, v_two));

      __m256 v_r = _mm256_mul_ps(v_r_base, v_inv_vooz);
      __m256 v_g = _mm256_mul_ps(v_g_base, v_inv_vooz);
      __m256 v_b = _mm256_mul_ps(v_b_base, v_inv_vooz);

      if constexpr (FogEnabled) {
        __m256 v_vz = v_inv_vooz;
        __m256 v_fog_t =
            _mm256_mul_ps(_mm256_sub_ps(v_vz, v_fog_start), v_fog_inv_r);
        v_fog_t =
            _mm256_max_ps(v_zero, _mm256_min_ps(v_fog_t, _mm256_set1_ps(1.0f)));

        v_r = _mm256_fmadd_ps(v_fog_t, _mm256_sub_ps(v_fog_r_col, v_r), v_r);
        v_g = _mm256_fmadd_ps(v_fog_t, _mm256_sub_ps(v_fog_g_col, v_g), v_g);
        v_b = _mm256_fmadd_ps(v_fog_t, _mm256_sub_ps(v_fog_b_col, v_b), v_b);
      }

      v_r = _mm256_min_ps(_mm256_max_ps(v_r, v_zero), v_255);
      v_g = _mm256_min_ps(_mm256_max_ps(v_g, v_zero), v_255);
      v_b = _mm256_min_ps(_mm256_max_ps(v_b, v_zero), v_255);

      __m256i vi_r = _mm256_cvtps_epi32(v_r);
      __m256i vi_g = _mm256_cvtps_epi32(v_g);
      __m256i vi_b = _mm256_cvtps_epi32(v_b);

      __m256i vi_g_shifted = _mm256_slli_epi32(vi_g, 8);
      __m256i vi_b_shifted = _mm256_slli_epi32(vi_b, 16);
      __m256i v_alpha = _mm256_set1_epi32(static_cast<int>(0xFF000000u));

      __m256i rgba = _mm256_or_si256(_mm256_or_si256(vi_r, vi_g_shifted),
                                     _mm256_or_si256(vi_b_shifted, v_alpha));

      __m256i store_mask = _mm256_castps_si256(valid_mask);
      _mm256_maskstore_ps(&local_zb[row_idx + local_px], store_mask,
                          v_vooz_base);
      _mm256_maskstore_epi32(
          reinterpret_cast<int *>(&local_fb[row_idx + local_px]), store_mask,
          rgba);
    }
#else
    const int step_delta = scan_min_x - x0;
    int32_t e01 = e01_row + step_delta * dx01;
    int32_t e12 = e12_row + step_delta * dx12;
    int32_t e20 = e20_row + step_delta * dx20;

    float vooz =
        tri.ooz[0] + (scan_min_x - p0x) * tri.dz_dx + (py - p0y) * tri.dz_dy;
    float r_oz =
        tri.col_r[0] + (scan_min_x - p0x) * tri.dr_dx + (py - p0y) * tri.dr_dy;
    float g_oz =
        tri.col_g[0] + (scan_min_x - p0x) * tri.dg_dx + (py - p0y) * tri.dg_dy;
    float b_oz =
        tri.col_b[0] + (scan_min_x - p0x) * tri.db_dx + (py - p0y) * tri.db_dy;

    const int strict_end = std::min(scan_max_x, x1);
    for (int px = scan_min_x; px <= strict_end; ++px, e01 += dx01, e12 += dx12,
             e20 += dx20, vooz += tri.dz_dx, r_oz += tri.dr_dx,
             g_oz += tri.dg_dx, b_oz += tri.db_dx) {
      if (e01 < 0 || e12 < 0 || e20 < 0)
        continue;

      const int idx = row_idx + (px - t_min_x);
      if constexpr (DOp == DepthOp::Less) {
        if (vooz <= local_zb[idx])
          continue;
      }

      const float inv_vooz = 1.0f / std::max(vooz, 1e-6f);
      float r = r_oz * inv_vooz;
      float g = g_oz * inv_vooz;
      float b = b_oz * inv_vooz;

      if constexpr (FogEnabled) {
        const float fog_t = ClampFloat(
            (inv_vooz - ctx.fog_start) * ctx.fog_inv_range, 0.0f, 1.0f);
        r += fog_t * (ctx.fog_r - r);
        g += fog_t * (ctx.fog_g - g);
        b += fog_t * (ctx.fog_b - b);
      }

      local_zb[idx] = vooz;
      local_fb[idx] = {
          static_cast<uint8_t>(std::round(ClampFloat(r, 0.0f, 255.0f))),
          static_cast<uint8_t>(std::round(ClampFloat(g, 0.0f, 255.0f))),
          static_cast<uint8_t>(std::round(ClampFloat(b, 0.0f, 255.0f))), 255};
    }
#endif
  }
}
} // namespace

void Rasterizer::ProcessTileBand(const RasterContext &ctx,
                                 const ProjectedTriangle &tri, int tile_x,
                                 int tile_y, Pixel *local_fb, float *local_zb,
                                 int tile_size) {
  if (ctx.fog_enabled) {
    if (tri.depthOp == DepthOp::Less)
      ProcessTileBandImpl<true, DepthOp::Less>(ctx, tri, tile_x, tile_y,
                                               local_fb, local_zb, tile_size);
    else
      ProcessTileBandImpl<true, DepthOp::Always>(ctx, tri, tile_x, tile_y,
                                                 local_fb, local_zb, tile_size);
  } else {
    if (tri.depthOp == DepthOp::Less)
      ProcessTileBandImpl<false, DepthOp::Less>(ctx, tri, tile_x, tile_y,
                                                local_fb, local_zb, tile_size);
    else
      ProcessTileBandImpl<false, DepthOp::Always>(
          ctx, tri, tile_x, tile_y, local_fb, local_zb, tile_size);
  }
}

} // namespace tvk
