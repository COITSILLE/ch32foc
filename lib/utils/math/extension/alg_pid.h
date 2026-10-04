#pragma once

#include "systick_time.h"
#include <stdint.h>
#include "smath.h"

#ifdef __cplusplus
/* timestamp_t: uint64_t, 单位为微秒, 由 Systick_getUsTick() 提供 */
typedef uint64_t timestamp_t;

/** @brief PID 微分模式 */
enum class DiffMode : uint8_t {
    Standard   = 0,  /**< 标准 PID: kd × d(error)/dt */
    IncompDiff,      /**< 不完全微分: kd 经低通滤波，抑制高频噪声 */
    DiffOnFb,        /**< 微分先行: kd 取自反馈微分，目标跳变无冲击 */
    DisableDiff,    /**< 禁用微分: kd=0，适合纯积分控制 */
};

template<typename T>
class PID{
private:
    timestamp_t last_time;
    T last_err;
    T last_err_i;
    T last_sp;
    T last_err_d;           /**< 不完全微分状态量 */
    T clampOutput(T res);
    T clampIntegral(T err_i);
    T last_output_;

    T kp;
    T ki;
    T kd;
    T max_output;
    T min_output;
    T ki_limit;
    T sp;
    T kv_ff_;               /**< 速度前馈系数 (V/(rad/s)) */
    T ka_ff_;               /**< 加速度前馈系数 (V/(rad/s²)) */
    DiffMode mode = DiffMode::IncompDiff;  /**< 微分模式 */
    T dt_;
    T diff_tau = T(0.01f);  /**< 不完全微分时间常数 (s)，仅 IncompDiff 模式生效 */
    T integral_sep_threshold = T(0.0f); /**< 积分分离阈值；|err|>此值时冻结积分，≤0 禁用 */

public:
    PID();
    void setFactors(T kp, T ki = T(0.0f), T kd = T(0.0f), T kfv = T(0.0f), T kfa = T(0.0f));
    void setLimit(T output_limit, T ki_limit);
    void setLimit(T max_output, T min_output, T ki_limit);
    void setDt(T dt);
    void setFeedforward(T kv, T ka = T(0.0f));
    void setTarget(T sp);
    void setDiffMode(DiffMode mode);
    void setDiffTau(T tau);
    void setIntegralSepThreshold(T threshold);
    /** @brief 执行一步 PID 计算，mode 成员决定微分算法 */
    T getOutput(T fb);
    /** @brief 微分先行 + 外部传入反馈微分值（仅 mode==DiffOnFb 时有效） */
    T getOutput(T fb, T external_fb_d);
    T getLastOutput() const { return last_output_; }
    T getTarget() const { return sp; }
    void reset();
};


#endif