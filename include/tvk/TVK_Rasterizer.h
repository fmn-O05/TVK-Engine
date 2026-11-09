#pragma once
#include <vector>
#include <cstdint>
#include "tvk/TVK_Types.h"
#include "tvk/TVK_Math.h"
#include "tvk/TVK_CommandBuffer.h"

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
        bool Overlaps(const Rect& o) const {
            return !(min_x > o.max_x || max_x < o.min_x || min_y > o.max_y || max_y < o.min_y);
        }
    };

    struct ShadeVertex {
        Vec3    position;
        Vec3    normal;
        Color   color;
        float   ndotl;
        float   nDotH;
        uint8_t base_shade;
        Vec2    uv;
    };

    struct ProjectedTriangle {

        Vec3  p0, p1, p2;

        float ooz[3];

        float ndotl[3];

        float spec[3];

        Color albedo;
        float ambient;
        float diffuse;
        float specular;

        float dz_dx, dz_dy;
        float dndotl_dx, dndotl_dy;
        float dspec_dx, dspec_dy;

        float max_inv_z;

        uint8_t  base_shade;
        DepthOp  depthOp;
        CullMode cullMode;
        Rect     bounding_box;
        float    inv_area;
    };

    struct ProjectedPoint {
        int     x, y;
        float   ooz;
        uint8_t r, g, b;
        DepthOp depthOp;
    };

    struct RasterContext {
        Pixel*  framebuffer;
        float*  zbuffer;
        int     pix_w;
        int     pix_h;

        const ProjectedTriangle* tri_array;

        const ProjectedPoint*    pt_array;

        int         num_workers;

        uint32_t max_tris_per_thread;
        uint32_t max_pts_per_thread;

        bool  fog_enabled   = false;
        float fog_start     = 50.0f;
        float fog_inv_range = 1.0f / 150.0f;
        float fog_r         = 8.0f;
        float fog_g         = 8.0f;
        float fog_b         = 16.0f;
    };

    class Rasterizer {
    public:

        static void ProcessTileBand(const RasterContext& ctx,
                                    const ProjectedTriangle& tri,
                                    int tile_x, int tile_y,
                                    Pixel* local_fb, float* local_zb, int tile_size);

        static int ClipAgainstNearPlane(const ShadeVertex in_v[3],
                                        ShadeVertex out_triangles[2][3]);

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

}
