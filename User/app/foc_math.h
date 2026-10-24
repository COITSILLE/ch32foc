#pragma once

#include "iqmath.h"

#ifdef __cplusplus

/** @brief Park transform: alpha/beta to d/q, given cos/sin of the electrical angle. */
static inline void park(q24_t i_alpha, q24_t i_beta, q24_t c, q24_t s, q24_t *d, q24_t *q) {
    // *d = i_alpha * c + i_beta * s;
    // *q = i_beta * c - i_alpha * s;
    *d = q24_add_w(q24_mul_w(i_alpha, c), q24_mul_w(i_beta, s));
    *q = q24_add_w(q24_mul_w(i_beta, c), q24_mul_w(-i_alpha, s));
}

/** @brief Clarke transform: phase U/V currents to alpha/beta (amplitude invariant). */
static inline void clarke(q24_t i_u, q24_t i_v, q24_t *i_alpha, q24_t *i_beta) {
    constexpr q24_t sqrt3_div3 = q24(0.5773502691896258f);  // 1/√3 = √3/3
    *i_alpha = i_u;
    *i_beta  = q24_mul_w(q24_add_w(i_u, q24_add_w(i_v, i_v)), sqrt3_div3);
}

/** @brief Inverse Park transform: d/q voltages to alpha/beta. */
static inline void inv_park(q24_t d, q24_t q, q24_t c, q24_t s, q24_t *i_alpha, q24_t *i_beta) {
    // *i_alpha = d * c - q * s;
    // *i_beta  = q * c + d * s;
    *i_alpha = q24_sub_w(q24_mul_w(d, c), q24_mul_w(q, s));
    *i_beta  = q24_add_w(q24_mul_w(q, c), q24_mul_w(d, s));
}
/** @brief Inverse Clarke transform: alpha/beta voltages to the three phase voltages. */
static inline void inv_clarke(q24_t i_alpha, q24_t i_beta, q24_t *i_u, q24_t *i_v, q24_t *i_w) {
    constexpr q24_t sqrt3_div2 = q24(0.8660254037844386f);  // sqrt(3)/2
    *i_u =  i_alpha;
    // *i_v =  -Q24_HALF * i_alpha + sqrt3_div2 * i_beta;
    // *i_w =  -Q24_HALF * i_alpha - sqrt3_div2 * i_beta;
    q24_t neg_half_i_alpha = -q24_mul_w(Q24_HALF, i_alpha);
    q24_t sqrt3_div2_i_beta = q24_mul_w(sqrt3_div2, i_beta);
    *i_v = q24_add_w(neg_half_i_alpha, sqrt3_div2_i_beta);
    *i_w = q24_sub_w(neg_half_i_alpha, sqrt3_div2_i_beta);
}

/** @brief q7.24 PI controller with conditional integration and optional feed-forward. */
class FocPiController {
public:
    FocPiController(void) = default;
    /** @brief Run one step from an explicit error input. */
    inline q24_t getOutputErr(q24_t err) {
        q24_t p_term = m_kp * err;

        q24_t out_raw = q24_add_w(p_term, m_integ);
        q24_t out_ff = q24_mul_w(m_k_ff, m_sp);
        out_raw = q24_add_w(out_raw, out_ff);

        if (out_raw > m_out_lim) {
            out_raw = m_out_lim;
            if (m_ki_dt != 0) m_integ = q24_sub_w(q24_sub_w(m_out_lim, p_term), out_ff);
        } else if (out_raw < -m_out_lim) {
            out_raw = -m_out_lim;
            if (m_ki_dt != 0) m_integ = q24_sub_w(q24_sub_w(-m_out_lim, p_term), out_ff);
        } else {
            m_integ = q24_add_w(m_integ, q24_mul_w(err, m_ki_dt));
        }

        return out_raw;
    }
    /** @brief Run one step from the reference and a feedback value. */
    inline q24_t getOutput(q24_t fb) {
        q24_t err = q24_sub_w(m_sp, fb);
        return getOutputErr(err);
    }
    /** @brief Saturate a value to the configured output limit. */
    void clamp(q24_t *out) {
        if (*out > m_out_lim) *out = m_out_lim;
        else if (*out < -m_out_lim) *out = -m_out_lim;
    }
    /** @brief Clear the integrator. */
    void reset() {
        m_integ = Q24_ZERO;
    }
    /** @brief Current reference. */
    q24_t getTarget() const { return m_sp; }
    /** @brief Set the reference. */
    void setTarget(q24_t sp) { m_sp = sp; }
    /** @brief Proportional gain. */
    q24_t getKp() const { return m_kp; }
    /** @brief Set the proportional gain. */
    void setKp(q24_t kp) { m_kp = kp; }
    /** @brief Integral gain already multiplied by dt. */
    q24_t getKiDt() const { return m_ki_dt; }
    /** @brief Set the integral gain already multiplied by dt. */
    void setKiDt(q24_t ki_dt) { m_ki_dt = ki_dt; }
    /** @brief Set the reference feed-forward gain. */
    void setFeedforward(q24_t k_ff) { m_k_ff = k_ff; }
    /** @brief Set the symmetric output limit. */
    void setOutputLimit(q24_t limit) { m_out_lim = limit; }
private:
    q24_t m_sp = Q24_ZERO;
    q24_t m_integ = Q24_ZERO;
    q24_t m_kp = Q24_ZERO;
    q24_t m_ki_dt = Q24_ZERO;
    q24_t m_k_ff = Q24_ZERO;
    q24_t m_out_lim = Q24_ZERO;

    
};

#endif