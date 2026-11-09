#include "tvk/renderer/Rasterizer.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <immintrin.h>

#ifdef _MSC_VER
#include <intrin.h>

static inline bool __sync_bool_compare_and_swap(volatile int32_t *ptr,
                                                int32_t oldval,
                                                int32_t newval)
{
  return _InterlockedCompareExchange(reinterpret_cast<volatile long *>(ptr),
                                     newval, oldval) == oldval;
}
static inline void __sync_fetch_and_xor_64(volatile uint64_t *ptr, uint64_t val)
{
  _InterlockedXor64(reinterpret_cast<volatile __int64 *>(ptr), val);
}
static inline int __builtin_ctz(unsigned int x)
{
  unsigned long idx;
  _BitScanForward(&idx, x);
  return static_cast<int>(idx);
}
#endif

namespace tvk
{

  int Rasterizer::ClipAgainstNearPlane(const ShadeVertex in_v[3],
                                       ShadeVertex out_triangles[2][3])
  {
    constexpr float NP = NEAR_PLANE;
    const bool inside[3] = {in_v[0].position.z >= NP, in_v[1].position.z >= NP,
                            in_v[2].position.z >= NP};
    const int cnt = (int)inside[0] + (int)inside[1] + (int)inside[2];

    if (cnt == 0)
      return 0;
    if (cnt == 3)
    {
      out_triangles[0][0] = in_v[0];
      out_triangles[0][1] = in_v[1];
      out_triangles[0][2] = in_v[2];
      return 1;
    }

    auto lerpSV = [](const ShadeVertex &a, const ShadeVertex &b,
                     float t) -> ShadeVertex
    {
      ShadeVertex r;
      r.position = {a.position.x + t * (b.position.x - a.position.x),
                    a.position.y + t * (b.position.y - a.position.y),
                    a.position.z + t * (b.position.z - a.position.z)};
      r.normal = Normalize({a.normal.x + t * (b.normal.x - a.normal.x),
                            a.normal.y + t * (b.normal.y - a.normal.y),
                            a.normal.z + t * (b.normal.z - a.normal.z)});
      r.color = {static_cast<uint8_t>(a.color.r + t * (b.color.r - a.color.r)),
                 static_cast<uint8_t>(a.color.g + t * (b.color.g - a.color.g)),
                 static_cast<uint8_t>(a.color.b + t * (b.color.b - a.color.b)),
                 255};
      r.ndotl = a.ndotl + t * (b.ndotl - a.ndotl);
      r.nDotH = a.nDotH + t * (b.nDotH - a.nDotH);
      r.base_shade =
          static_cast<uint8_t>(a.base_shade + t * (b.base_shade - a.base_shade));
      r.uv = {a.uv.u + t * (b.uv.u - a.uv.u), a.uv.v + t * (b.uv.v - a.uv.v)};
      return r;
    };

    auto clipT = [&](const ShadeVertex &a, const ShadeVertex &b) -> float
    {
      return (NP - a.position.z) / (b.position.z - a.position.z + 1e-9f);
    };

    if (cnt == 2)
    {

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
    }
    else
    {

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

  void Rasterizer::RasterizeProjectedTriangle(
      int, const ShadeVertex view_verts[3], const PipelineState &pso,
      const Mat4 &proj_matrix, const ViewportTransform &viewport, int pix_w,
      int pix_h, ProjectedTriangle *tri_array, uint32_t &tri_arena_size,
      uint32_t max_tris_per_thread)
  {

    if (tri_arena_size >= max_tris_per_thread)
      return;

    auto project = [&](const ShadeVertex &sv, Vec3 &out_screen, float &out_ooz)
    {
      const float z = sv.position.z;
      const float inv_z = (z > 1e-6f) ? (1.0f / z) : 0.0f;

      float cx = proj_matrix.m[0][0] * sv.position.x * inv_z;
      float cy = proj_matrix.m[1][1] * sv.position.y * inv_z;

      out_screen.x = (cx + 1.0f) * viewport.half_w + viewport.offset_x;
      out_screen.y = (-cy + 1.0f) * viewport.half_h + viewport.offset_y;
      out_screen.z = inv_z;
      out_ooz = inv_z;
    };

    Vec3 p[3];
    float ooz[3];
    project(view_verts[0], p[0], ooz[0]);
    project(view_verts[1], p[1], ooz[1]);
    project(view_verts[2], p[2], ooz[2]);

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
    for (int i = 0; i < 3; ++i)
    {
      const float h = std::max(0.0f, view_verts[i].nDotH);
      pt.spec[i] = pso.material.specular * std::pow(h, shin);
    }

    if (area < 0.0f)
    {
      std::swap(pt.p1, pt.p2);
      std::swap(pt.ooz[1], pt.ooz[2]);
      std::swap(pt.ndotl[1], pt.ndotl[2]);
      std::swap(pt.spec[1], pt.spec[2]);
    }

    int32_t p0x = static_cast<int32_t>(std::round(pt.p0.x - 0.5f));
    int32_t p0y = static_cast<int32_t>(std::round(pt.p0.y - 0.5f));
    int32_t p1x = static_cast<int32_t>(std::round(pt.p1.x - 0.5f));
    int32_t p1y = static_cast<int32_t>(std::round(pt.p1.y - 0.5f));
    int32_t p2x = static_cast<int32_t>(std::round(pt.p2.x - 0.5f));
    int32_t p2y = static_cast<int32_t>(std::round(pt.p2.y - 0.5f));

    float snapped_area = static_cast<float>((p1x - p0x) * (p2y - p0y) - (p2x - p0x) * (p1y - p0y));
    if (snapped_area <= 0.0f)
    {
      tri_arena_size--;
      return;
    }
    pt.inv_area = 1.0f / snapped_area;

    float dx12 = p1y - p2y;
    float dy12 = p2x - p1x;
    float dx20 = p2y - p0y;
    float dy20 = p0x - p2x;
    float dx01 = p0y - p1y;
    float dy01 = p1x - p0x;

    pt.dz_dx = (dx12 * pt.ooz[0] + dx20 * pt.ooz[1] + dx01 * pt.ooz[2]) * pt.inv_area;
    pt.dz_dy = (dy12 * pt.ooz[0] + dy20 * pt.ooz[1] + dy01 * pt.ooz[2]) * pt.inv_area;

    float nd0 = pt.ndotl[0] * pt.ooz[0];
    float nd1 = pt.ndotl[1] * pt.ooz[1];
    float nd2 = pt.ndotl[2] * pt.ooz[2];
    pt.dndotl_dx = (dx12 * nd0 + dx20 * nd1 + dx01 * nd2) * pt.inv_area;
    pt.dndotl_dy = (dy12 * nd0 + dy20 * nd1 + dy01 * nd2) * pt.inv_area;

    float sp0 = pt.spec[0] * pt.ooz[0];
    float sp1 = pt.spec[1] * pt.ooz[1];
    float sp2 = pt.spec[2] * pt.ooz[2];
    pt.dspec_dx = (dx12 * sp0 + dx20 * sp1 + dx01 * sp2) * pt.inv_area;
    pt.dspec_dy = (dy12 * sp0 + dy20 * sp1 + dy01 * sp2) * pt.inv_area;

    pt.albedo = view_verts[0].color;
    pt.max_inv_z = std::max({pt.ooz[0], pt.ooz[1], pt.ooz[2]});
    pt.ambient = pso.material.ambient;
    pt.diffuse = pso.material.diffuse;
    pt.specular = pso.material.specular;
    pt.depthOp = pso.depthOp;
    pt.cullMode = pso.cullMode;
    pt.bounding_box = {min_x, min_y, max_x, max_y};

    pt.base_shade = view_verts[0].base_shade;

  }

  namespace
  {
    template <bool FogEnabled, DepthOp DOp>
    void ProcessTileBandImpl(const RasterContext &ctx, const ProjectedTriangle &tri,
                             int tile_x, int tile_y, Pixel *local_fb, float *local_zb, int tile_size)
    {

      const Vec3 &p0 = tri.p0;
      const Vec3 &p1 = tri.p1;
      const Vec3 &p2 = tri.p2;
      const Rect &bb = tri.bounding_box;

      int t_min_x = tile_x * tile_size;
      int t_min_y = tile_y * tile_size;

      const int x0 = std::max(bb.min_x, t_min_x);
      const int y0 = std::max(bb.min_y, t_min_y);
      const int x1 = std::min(bb.max_x, t_min_x + tile_size - 1);
      const int y1 = std::min(bb.max_y, t_min_y + tile_size - 1);
      if (x0 > x1 || y0 > y1)
        return;

      const __m256 v_albedo_r = _mm256_set1_ps(static_cast<float>(tri.albedo.r));
      const __m256 v_albedo_g = _mm256_set1_ps(static_cast<float>(tri.albedo.g));
      const __m256 v_albedo_b = _mm256_set1_ps(static_cast<float>(tri.albedo.b));
      const __m256 v_ambient = _mm256_set1_ps(tri.ambient);
      const __m256 v_diffuse = _mm256_set1_ps(tri.diffuse);
      const __m256 v_255 = _mm256_set1_ps(255.0f);
      const __m256 v_zero = _mm256_setzero_ps();

      const int32_t p0x = static_cast<int32_t>(std::round(p0.x - 0.5f));
      const int32_t p0y = static_cast<int32_t>(std::round(p0.y - 0.5f));
      const int32_t p1x = static_cast<int32_t>(std::round(p1.x - 0.5f));
      const int32_t p1y = static_cast<int32_t>(std::round(p1.y - 0.5f));
      const int32_t p2x = static_cast<int32_t>(std::round(p2.x - 0.5f));
      const int32_t p2y = static_cast<int32_t>(std::round(p2.y - 0.5f));

      const int32_t dx01 = p0y - p1y, dy01 = p1x - p0x;
      const int32_t dx12 = p1y - p2y, dy12 = p2x - p1x;
      const int32_t dx20 = p2y - p0y, dy20 = p0x - p2x;

      __m256 v_fog_start = _mm256_set1_ps(ctx.fog_start);
      __m256 v_fog_inv_r = _mm256_set1_ps(ctx.fog_inv_range);
      __m256 v_fog_r_col = _mm256_set1_ps(ctx.fog_r);
      __m256 v_fog_g_col = _mm256_set1_ps(ctx.fog_g);
      __m256 v_fog_b_col = _mm256_set1_ps(ctx.fog_b);

      for (int py = y0; py <= y1; ++py)
      {

        int32_t e01_row = (x0 - p0x) * dx01 + (py - p0y) * dy01;
        int32_t e12_row = (x0 - p1x) * dx12 + (py - p1y) * dy12;
        int32_t e20_row = (x0 - p2x) * dx20 + (py - p2y) * dy20;

        int scan_min_x = x0;
        int scan_max_x = x1;

        auto intersect = [&](int32_t e_row, int32_t dx)
        {
          if (dx > 0)
          {
            if (e_row < 0)
            {
              int steps = (-e_row + dx - 1) / dx;
              scan_min_x = std::max(scan_min_x, x0 + steps);
            }
          }
          else if (dx < 0)
          {
            if (e_row < 0)
            {
              scan_max_x = -1;
            }
            else
            {
              int steps = e_row / (-dx);
              scan_max_x = std::min(scan_max_x, x0 + steps);
            }
          }
          else
          {
            if (e_row < 0)
              scan_max_x = -1;
          }
        };

        intersect(e01_row, dx01);
        intersect(e12_row, dx12);
        intersect(e20_row, dx20);

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

        const int local_py = py - t_min_y;
        const int row_idx = local_py * tile_size;
        const __m256i v_minus_one = _mm256_set1_epi32(-1);

        float vooz_row = tri.ooz[0] + (aligned_min_x - p0x) * tri.dz_dx + (py - p0y) * tri.dz_dy;
        float ndotl_row = (tri.ndotl[0] * tri.ooz[0]) + (aligned_min_x - p0x) * tri.dndotl_dx + (py - p0y) * tri.dndotl_dy;
        float spec_row = (tri.spec[0] * tri.ooz[0]) + (aligned_min_x - p0x) * tri.dspec_dx + (py - p0y) * tri.dspec_dy;

        __m256 v_vooz_base = _mm256_add_ps(_mm256_set1_ps(vooz_row), _mm256_mul_ps(_mm256_setr_ps(0, 1, 2, 3, 4, 5, 6, 7), _mm256_set1_ps(tri.dz_dx)));
        __m256 v_ndotl_base = _mm256_add_ps(_mm256_set1_ps(ndotl_row), _mm256_mul_ps(_mm256_setr_ps(0, 1, 2, 3, 4, 5, 6, 7), _mm256_set1_ps(tri.dndotl_dx)));
        __m256 v_spec_base = _mm256_add_ps(_mm256_set1_ps(spec_row), _mm256_mul_ps(_mm256_setr_ps(0, 1, 2, 3, 4, 5, 6, 7), _mm256_set1_ps(tri.dspec_dx)));

        const __m256 v_dz_dx_8 = _mm256_set1_ps(tri.dz_dx * 8.0f);
        const __m256 v_dndotl_dx_8 = _mm256_set1_ps(tri.dndotl_dx * 8.0f);
        const __m256 v_dspec_dx_8 = _mm256_set1_ps(tri.dspec_dx * 8.0f);

        for (int px = aligned_min_x; px <= scan_max_x; px += 8,
                 ve01 = _mm256_add_epi32(ve01, v_dx01),
                 ve12 = _mm256_add_epi32(ve12, v_dx12),
                 ve20 = _mm256_add_epi32(ve20, v_dx20),
                 v_vooz_base = _mm256_add_ps(v_vooz_base, v_dz_dx_8),
                 v_ndotl_base = _mm256_add_ps(v_ndotl_base, v_dndotl_dx_8),
                 v_spec_base = _mm256_add_ps(v_spec_base, v_dspec_dx_8))
        {

          const __m256i mask_i = _mm256_and_si256(
              _mm256_and_si256(_mm256_cmpgt_epi32(ve01, v_minus_one),
                               _mm256_cmpgt_epi32(ve12, v_minus_one)),
              _mm256_cmpgt_epi32(ve20, v_minus_one));

          __m256i bounds_mask_i = _mm256_set1_epi32(-1);
          int strict_end = std::min(scan_max_x, x1);
          if (px + 8 > strict_end + 1)
          {
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
          if constexpr (DOp == DepthOp::Less)
          {
            z_mask = _mm256_cmp_ps(v_vooz_base, old_z, _CMP_GT_OQ);
          }
          else
          {
            z_mask = _mm256_castsi256_ps(_mm256_set1_epi32(-1));
          }

          __m256 valid_mask = _mm256_and_ps(z_mask, mask);
          int valid_mask_int = _mm256_movemask_ps(valid_mask);
          if (valid_mask_int == 0)
            continue;

          __m256 v_inv_vooz = _mm256_rcp_ps(v_vooz_base);

          __m256 v_p_ndotl = _mm256_mul_ps(v_ndotl_base, v_inv_vooz);
          __m256 v_p_spec = _mm256_mul_ps(v_spec_base, v_inv_vooz);

          __m256 v_base_shade = _mm256_add_ps(v_ambient, v_p_spec);
          __m256 v_shade = _mm256_fmadd_ps(v_diffuse, v_p_ndotl, v_base_shade);

          v_shade = _mm256_min_ps(v_shade, _mm256_set1_ps(1.5f));

          __m256 v_r = _mm256_mul_ps(v_albedo_r, v_shade);
          __m256 v_g = _mm256_mul_ps(v_albedo_g, v_shade);
          __m256 v_b = _mm256_mul_ps(v_albedo_b, v_shade);

          if constexpr (FogEnabled)
          {
            __m256 v_vz = _mm256_rcp_ps(_mm256_max_ps(v_vooz_base, _mm256_set1_ps(1e-6f)));
            __m256 v_fog_t = _mm256_mul_ps(_mm256_sub_ps(v_vz, v_fog_start), v_fog_inv_r);

            v_fog_t = _mm256_max_ps(v_zero, _mm256_min_ps(v_fog_t, _mm256_set1_ps(1.0f)));

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
          __m256i v_alpha = _mm256_set1_epi32(0xFF000000u);

          __m256i rgba = _mm256_or_si256(
              _mm256_or_si256(vi_r, vi_g_shifted),
              _mm256_or_si256(vi_b_shifted, v_alpha));

          __m256i store_mask = _mm256_castps_si256(valid_mask);
          _mm256_maskstore_ps(&local_zb[row_idx + local_px], store_mask, v_vooz_base);
          _mm256_maskstore_epi32(reinterpret_cast<int *>(&local_fb[row_idx + local_px]), store_mask, rgba);
        }
      }
    }
  }

  void Rasterizer::ProcessTileBand(const RasterContext &ctx, const ProjectedTriangle &tri,
                                   int tile_x, int tile_y, Pixel *local_fb, float *local_zb, int tile_size)
  {
    if (ctx.fog_enabled)
    {
      if (tri.depthOp == DepthOp::Less)
        ProcessTileBandImpl<true, DepthOp::Less>(ctx, tri, tile_x, tile_y, local_fb, local_zb, tile_size);
      else
        ProcessTileBandImpl<true, DepthOp::Always>(ctx, tri, tile_x, tile_y, local_fb, local_zb, tile_size);
    }
    else
    {
      if (tri.depthOp == DepthOp::Less)
        ProcessTileBandImpl<false, DepthOp::Less>(ctx, tri, tile_x, tile_y, local_fb, local_zb, tile_size);
      else
        ProcessTileBandImpl<false, DepthOp::Always>(ctx, tri, tile_x, tile_y, local_fb, local_zb, tile_size);
    }
  }

}
