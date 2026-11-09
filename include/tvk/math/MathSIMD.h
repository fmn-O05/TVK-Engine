#pragma once
#include <immintrin.h>
#include <array>
#include "tvk/math/Math.h"

namespace tvk {
namespace simd {

    inline Vec3 NormalizeFast(Vec3 v) {
        __m128 vec  = _mm_set_ps(0.0f, v.z, v.y, v.x);
        __m128 dp   = _mm_dp_ps(vec, vec, 0x7F);
        __m128 rsq  = _mm_rsqrt_ps(dp);

        __m128 half = _mm_set1_ps(0.5f);
        __m128 c32  = _mm_set1_ps(1.5f);
        rsq = _mm_mul_ps(rsq, _mm_sub_ps(c32, _mm_mul_ps(half, _mm_mul_ps(dp, _mm_mul_ps(rsq,rsq)))));
        alignas(16) float r[4];
        _mm_store_ps(r, _mm_mul_ps(vec, rsq));
        return {r[0], r[1], r[2]};
    }

    inline __m256 Clamp01_8(__m256 v) {
        return _mm256_max_ps(_mm256_setzero_ps(), _mm256_min_ps(v, _mm256_set1_ps(1.0f)));
    }

    inline __m256 Lerp8(__m256 a, __m256 b, __m256 t) {
        return _mm256_fmadd_ps(t, _mm256_sub_ps(b, a), a);
    }

    struct VertexBatchSoA {
        float* x;
        float* y;
        float* z;
        float* nx;
        float* ny;
        float* nz;
        uint32_t count;
    };

    inline void TransformVerticesAVX2(const VertexBatchSoA& in, VertexBatchSoA& out,
                                      const Mat4& M) {
        __m256 r0x=_mm256_set1_ps(M.m[0][0]), r0y=_mm256_set1_ps(M.m[0][1]),
               r0z=_mm256_set1_ps(M.m[0][2]), r0w=_mm256_set1_ps(M.m[0][3]);
        __m256 r1x=_mm256_set1_ps(M.m[1][0]), r1y=_mm256_set1_ps(M.m[1][1]),
               r1z=_mm256_set1_ps(M.m[1][2]), r1w=_mm256_set1_ps(M.m[1][3]);
        __m256 r2x=_mm256_set1_ps(M.m[2][0]), r2y=_mm256_set1_ps(M.m[2][1]),
               r2z=_mm256_set1_ps(M.m[2][2]), r2w=_mm256_set1_ps(M.m[2][3]);

        uint32_t i = 0;
        for (; i + 7 < in.count; i += 8) {
            __m256 vx = _mm256_load_ps(&in.x[i]);
            __m256 vy = _mm256_load_ps(&in.y[i]);
            __m256 vz = _mm256_load_ps(&in.z[i]);
            _mm256_store_ps(&out.x[i], _mm256_fmadd_ps(r0x,vx,_mm256_fmadd_ps(r0y,vy,_mm256_fmadd_ps(r0z,vz,r0w))));
            _mm256_store_ps(&out.y[i], _mm256_fmadd_ps(r1x,vx,_mm256_fmadd_ps(r1y,vy,_mm256_fmadd_ps(r1z,vz,r1w))));
            _mm256_store_ps(&out.z[i], _mm256_fmadd_ps(r2x,vx,_mm256_fmadd_ps(r2y,vy,_mm256_fmadd_ps(r2z,vz,r2w))));
        }
        for (; i < in.count; ++i) {
            float vx=in.x[i], vy=in.y[i], vz=in.z[i];
            out.x[i] = M.m[0][0]*vx+M.m[0][1]*vy+M.m[0][2]*vz+M.m[0][3];
            out.y[i] = M.m[1][0]*vx+M.m[1][1]*vy+M.m[1][2]*vz+M.m[1][3];
            out.z[i] = M.m[2][0]*vx+M.m[2][1]*vy+M.m[2][2]*vz+M.m[2][3];
        }
    }

    inline void TransformNormalsAVX2(const VertexBatchSoA& in, VertexBatchSoA& out,
                                     const std::array<float,9>& it) {
        __m256 n00=_mm256_set1_ps(it[0]), n01=_mm256_set1_ps(it[1]), n02=_mm256_set1_ps(it[2]);
        __m256 n10=_mm256_set1_ps(it[3]), n11=_mm256_set1_ps(it[4]), n12=_mm256_set1_ps(it[5]);
        __m256 n20=_mm256_set1_ps(it[6]), n21=_mm256_set1_ps(it[7]), n22=_mm256_set1_ps(it[8]);

        uint32_t i = 0;
        for (; i + 7 < in.count; i += 8) {
            __m256 nx = _mm256_load_ps(&in.nx[i]);
            __m256 ny = _mm256_load_ps(&in.ny[i]);
            __m256 nz = _mm256_load_ps(&in.nz[i]);

            __m256 ox = _mm256_fmadd_ps(n00,nx,_mm256_fmadd_ps(n01,ny,_mm256_mul_ps(n02,nz)));
            __m256 oy = _mm256_fmadd_ps(n10,nx,_mm256_fmadd_ps(n11,ny,_mm256_mul_ps(n12,nz)));
            __m256 oz = _mm256_fmadd_ps(n20,nx,_mm256_fmadd_ps(n21,ny,_mm256_mul_ps(n22,nz)));

            __m256 dot = _mm256_fmadd_ps(ox,ox,_mm256_fmadd_ps(oy,oy,_mm256_mul_ps(oz,oz)));
            __m256 rlen = _mm256_rsqrt_ps(dot);

            __m256 half = _mm256_set1_ps(0.5f), c3   = _mm256_set1_ps(1.5f);
            rlen = _mm256_mul_ps(rlen, _mm256_sub_ps(c3, _mm256_mul_ps(half, _mm256_mul_ps(dot, _mm256_mul_ps(rlen, rlen)))));

            _mm256_store_ps(&out.nx[i], _mm256_mul_ps(ox, rlen));
            _mm256_store_ps(&out.ny[i], _mm256_mul_ps(oy, rlen));
            _mm256_store_ps(&out.nz[i], _mm256_mul_ps(oz, rlen));
        }

        for (; i < in.count; ++i) {
            float nx=in.nx[i], ny=in.ny[i], nz=in.nz[i];
            float ox = it[0]*nx+it[1]*ny+it[2]*nz;
            float oy = it[3]*nx+it[4]*ny+it[5]*nz;
            float oz = it[6]*nx+it[7]*ny+it[8]*nz;
            float len = std::sqrt(ox*ox+oy*oy+oz*oz);
            float inv = (len > 1e-6f) ? 1.0f/len : 0.0f;
            out.nx[i]=ox*inv; out.ny[i]=oy*inv; out.nz[i]=oz*inv;
        }
    }

}
}
