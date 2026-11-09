#pragma once
#include <vector>
#include <cstdint>
#include "tvk/TVK_Types.h"
#include "tvk/TVK_Math.h"
#include "tvk/TVK_CommandBuffer.h"

namespace tvk {

    // Single source of truth for near-plane distance
    static constexpr float NEAR_PLANE = 4.0f;

    // =========================================================================
    // Viewport transform
    // =========================================================================

    struct ViewportTransform {
        float half_w;   ///< pix_w / 2.0f
        float half_h;   ///< pix_h / 2.0f
        float offset_x; ///< Sub-viewport X offset (0 for fullscreen)
        float offset_y;
    };

    // =========================================================================
    // Rect
    // =========================================================================

    struct Rect {
        int min_x, min_y;
        int max_x, max_y;
        bool Overlaps(const Rect& o) const {
            return !(min_x > o.max_x || max_x < o.min_x || min_y > o.max_y || max_y < o.min_y);
        }
    };

    // =========================================================================
    // ShadeVertex  — post-vertex-shader data passed to the geometry stage
    // =========================================================================

    struct ShadeVertex {
        Vec3    position;    ///< View-space position
        Vec3    normal;      ///< View-space normal (unit length)
        Color   color;       ///< Albedo / vertex colour
        float   ndotl;       ///< Accumulated diffuse N·L across all lights [0, 1]
        float   nDotH;       ///< N·H half-vector dot for Blinn-Phong specular [0, 1]
        uint8_t base_shade;  ///< Legacy shade byte (for point-draw primitives)
        Vec2    uv;          ///< Texture coordinate (not yet sampled; for future texturing)
    };

    // =========================================================================
    // ProjectedTriangle  — screen-space triangle ready for tile rasterization
    // =========================================================================

    struct ProjectedTriangle {
        // Screen-space positions (x, y) and view-space 1/z stored in .z
        Vec3  p0, p1, p2;

        // Per-vertex depth reciprocals (for perspective-correct interpolation)
        float ooz[3];

        // Per-vertex diffuse lighting  (stored pre-multiplied by ooz for lerp)
        float ndotl[3];

        // Per-vertex precomputed Gouraud specular term (stored pre-multiplied by ooz)
        float spec[3];

        // ── Material (per draw-call, set from PipelineState) ─────────────────
        Color albedo;     ///< Base triangle colour  ← WAS MISSING (bug #1 fixed)
        float ambient;    ///< Ambient coefficient   ← WAS MISSING (bug #2 fixed)
        float diffuse;    ///< Diffuse coefficient   ← WAS MISSING (bug #3 fixed)
        float specular;   ///< Specular coefficient  (new)

        // ── Linear Interpolation Gradients (Screen Space) ────────────────────
        float dz_dx, dz_dy;
        float dndotl_dx, dndotl_dy;
        float dspec_dx, dspec_dy;

        // ── Accelerated Sorting ──────────────────────────────────────────────
        float max_inv_z; // Highest 1/z (closest point) for fast front-to-back sorting

        // ── Rasterizer state ─────────────────────────────────────────────────
        uint8_t  base_shade;
        DepthOp  depthOp;
        CullMode cullMode;
        Rect     bounding_box;
        float    inv_area;
    };

    // =========================================================================
    // ProjectedPoint  — screen-space point for particle / point draws
    // =========================================================================

    struct ProjectedPoint {
        int     x, y;
        float   ooz;
        uint8_t r, g, b;  ///< Expanded from shade to full RGB
        DepthOp depthOp;
    };

    // (Linked list nodes moved to Renderer)

    // =========================================================================
    // RasterContext  — passed to ProcessTile workers (read-only except fb/zb)
    // =========================================================================

    struct RasterContext {
        Pixel*  framebuffer;
        float*  zbuffer;
        int     pix_w;
        int     pix_h;

        // Triangle arena (read-only during raster phase)
        const ProjectedTriangle* tri_array;

        // Point arena
        const ProjectedPoint*    pt_array;

        int         num_workers;

        // Arena layout strides (capacities per worker)
        uint32_t max_tris_per_thread;
        uint32_t max_pts_per_thread;

        // ── Fog parameters (zero-cost skip when fog_enabled == false) ─────────
        bool  fog_enabled   = false;
        float fog_start     = 50.0f;
        float fog_inv_range = 1.0f / 150.0f; ///< 1 / (fog_end - fog_start)
        float fog_r         = 8.0f;
        float fog_g         = 8.0f;
        float fog_b         = 16.0f;
    };

    // =========================================================================
    // Rasterizer
    // =========================================================================

    class Rasterizer {
    public:
        /// Rasterize a specific triangle restricted to a horizontal band
        static void ProcessTileBand(const RasterContext& ctx, 
                                    const ProjectedTriangle& tri, 
                                    int tile_x, int tile_y,
                                    Pixel* local_fb, float* local_zb, int tile_size);

        /// Sutherland-Hodgman near-plane clip.  Returns 0, 1 or 2 output triangles.
        static int ClipAgainstNearPlane(const ShadeVertex in_v[3],
                                        ShadeVertex out_triangles[2][3]);

        /// Project view-space triangle, compute specular, and insert into linear array.
        static void RasterizeProjectedTriangle(
            int worker_id,
            const ShadeVertex view_verts[3],
            const PipelineState& pso,
            const Mat4& proj_matrix,
            const ViewportTransform& viewport,
            int pix_w, int pix_h,
            ProjectedTriangle* tri_array,
            uint32_t& tri_arena_size,
            uint32_t  max_tris_per_thread);
    };

} // namespace tvk
