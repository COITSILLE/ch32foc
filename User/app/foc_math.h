#pragma once

#include "iqmath.h"
#include "sys_interface.h"

#ifdef __cplusplus

static inline void park(q24_t i_alpha, q24_t i_beta, q24_t c, q24_t s, q24_t *d, q24_t *q){
    // *d = i_alpha * c + i_beta * s;
    // *q = i_beta * c - i_alpha * s;
    *d = q24_add_w(q24_mul_w(i_alpha, c), q24_mul_w(i_beta, s));
    *q = q24_add_w(q24_mul_w(i_beta, c), q24_mul_w(-i_alpha, s));
}

static inline void clarke(q24_t i_u, q24_t i_v, q24_t *i_alpha, q24_t *i_beta){
    constexpr q24_t sqrt3_div3 = q24(0.5773502691896258f); /* 1/√3 = √3/3 */
    *i_alpha = i_u;
    *i_beta  = q24_mul_w(q24_add_w(i_u, q24_add_w(i_v, i_v)), sqrt3_div3);
}

static inline void inv_park(q24_t d, q24_t q, q24_t c, q24_t s, q24_t *i_alpha, q24_t *i_beta){
    // *i_alpha = d * c - q * s;
    // *i_beta  = q * c + d * s;
    *i_alpha = q24_sub_w(q24_mul_w(d, c), q24_mul_w(q, s));
    *i_beta  = q24_add_w(q24_mul_w(q, c), q24_mul_w(d, s));
}
static inline void inv_clarke(q24_t i_alpha, q24_t i_beta, q24_t *i_u, q24_t *i_v, q24_t *i_w){
    constexpr q24_t sqrt3_div2 = q24(0.8660254037844386f); /* sqrt(3)/2 */
    *i_u =  i_alpha;
    // *i_v =  -Q24_HALF * i_alpha + sqrt3_div2 * i_beta;
    // *i_w =  -Q24_HALF * i_alpha - sqrt3_div2 * i_beta;
    q24_t neg_half_i_alpha = -q24_mul_w(Q24_HALF, i_alpha);
    q24_t sqrt3_div2_i_beta = q24_mul_w(sqrt3_div2, i_beta);
    *i_v = q24_add_w(neg_half_i_alpha, sqrt3_div2_i_beta);
    *i_w = q24_sub_w(neg_half_i_alpha, sqrt3_div2_i_beta);
}

class foc_pi_controller{
public:
    foc_pi_controller(void) = default;
    inline q24_t getOutputErr(q24_t err){
        q24_t p_term = kp_ * err;

        q24_t out_raw = q24_add_w(p_term, integ_);
        q24_t out_ff = q24_mul_w(k_ff_, sp_);
        out_raw = q24_add_w(out_raw, out_ff);

        if (out_raw > out_lim_) {
            out_raw = out_lim_;
            integ_ = q24_sub_w(q24_sub_w(out_lim_, p_term), out_ff);
        } else if (out_raw < -out_lim_) {
            out_raw = -out_lim_;
            integ_ = q24_sub_w(q24_sub_w(-out_lim_, p_term), out_ff);
        } else {
            integ_ = q24_add_w(integ_, q24_mul_w(err, ki_dt_));
        }

        return out_raw;
    }
    inline q24_t getOutput(q24_t fb){
        q24_t err = q24_sub_w(sp_, fb);
        return getOutputErr(err);
    }
    void clamp(q24_t *out){
        if (*out > out_lim_) *out = out_lim_;
        else if (*out < -out_lim_) *out = -out_lim_;
    }
    void reset(){
        integ_ = Q24_ZERO;
    }
    q24_t getTarget() const { return sp_; }
    void setTarget(q24_t sp) { sp_ = sp; }
    q24_t getKp() const { return kp_; }
    void setKp(q24_t kp) { kp_ = kp; }
    q24_t getKiDt() const { return ki_dt_; }
    void setKiDt(q24_t ki_dt) { ki_dt_ = ki_dt; }
    void setFeedforward(q24_t k_ff) { k_ff_ = k_ff; }
    void setOutputLimit(q24_t limit) { out_lim_ = limit; }
private:
    q24_t sp_ = Q24_ZERO;
    q24_t integ_ = Q24_ZERO;
    q24_t kp_ = Q24_ZERO;
    q24_t ki_dt_ = Q24_ZERO;
    q24_t k_ff_ = Q24_ZERO;
    q24_t out_lim_ = Q24_ZERO;
};

#endif