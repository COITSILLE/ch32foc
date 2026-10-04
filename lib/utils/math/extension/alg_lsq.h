#pragma once

#include "matrix_def.h"
#include "matrix_op.h"
#include "smath.h"       // s_sqrt

#ifdef __cplusplus

/**
 * @brief 通用线性最小二乘求解器
 *
 * 对模型 y = θ₀·f₀(x) + θ₁·f₁(x) + ... + θ_{M-1}·f_{M-1}(x)
 * 收集 N 个样本 (f₀..f_{M-1}, y)，求解最小化 ||Fθ - y||² 的 θ。
 *
 * 内部维护法方程 (F^T F) θ = F^T y，其中 F 为 N×M 设计矩阵。
 *
 * @tparam M  模型参数数量 (特征数)
 *
 * 用法示例 — 拟合平面 z = a·x + b·y + c:
 * @code
 *   LsqSolver<3> solver;
 *   for (每个数据点) {
 *       matrix_t<3, 1> f = {{ x, y, 1.0f }};   // 特征向量
 *       solver.addSample(f, z);                   // 目标值
 *   }
 *   matrix_t<3, 1> theta;
 *   if (solver.solve(theta)) {
 *       float a = theta[0], b = theta[1], c = theta[2];
 *   }
 * @endcode
 */
template<size_t M>
class LsqSolver
{
public:
    LsqSolver() { reset(); }

    /**
     * @brief 重置所有累积状态
     */
    void reset()
    {
        for (size_t i = 0; i < M * M; ++i) FTF_[i] = 0.0f;
        for (size_t i = 0; i < M; ++i)     FTy_[i] = 0.0f;
        sum_yy_ = 0.0f;
        N_ = 0;
    }

    /**
     * @brief 添加一个样本
     *
     * @param features  特征向量 f = [f₀, f₁, ..., f_{M-1}]^T (M×1)
     * @param target    目标值 y (标量)
     *
     * 内部更新:
     *   F^T F += f · f^T    (外积, M×M)
     *   F^T y += f · y      (M×1)
     *   sum_yy += y²         (标量, 用于残差计算)
     *   N++
     */
    void addSample(const matrix_t<M, 1>& features, float target)
    {
        /* F^T F += features * features^T */
        for (size_t r = 0; r < M; ++r) {
            for (size_t c = 0; c < M; ++c) {
                FTF_.at(r, c) += features[r] * features[c];
            }
        }

        /* F^T y += features * target */
        for (size_t r = 0; r < M; ++r) {
            FTy_[r] += features[r] * target;
        }

        sum_yy_ += target * target;
        N_++;
    }

    /**
     * @brief 求解法方程 (F^T F) θ = F^T y
     *
     * 使用列主元高斯消元法，适用于任意 M。
     *
     * @param theta  [out] 最优参数向量 θ (M×1)
     * @return true  求解成功
     * @return false 法方程奇异 (样本不足或特征线性相关)
     */
    bool solve(matrix_t<M, 1>& theta) const
    {
        if (N_ < (int)M)
            return false;  // 欠定

        matrix_t<M, M> A = FTF_;
        matrix_t<M, 1> b = FTy_;
        return matrix_solve<M>(A, b, theta);
    }

    /** @brief 当前样本数 */
    int count() const { return N_; }

    /** @brief 法方程矩阵 F^T F (只读) */
    const matrix_t<M, M>& FTF() const { return FTF_; }

    /** @brief 法方程右侧 F^T y (只读) */
    const matrix_t<M, 1>& FTy() const { return FTy_; }

    /* ============================================================
     *  残差 / 评估
     * ============================================================ */

    /**
     * @brief 计算单个样本的残差  r = y - f^T·θ
     *
     * 利用已拟合参数 θ 评估一个样本的拟合误差（无需重新遍历全部数据）。
     *
     * @param features  特征向量 (M×1)
     * @param target    目标值 y
     * @param theta     已拟合的参数 θ (M×1)
     * @return 残差 r = y - f^T·θ
     */
    static float computeResidual(const matrix_t<M, 1>& features,
                                  float target,
                                  const matrix_t<M, 1>& theta)
    {
        float y_fit = 0.0f;
        for (size_t i = 0; i < M; ++i)
            y_fit += features[i] * theta[i];
        return target - y_fit;
    }

    /**
     * @brief 计算均方根误差 RMS = sqrt(SSE / N)
     *
     * 利用法方程累积量 SSE = sum_yy - 2·θ^T·(F^T y) + θ^T·(F^T F)·θ
     * 无需重新遍历全部样本，时间复杂度 O(M²)。
     *
     * @param theta  已拟合的参数 θ (M×1)
     * @return RMS 误差，或 -1.0f (样本不足时)
     */
    float computeRmsError(const matrix_t<M, 1>& theta) const
    {
        if (N_ < (int)M)
            return -1.0f;

        /* SSE = sum_yy - 2·θ^T·(F^T y) + θ^T·(F^T F)·θ */
        float sse = sum_yy_;

        /* -2·θ^T·(F^T y) */
        float fty_dot = 0.0f;
        for (size_t i = 0; i < M; ++i)
            fty_dot += theta[i] * FTy_[i];
        sse -= 2.0f * fty_dot;

        /* + θ^T·(F^T F)·θ */
        for (size_t r = 0; r < M; ++r) {
            float row_sum = 0.0f;
            for (size_t c = 0; c < M; ++c)
                row_sum += FTF_.at(r, c) * theta[c];
            sse += theta[r] * row_sum;
        }

        if (sse < 0.0f)
            sse = 0.0f;  // 数值舍入可能略低于 0

        return s_sqrt(sse / (float)N_);
    }

private:
    matrix_t<M, M> FTF_;    ///< F^T F (法方程矩阵)
    matrix_t<M, 1> FTy_;    ///< F^T y (法方程右侧)
    float sum_yy_;           ///< Σ y_i² (目标值平方和)
    int N_;                  ///< 样本总数
};

#endif /* __cplusplus */
