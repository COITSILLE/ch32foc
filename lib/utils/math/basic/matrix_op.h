#pragma once

#include "matrix_def.h"
#include "special_matrix_op.h"

#ifdef __cplusplus

/*===================== 函数式 API ========================*/

/**
 * @brief C = A + B
 */
template<size_t R, size_t C>
matrix_t<R, C> matrix_add(const matrix_t<R, C>& a, const matrix_t<R, C>& b) {
    matrix_t<R, C> result;
    for (size_t r = 0; r < R; ++r) {
        for (size_t c = 0; c < C; ++c) {
            result.at(r, c) = a.at(r, c) + b.at(r, c);
        }
    }
    return result;
}

/**
 * @brief 元素减法: C = A - B
 */
template<size_t R, size_t C>
matrix_t<R, C> matrix_sub(const matrix_t<R, C>& a, const matrix_t<R, C>& b) {
    matrix_t<R, C> result;
    for (size_t r = 0; r < R; ++r) {
        for (size_t c = 0; c < C; ++c) {
            result.at(r, c) = a.at(r, c) - b.at(r, c);
        }
    }
    return result;
}

/**
 * @brief 矩阵乘法: C(m×p) = A(m×n) * B(n×p)
 * @note 要求 A.col == B.row (即 CA == RB)。
 */
template<size_t RA, size_t CA, size_t RB, size_t CB>
matrix_t<RA, CB> matrix_mul(const matrix_t<RA, CA>& a, const matrix_t<RB, CB>& b) {
    matrix_t<RA, CB> result;
    for (size_t r = 0; r < RA; ++r) {
        for (size_t c = 0; c < CB; ++c) {
            float sum = 0.0f;
            for (size_t k = 0; k < CA; ++k) {
                sum += a.at(r, k) * b.at(k, c);
            }
            result.at(r, c) = sum;
        }
    }
    return result;
}

/**
 * @brief 矩阵转置: B(c,r) = A(r,c)
 *
 * @tparam R  源矩阵行数
 * @tparam C  源矩阵列数
 * @param a   输入矩阵 (R×C)
 * @return 转置矩阵 (C×R)
 */
template<size_t R, size_t C>
matrix_t<C, R> matrix_transpose(const matrix_t<R, C>& a) {
    matrix_t<C, R> result;
    for (size_t r = 0; r < R; ++r) {
        for (size_t c = 0; c < C; ++c) {
            result.at(c, r) = a.at(r, c);
        }
    }
    return result;
}

/**
 * @brief 方阵的迹 (trace): 对角线元素之和
 *
 *   tr(A) = Σᵢ A[i][i]
 *
 * @tparam N  方阵维度
 * @param a   N×N 方阵
 * @return 迹的值
 */
template<size_t N>
float matrix_trace(const matrix_t<N, N>& a) {
    float sum = 0.0f;
    for (size_t i = 0; i < N; ++i) {
        sum += a.at(i, i);
    }
    return sum;
}

/**
 * @brief 列主元高斯消元法求解 N×N 线性方程组 A * x = b
 *
 * 时间复杂度 O(N³)。适用于任意 N 的方阵。
 * 对 N=2,3 建议使用 matrix_fsolve (special_matrix_op.h) 以获得更高性能。
 *
 * @tparam N  方阵维度
 * @param A   N×N 系数矩阵 (会被修改为行阶梯形)
 * @param b   N×1 列向量 (会被同步消元)
 * @param x   [out] 解向量
 * @return true  求解成功
 * @return false 矩阵奇异
 */
template<size_t N>
bool matrix_solve(matrix_t<N, N>& A, matrix_t<N, 1>& b, matrix_t<N, 1>& x)
{
    const float EPS = 1e-12f;

    for (size_t col = 0; col < N; ++col) {
        size_t pivot = col;
        float max_val = A.at(col, col) >= 0 ? A.at(col, col) : -A.at(col, col);
        for (size_t row = col + 1; row < N; ++row) {
            float val = A.at(row, col) >= 0 ? A.at(row, col) : -A.at(row, col);
            if (val > max_val) { max_val = val; pivot = row; }
        }
        if (max_val < EPS) return false;

        if (pivot != col) {
            for (size_t c = col; c < N; ++c) {
                float tmp = A.at(col, c);
                A.at(col, c) = A.at(pivot, c);
                A.at(pivot, c) = tmp;
            }
            float tmp = b[col]; b[col] = b[pivot]; b[pivot] = tmp;
        }

        float piv = A.at(col, col);
        for (size_t row = col + 1; row < N; ++row) {
            float factor = A.at(row, col) / piv;
            for (size_t c = col; c < N; ++c)
                A.at(row, c) -= factor * A.at(col, c);
            b[row] -= factor * b[col];
        }
    }

    for (int row = (int)N - 1; row >= 0; --row) {
        float sum = b[row];
        for (size_t c = (size_t)row + 1; c < N; ++c)
            sum -= A.at((size_t)row, c) * x[c];
        x[row] = sum / A.at((size_t)row, (size_t)row);
    }
    return true;
}

/*======================= 运算符重载 ========================*/

template<size_t R, size_t C>
matrix_t<R, C> operator+(const matrix_t<R, C>& a, const matrix_t<R, C>& b) {
    return matrix_add(a, b);
}

template<size_t R, size_t C>
matrix_t<R, C> operator-(const matrix_t<R, C>& a, const matrix_t<R, C>& b) {
    return matrix_sub(a, b);
}

template<size_t R, size_t C>
matrix_t<R, C>& operator+=(matrix_t<R, C>& a, const matrix_t<R, C>& b) {
    for (size_t i = 0; i < R * C; ++i) a.data[i] += b.data[i];
    return a;
}

template<size_t R, size_t C>
matrix_t<R, C>& operator-=(matrix_t<R, C>& a, const matrix_t<R, C>& b) {
    for (size_t i = 0; i < R * C; ++i) a.data[i] -= b.data[i];
    return a;
}

template<size_t RA, size_t CA, size_t RB, size_t CB>
matrix_t<RA, CB> operator*(const matrix_t<RA, CA>& a, const matrix_t<RB, CB>& b) {
    return matrix_mul(a, b);
}

/*=================== 标量运算 =====================*/

template<size_t R, size_t C>
matrix_t<R, C> operator*(const matrix_t<R, C>& a, float s) {
    matrix_t<R, C> r;
    for (size_t i = 0; i < R * C; ++i) r.data[i] = a.data[i] * s;
    return r;
}

template<size_t R, size_t C>
matrix_t<R, C> operator*(float s, const matrix_t<R, C>& a) {
    return a * s;
}

template<size_t R, size_t C>
matrix_t<R, C> operator/(const matrix_t<R, C>& a, float s) {
    float inv = 1.0f / s;
    return a * inv;
}

template<size_t R, size_t C>
matrix_t<R, C>& operator*=(matrix_t<R, C>& a, float s) {
    for (size_t i = 0; i < R * C; ++i) a.data[i] *= s;
    return a;
}

template<size_t R, size_t C>
matrix_t<R, C>& operator/=(matrix_t<R, C>& a, float s) {
    float inv = 1.0f / s;
    for (size_t i = 0; i < R * C; ++i) a.data[i] *= inv;
    return a;
}

template<size_t R, size_t C>
matrix_t<R, C> operator-(const matrix_t<R, C>& a) {
    matrix_t<R, C> r;
    for (size_t i = 0; i < R * C; ++i) r.data[i] = -a.data[i];
    return r;
}

/*===================== 统一求解器 ========================*/

/**
 * @brief 自动分派的线性方程组求解器
 *
 *  N=2 → matrix_fsolve (Cramer, 最快)
 *  N=3 → matrix_fsolve (Cramer, 最快)
 *  N≥4 → matrix_solve_gauss (通用高斯消元)
 *
 * @tparam N  方阵维度
 */
template<size_t N>
inline bool matrix_fsolve(const matrix_t<N, N>& A,
                         const matrix_t<N, 1>& b,
                         matrix_t<N, 1>& x)
{
    if constexpr (N == 2) {
        return matrix22_solve(
            reinterpret_cast<const matrix_t<2, 2>&>(A),
            reinterpret_cast<const matrix_t<2, 1>&>(b),
            reinterpret_cast<matrix_t<2, 1>&>(x));
    } else if constexpr (N == 3) {
        return matrix33_solve(
            reinterpret_cast<const matrix_t<3, 3>&>(A),
            reinterpret_cast<const matrix_t<3, 1>&>(b),
            reinterpret_cast<matrix_t<3, 1>&>(x));
    } else {
        matrix_t<N, N> A_cp = A;
        matrix_t<N, 1> b_cp = b;
        return matrix_solve(A_cp, b_cp, x);
    }
}

#endif