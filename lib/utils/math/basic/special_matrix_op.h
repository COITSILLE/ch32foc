#pragma once

#include "matrix_def.h"
#include "vector.h"

#ifdef __cplusplus

/* ========== 2D 向量运算 ========== */

/** @brief 2D 叉积标量：a × b = a.x*b.y - a.y*b.x */
inline float cross2D(Vec2_t a, Vec2_t b)
{
    return a.x() * b.y() - a.y() * b.x();
}

/** @brief 2D 叉积标量（分量形式） */
inline float cross2D(float ax, float ay, float bx, float by)
{
    return ax * by - ay * bx;
}

/* ========== 2×2 ========== */

/**
 * @brief 2×2 矩阵行列式  det = a·d - b·c
 */
inline float matrix22_det(const matrix_t<2, 2>& A)
{
    return A[0] * A[3] - A[1] * A[2];
}

/**
 * @brief Cramer 法则求解 2×2 线性方程组 A * x = b
 */
inline bool matrix22_solve(const matrix_t<2, 2>& A,
                           const matrix_t<2, 1>& b,
                           matrix_t<2, 1>& x)
{
    float det = matrix22_det(A);
    if (det == 0.0f) return false;
    x[0] = (b[0] * A[3] - b[1] * A[1]) / det;
    x[1] = (A[0] * b[1] - A[2] * b[0]) / det;
    return true;
}

/* ========== 3×3 ========== */

/**
 * @brief 3×3 矩阵行列式
 *   det = a(ei - fh) - b(di - fg) + c(dh - eg)
 */
inline float matrix33_det(const matrix_t<3, 3>& A)
{
    float a = A[0], b = A[1], c = A[2];
    float d = A[3], e = A[4], f = A[5];
    float g = A[6], h = A[7], i = A[8];
    return a * (e * i - f * h)
         - b * (d * i - f * g)
         + c * (d * h - e * g);
}

/**
 * @brief Cramer 法则求解 3×3 线性方程组 A * x = b
 */
inline bool matrix33_solve(const matrix_t<3, 3>& A,
                           const matrix_t<3, 1>& b,
                           matrix_t<3, 1>& x)
{
    float detA = matrix33_det(A);
    if (detA == 0.0f) return false;

    matrix_t<3, 3> A0 = A, A1 = A, A2 = A;
    A0[0] = b[0]; A0[3] = b[1]; A0[6] = b[2];
    A1[1] = b[0]; A1[4] = b[1]; A1[7] = b[2];
    A2[2] = b[0]; A2[5] = b[1]; A2[8] = b[2];

    x[0] = matrix33_det(A0) / detA;
    x[1] = matrix33_det(A1) / detA;
    x[2] = matrix33_det(A2) / detA;
    return true;
}

#endif /* __cplusplus */
