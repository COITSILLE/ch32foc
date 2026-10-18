#include "iqmath.h"
#include "iqmath_lut.h"        /* LUTs: rec/slp (div) + sin/sqrt + poly coefficients */

/* This file implements the runtime functions; operator+ - *, satq24 / q24_mac and the
 * arithmetic operators live in iqmath.h as static inline constexpr. For division, the
 * compile-time constant path uses the slow 64-bit divide in the header; the runtime path
 * dispatches via operator/ to q24_fast_div (the LUT lives only in this TU). */
#ifdef QMATH_ENABLE_FAST_DIV
q24_t q24_fast_div(q24_t a, q24_t b){
    if (b.val == 0) {                       // div-by-zero -> saturate
        return (a.val >= 0) ? Q24_MAX : Q24_MIN;
    }
    uint32_t sign = ((uint32_t)a.val ^ (uint32_t)b.val) >> 31;
    // Unsigned negation: |INT32_MIN| = 2^31 fits uint32 (0x80000000). A bare -a.val on
    // INT32_MIN is signed-overflow UB (same trick as cvtq24float in iqmath.h).
    uint32_t A = (a.val >= 0) ? (uint32_t)a.val : (uint32_t)(0u - (uint32_t)a.val);
    uint32_t U = (b.val >= 0) ? (uint32_t)b.val : (uint32_t)(0u - (uint32_t)b.val);
    // Overflow saturation: |A / U| >= 128, i.e. A >> 7 >= U
    if ((A >> 7) >= U) {
        return sign ? Q24_MIN : Q24_MAX;
    }
    // Normalize divisor U -> m (m/2^31 in [1, 2), bit31 always set): m = U << clz(U).
    // For any U != 0 (guaranteed above) the leading 1 lands on bit31, so m >= 2^31 always,
    // hence no clamp is needed. 32-bit shifts only, avoids a __ashldi3 call.
    int n = __builtin_clz(U);
    uint32_t m = U << n;
    // Initial guess from LUT + first-order Taylor interp (Q1.30):
    //   idx = (m >> 24) - 128          (m/2^31 in [1, 2), high 8 bits -> 128-entry table)
    //   F   = m & 0x00FFFFFF           (position inside the bin)
    //   x0  = rec_lut[idx] - ((F>>8 * slp_lut[idx]) >> 8)
    // Slope table ~Q15.0 (shifted right by 8), paired with F>>8 so the 16x16 multiply
    // stays in 32 bits (single `mul`).
    uint32_t idx = (m >> 24) - 128;
    uint32_t F   = m & 0x00FFFFFFu;
    int32_t x = q24_rec_lut[idx] - (int32_t)(((uint32_t)(F >> 8) * q24_slp_lut[idx]) >> 8);
    // 1 Newton refinement (quadratic convergence): x <- x*(2 - m*x)
    int64_t prod  = (int64_t)m * x;            // Q0.31 x Q1.30 = Q1.61
    int32_t prod_q = (int32_t)(prod >> 31);    // -> Q1.30
    int32_t sub    = (int32_t)((2u << 30) - (uint32_t)prod_q);  // 2^31 - prod_q (always positive, fits int32)
    int64_t prod2  = (int64_t)x * sub;         // Q1.30 x Q1.30 = Q2.60
    x = (int32_t)(prod2 >> 30);                // -> Q1.30
    // Final quotient (Q7.24): x ~ 2^61/m, m = U*2^n, so (A*x)>>(61-n-24) = A*2^24/U.
    // Split into 32-bit hi/lo with variable shift to avoid a 64-bit right shift that
    // would pull in libgcc __lshrdi3.
    uint64_t raw_res = (uint64_t)A * (uint32_t)x;
    uint32_t hi = (uint32_t)(raw_res >> 32);
    uint32_t lo = (uint32_t)raw_res;
    uint32_t result_abs = 0;
    if (n <= 5) {
        result_abs = hi >> (5 - n);
    } else {
        result_abs = (hi << (n - 5)) | (lo >> (37 - n));
    }
    // Restore sign
    return sign ? -q24_t::from_raw(result_abs) : q24_t::from_raw(result_abs);
}
#else
q24_t q24_div(q24_t a, q24_t b){
    if(b.val == 0){     // div-by-zero -> saturate (same as FAST_DIV path)
        return q24_t::from_raw(a.val >= 0 ? INT32_MAX : INT32_MIN);
    }
    int64_t dividend = ((int64_t)a.val << 24); // Q24
    int64_t quotient = dividend / (int64_t)b.val;
    return satq24(quotient);    // 与运算符一致：恒饱和（需要不饱和时由调用方另行处理）
}
#endif



/**
 * @brief Fused multiply-accumulate: acc = round((acc*mul) >> 24) + add.
 * @note Shared by the trig Horner chains (q_tan/q_atan/q_asin); intentionally NOT
 *       saturating - the intermediates stay well within Q7.24 range.
 */
static inline constexpr int32_t q24_mac(int32_t acc, int32_t mul, int32_t add){
    return (int32_t)((((int64_t)acc * mul) + (1 << 23)) >> 24) + add;
}

q24_t q_pow(q24_t x, int y){
    // 0^0 = 1; 0^y = 0 (y > 0); 0^y (y < 0) -> +inf (saturates, matches div-by-zero)
    if (x.val == 0 && y == 0) return Q24_ONE;
    if (x.val == 0) return (y > 0) ? q24_t::from_raw(0) : q24_t::from_raw(INT32_MAX);
    // Negative exponent: x^-y = (1/x)^y. Store |y| in uint32_t to avoid -y overflow at y == INT_MIN.
    uint32_t e = (uint32_t)y;
    if (y < 0) {
        x = Q24_ONE / x;   // x^-y = 1/x^y
        e = 0u - e;        // |y|
    }
    // Binary exponentiation
    q24_t result = Q24_ONE;
    while (e > 0) {
        if (e & 1) result *= x;
        x *= x;
        e >>= 1;
    }
    return result;
}

q24_t q_abs(q24_t x){
    // |INT32_MIN| = 2^31 is outside Q7.24; -INT32_MIN would overflow (UB), so saturate
    // instead of wrapping.
    if (x >= Q24_ZERO) return x;
    return -x;
}

q24_t q_mod(q24_t x, q24_t y){
    if (y.val == 0) return Q24_ZERO;
    q24_t q = x / y;
    // Integer part of the quotient, truncated toward zero (bit ops, no soft divide):
    // floor is q>>24; for negatives with a fraction, +1 turns floor into trunc-toward-zero.
    int32_t sign = q.val >> 31;
    int32_t frac = q.val & 0xFFFFFF;
    int32_t qi = (q.val >> 24) + (sign & (frac != 0));
    return q24_t::from_raw((int32_t)((int64_t)x.val - (int64_t)y.val * qi));
}

q24_t q_inv_sqrt(q24_t x){
    if (x.val <= 0) return Q24_ZERO;      // 1/sqrt(<=0) undefined -> 0
    return Q24_ONE / q_sqrt(x);           // reuse q_sqrt (uses fast division internally)
}

q24_t q_sqrt(q24_t x){
    // sqrt(<=0) undefined -> 0 (matches q_inv_sqrt). Negatives must be rejected before the
    // clz normalization: (uint32_t)x.val of a negative has clz==0, then `a << (n-1)` would
    // be a negative-shift UB and the LUT index (m>>23)-128 would run off the table.
    if (x.val <= 0) return Q24_ZERO;
    uint32_t a = (uint32_t)x.val;
    // 1) clz normalization: m = a << (n-1) in [2^30, 2^31), idx=(m>>23)-128
    int n = __builtin_clz(a);
    uint32_t m = a << (n - 1);
    int k = (n - 1) >> 1;
    // 2) Initial guess from LUT: q_sqrt.val = sqrt(m) * 2^(12-k) (table stores sqrt(m) as int)
    int32_t s0 = q24_sqrt_lut[(m >> 23) - 128];
    int32_t y;
    if (k <= 12) y = (int32_t)((uint32_t)s0 << (12 - k));
    else         y = s0 >> (k - 12);
    if ((n & 1) == 0) {                  // n even: sqrt(m)/sqrt(2)
        y = (int32_t)(((int64_t)y * Q24_SQRT1_2.val) >> 24);
    }
    // 3) Newton iterations (Q24, reuse fast division): y = (y + x/y)/2; 2 iters (default)
    static const int iters = 2;
    for (int i = 0; i < iters; ++i) {
        q24_t t = x / q24_t{y};
        y = (int32_t)(((int64_t)y + t.val) >> 1);
    }
    return q24_t::from_raw(y);
}

/* sin of an angle already reduced to [0, pi/2] (Q24 rad): 0.05 rad LUT + linear interp.
 */
static inline int32_t q24_sin_quarter(q24_t x){
    int64_t p20 = (int64_t)x.val * 20;          // idx = x/0.05
    int idx_low = (int)(p20 >> 24);
    uint32_t t  = (uint32_t)(p20 & 0xFFFFFF);   // fractional part (Q24)
    int idx_high = idx_low + 1;
    if (idx_high >= 33) return (1 << 24);       // guard (x near pi/2, should not happen)
    int32_t delta = q24_sin_lut[idx_high] - q24_sin_lut[idx_low];
    return q24_sin_lut[idx_low] + (int32_t)(((int64_t)delta * t) >> 24);
}

q24_t q_sin(q24_t x){
    // 1) Reduce to [0, 2pi): one q_mod + single negative fix-up (no while loop)
    x = q_mod(x, Q24_TWOPI);
    if (x.val < 0) x = x + Q24_TWOPI;
    // 2) Quadrant reduction to [0, pi/2]
    int32_t sign = 0;
    if (x.val > Q24_PI.val)   { sign = 1; x = x - Q24_PI; }
    if (x.val > Q24_PI_2.val) { x = Q24_PI - x; }
    // 3) 0.05 rad step table + linear interpolation
    int32_t y = q24_sin_quarter(x);
    return q24_t::from_raw(sign ? -y : y);
}

q24_t q_cos(q24_t x){
    return q_sin(x + Q24_PI_2);
}

void q_sincos(q24_t x, q24_t* s, q24_t* c){
    // 1) Reduce to [0, 2pi) 
    // x = n*2pi + r
    constexpr q24_t Q24_INV_TWOPI = 1.0f / Q24_TWOPI;
    int32_t n = q24_mul_w(x, Q24_INV_TWOPI).val >> 24; // floor to get n = x/(2pi), int
    q24_t r = q24_sub_w(x, q24_t::from_raw(n * Q24_TWOPI.val));// r = x - n*2pi
    if (r < 0) r = q24_add_w(r, Q24_TWOPI);
    if (r > Q24_TWOPI) r = q24_sub_w(r, Q24_TWOPI);// guard
    x = r;

    // 2) Quadrant split: x = k*(pi/2) + u, k in {0..3}, u in [0, pi/2)
    int k;
    q24_t u;
    constexpr q24_t Q24_3PI_2 = Q24_TWOPI - Q24_PI_2;
    if      (x < Q24_PI_2)              { k = 0; u = x; }
    else if (x < Q24_PI)                { k = 1; u = x - Q24_PI_2; }
    else if (x < Q24_3PI_2)             { k = 2; u = x - Q24_PI; }
    else                                { k = 3; u = x - (Q24_TWOPI - Q24_PI_2); }
    // 3) Same forward LUT interp: sin(u), cos(u) = sin(pi/2 - u) (mirror angle in [0, pi/2])
    int32_t su = q24_sin_quarter(u);
    int32_t cu = q24_sin_quarter(Q24_PI_2 - u);
    // 4) Combine sign & sin/cos swap per quadrant
    int32_t sv, cv;
    switch (k) {
        case 0: sv =  su; cv =  cu; break;
        case 1: sv =  cu; cv = -su; break;
        case 2: sv = -su; cv = -cu; break;
        default:sv = -cu; cv =  su; break;
    }
    if (s) *s = q24_t::from_raw(sv);
    if (c) *c = q24_t::from_raw(cv);
}

q24_t q_tan(q24_t x){
    // Reduce to [0, pi)
    x = q_mod(x, Q24_PI);
    if (x.val < 0) x = x + Q24_PI;
    int32_t sign = 0;
    if (x.val > Q24_PI_2.val) { sign = 1; x = Q24_PI - x; }   // [0, pi/2]
    // tan(x) ~= x*P(x^2), P(t)=C0+t*(C1+t*(C2+t*(C3+t*C4))), t=x^2 in [0,(pi/4)^2].
    // Shared q24_mac accumulator: each multiply is rounded+shifted once and fed back in,
    // instead of a chain of operator* calls.
    if (x.val < Q24_PI_4.val) {
        int32_t x2    = q24_mac(x.val, x.val, 0);                      // x2 = x*x
        int32_t inner = q24_mac(Q24_TAN_C4.val, x2, Q24_TAN_C3.val);   // C4*x2 + C3
        inner         = q24_mac(x2, inner, Q24_TAN_C2.val);            // x2*inner + C2
        inner         = q24_mac(x2, inner, Q24_TAN_C1.val);            // x2*inner + C1
        int32_t res   = q24_mac(x2, inner, Q24_TAN_C0.val);            // x2*inner + C0
        res           = q24_mac(x.val, res, 0);                        // x*res
        return q24_t::from_raw(sign ? -res : res);
    } else {                                    // complementary angle m = pi/2 - x, take reciprocal
        q24_t m      = Q24_PI_2 - x;
        int32_t m2   = q24_mac(m.val, m.val, 0);
        int32_t inner = q24_mac(Q24_TAN_C4.val, m2, Q24_TAN_C3.val);
        inner        = q24_mac(m2, inner, Q24_TAN_C2.val);
        inner        = q24_mac(m2, inner, Q24_TAN_C1.val);
        int32_t den  = q24_mac(m2, inner, Q24_TAN_C0.val);             // m2*inner + C0
        den          = q24_mac(m.val, den, 0);                         // m*den
        q24_t res = Q24_ONE / q24_t::from_raw(den);
        return sign ? -res : res;
    }
}

q24_t q_atan(q24_t x){
    // atan(x) ~= x*S(u), u=1-|x| in [0,1], S(u)=C0+u*(C1+u*(C2+u*(C3+u*C4))).
    // |x|>1: atan(x)=pi/2-atan(1/x). Shared q24_mac accumulator: one round+shift per
    // multiply instead of a separate operator* per term.
    auto term_of = [](q24_t v){
        int32_t u = (Q24_ONE - q_abs(v)).val;             // u = 1-|v|
        int32_t s = Q24_ATAN_C4.val;                      // Horner in u
        s = q24_mac(u, s, Q24_ATAN_C3.val);
        s = q24_mac(u, s, Q24_ATAN_C2.val);
        s = q24_mac(u, s, Q24_ATAN_C1.val);
        s = q24_mac(u, s, Q24_ATAN_C0.val);
        return q24_mac(v.val, s, 0);                      // v*S(u)
    };
    if (x.val > Q24_ONE.val) {                             // atan(x) = pi/2 - atan(1/x)
        q24_t inv = Q24_ONE / x;
        return Q24_PI_2 - q24_t::from_raw(term_of(inv));
    } else if (x.val < -Q24_ONE.val) {
        q24_t inv = Q24_ONE / x;
        return q24_t::from_raw(-(Q24_PI_2.val + term_of(inv)));
    }
    return q24_t::from_raw(term_of(x));
}

q24_t q_asin(q24_t x){
    // Piecewise Remez approximation:
    //   |x| <= 0.5 : asin(x) = x*S(x^2), S(t)=C0+C1*t+C2*t^2+C3*t^3+C4*t^4
    //                (pure mul/add fast path - no sqrt, no division)
    //   |x| >  0.5 : asin(x) = pi/2 - 2*asin(u), u=sqrt((1-|x|)/2)
    //                (one Newton-iteration sqrt suffices after the LUT init)
    uint32_t neg = (uint32_t)x.val >> 31;
    uint32_t i   = neg ? (0u - (uint32_t)x.val) : (uint32_t)x.val;   // |x| (raw Q24; no -INT32_MIN UB)
    if (i >= Q24_ONE.val) {                                      // |x| >= 1
        return neg ? -Q24_PI_2 : Q24_PI_2;
    }
    int32_t u;
    if (i <= Q24_HALF.val) {                                     // |x| <= 0.5 fast path
        u = (int32_t)i;                                          // u = |x|
    } else {                                                     // 0.5 < |x| < 1
        u = q_sqrt(q24_t::from_raw((int32_t)((Q24_ONE.val - i) >> 1))).val;  // sqrt((1-|x|)/2)
    }
    // Horner: S(t) = (((C4*t + C3)*t + C2)*t + C1)*t + C0, t = u^2.
    // Shared q24_mac accumulator: mul+round+shift lives in one place.
    int32_t t = q24_mac(u, u, 0);                             // t = u^2
    int32_t s = q24_mac(Q24_ASIN_C4.val, t, Q24_ASIN_C3.val); // C4*t + C3
    s = q24_mac(s, t, Q24_ASIN_C2.val);                       // S*t + C2
    s = q24_mac(s, t, Q24_ASIN_C1.val);                       // S*t + C1
    s = q24_mac(s, t, Q24_ASIN_C0.val);                       // S*t + C0
    int32_t a = q24_mac(u, s, 0);                             // u*S(u^2)
    int32_t y = (i <= Q24_HALF.val) ? a                          // asin(|x|)
                : (Q24_PI_2.val - 2 * a);                        // pi/2 - 2*asin(u)
    return q24_t::from_raw(neg ? -y : y);
}

q24_t q_acos(q24_t x){
    if (x.val >= Q24_ONE.val)  return Q24_ZERO;
    if (x.val <= -Q24_ONE.val) return Q24_PI;
    return Q24_PI_2 - q_asin(x);
}

q24_t q_atan2(q24_t y, q24_t x){
    if (x.val > 0) {
        return q_atan(y / x);
    } else if (x.val < 0) {
        q24_t a = q_atan(y / x);
        return (y.val < 0) ? a - Q24_PI : a + Q24_PI;
    }
    // x == 0
    if (y.val > 0) return Q24_PI_2;
    if (y.val < 0) return -Q24_PI_2;
    return Q24_ZERO;  // (0,0) -> 0
}