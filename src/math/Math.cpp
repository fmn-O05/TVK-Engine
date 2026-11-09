#include "tvk/math/Math.h"
#include <cmath>
#include <algorithm>

namespace tvk {

    Mat4 Mat4Identity() {
        Mat4 mat = {0};
        mat.m[0][0] = 1.0f; mat.m[1][1] = 1.0f;
        mat.m[2][2] = 1.0f; mat.m[3][3] = 1.0f;
        return mat;
    }

    Mat4 Mat4Multiply(const Mat4& a, const Mat4& b) {
        Mat4 mat = {0};
        for (int r = 0; r < 4; ++r) {
            for (int k = 0; k < 4; ++k) {
                float a_val = a.m[r][k];
                for (int c = 0; c < 4; ++c) {
                    mat.m[r][c] += a_val * b.m[k][c];
                }
            }
        }
        return mat;
    }

    Mat4 Mat4Translate(Vec3 v) {
        Mat4 mat = Mat4Identity();
        mat.m[0][3] = v.x;
        mat.m[1][3] = v.y;
        mat.m[2][3] = v.z;
        return mat;
    }

    Mat4 Mat4Scale(Vec3 v) {
        Mat4 mat = {0};
        mat.m[0][0] = v.x;
        mat.m[1][1] = v.y;
        mat.m[2][2] = v.z;
        mat.m[3][3] = 1.0f;
        return mat;
    }

}
