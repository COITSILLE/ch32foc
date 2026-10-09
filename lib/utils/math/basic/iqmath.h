#pragma once

/**
 * @file    iqmath.h
 * @brief   Fast math for Q7.24 fixed-point type (q24_t).
 * @author  COITSILLE
 * @date    2026.8.30
 * @attention Default config: QMATH_ENABLE_FAST_DIV;
 *            the accuracy/perf figures below assume that configuration.
 */

/* ===================== 1. CONFIGS ===================== */
/** Fast division (LUT + 1 Newton, worst err <= 8 LSB); else 64-bit truncating div (< 1 LSB). */
#define QMATH_ENABLE_FAST_DIV
/* ====================================================== */

/* ===================== 2. EXCEPTION CONVENTION ===================== */
/**
 * @brief All out-of-range / invalid-input results follow one rule set:
 *        - Overflow saturation (result outside Q7.24)  -> clamp to +-Q24_MAX
 *        - Division by zero (operator/ only)           -> saturate to +-Q24_MAX
 *        - Undefined domain (sqrt/inv_sqrt of <=0, q_mod y==0, atan2(0,0)) -> 0
 *        - Domain-edge clamps (asin/acos |x| >= 1)     -> +-PI/2 / PI
 */
/* =================================================================== */

/**
 * @brief  Accuracy report (QMATH_ENABLE_FAST_DIV on).
 * @details 1 LSB = 2^-24 = 5.96e-08
 *
 *          [cvtq24float] n=200000 max=7.5697899e-06 (127.00 LSB)   mean=3.0629337e-07  rmse=1.064079e-06   p99=6.0200691e-06   worst_q=127.68169                   fast=127.68168      ref=127.68169
 *          [cvtfloatq24] n=200000 max=5.9597369e-08 (1.00 LSB)     mean=9.7274754e-09  rmse=1.9559155e-08  p99=5.736274e-08    worst_x=-1.7880666e-07              fast=-1.1920929e-07 ref=-1.7880666e-07
 *          [mul]         n=200000 max=2.9802209e-08 (0.50 LSB)     mean=1.4885023e-08  rmse=1.718817e-08   p99=2.9494032e-08   worst_a=-9.3157828,b=4.4280643      fast=-41.250886     ref=-41.250886
 *          [div_fast]    n=200000 max=4.3638398e-07 (7.32 LSB)     mean=3.1489065e-08  rmse=3.9329491e-08  p99=1.1185976e-07   worst_a=0.014857411,b=0.00012391806 fast=119.89707      ref=119.89707
 *          [div]         n=200000 max=5.9604357e-08 (1.00 LSB)     mean=2.774896e-08   rmse=3.2893854e-08  p99=5.8858276e-08   worst_a=-1.2570974,b=3.7200627      fast=-0.33792365    ref=-0.33792371
 *
 *          Notes:
 *          - cvtq24float: mantissa truncation, error <= 1 ulp (max 127 LSB at |q| near 128).
 *          - cvtfloatq24: truncation toward zero, error < 1 LSB; |x| >= 128 saturates to
 *            +-127.9999999; +-Inf saturates likewise, NaN -> 0.
 *
 *          [inv_sqrt]    n=200000 max=9.60387258e-04 mean=3.35862478e-05 rmse=1.05682544e-04 p99=5.41772945e-04 worst_x=0.000061   fast=127.93850613   ref=127.93754574
 *          [sqrt]        n=200000 max=7.34155599e-08 mean=2.95560535e-08 rmse=3.46663125e-08 p99=5.94478917e-08 worst_x=75.449825  fast=8.68618578     ref=8.68618585
 *          [sin]         n=200000 max=3.12214703e-04 mean=1.02877521e-04 rmse=1.40309399e-04 p99=3.05769251e-04 worst_x=26.749307  fast=0.99864054     ref=0.99895275
 *          [cos]         n=200000 max=3.12213874e-04 mean=1.58035809e-04 rmse=1.87108636e-04 p99=3.09025211e-04 worst_x=-0.045785  fast=0.99863982     ref=0.99895204
 *          [tan]         n=200000 max=1.01932823e-03 mean=6.05961559e-06 rmse=2.63097283e-05 p99=7.46286429e-05 worst_x=-7.864307  fast=96.84545445    ref=96.84443513
 *          [atan]        n=200000 max=3.02350140e-05 mean=1.76161398e-05 rmse=1.94532323e-05 p99=3.01501840e-05 worst_x=-21.041932 fast=-1.52333814    ref=-1.52330790
 *          [asin]        n=200000 max=8.15643652e-05 mean=4.20732874e-07 rmse=8.97424590e-07 p99=3.48783087e-06 worst_x=-1.000000  fast=-1.57010585    ref=-1.57002429
 *          [acos]        n=200000 max=8.15484720e-05 mean=4.22805182e-07 rmse=8.97643585e-07 p99=3.48734921e-06 worst_x=-1.000000  fast=3.14090216     ref=3.14082061
 *          [atan2]       n=200000 max=7.75766650e-03 mean=2.97773572e-05 rmse=2.83271716e-04 p99=3.00823472e-05 worst=(x=0.002742, y=-61.724046) fast=-1.56299424 ref=-1.57075191
 */

/**
 * @brief  Performance on CH32M030 @ -Og (time in us), CALC_SAT, FAST_DIV, q_sqrt 2 iters.
 * @details
 *          add         float:3.0852    q24:1.0980      ratio:0.36x
 *          sub         float:3.2381    q24:1.1258      ratio:0.35x
 *          mul         float:3.5716    q24:1.0424      ratio:0.29x
 *          div         float:4.0302    q24:2.8351      ratio:0.70x
 *          inv_sqrt    std:20.7409     q24:10.5182     ratio:0.51x
 *          sqrt        std:17.0218     q24:7.5912      ratio:0.45x
 *          sin         std:76.3568     q24:6.2901      ratio:0.08x
 *          cos         std:77.0536     q24:7.2984      ratio:0.09x
 *          tan         std:124.0708    q24:9.6479      ratio:0.08x
 *          asin        std:96.4254     q24:8.1679      ratio:0.08x
 *          acos        std:90.9241     q24:9.5298      ratio:0.10x
 *          atan        std:76.4906     q24:7.1569      ratio:0.09x
 *          atan2       std:83.3903     q24:9.5889      ratio:0.11x
 */

#ifdef __cplusplus

#include <stdint.h>
#include <type_traits>

/* ===================== 3. TYPE & BASIC CONVERSIONS ===================== */

/**
 * @brief Q7.24 fixed-point wrapper type (underlying int32).
 */
struct q24_t;
/** @brief Underlying storage type of Q7.24 (int32). */
using q24_raw_t = int32_t;
#ifndef bit_cast
#define bit_cast __builtin_bit_cast
#endif

/**
 * @brief  Q7.24 fixed-point -> float32 conversion.
 * @param[in] q Q7.24 fixed-point value.
 * @return float32 representation (mantissa truncation, error <= 1 ulp).
 * @note constexpr: usable both at compile time and runtime.
 * @performance CH32M030 @ -Og: ~0.97 us/op.
 */
constexpr float cvtq24float(q24_t q);

/**
 * @brief  float32 -> Q7.24 fixed-point conversion.
 * @param[in] x float32 input.
 * @return Q7.24 fixed-point value (truncation toward zero, error < 1 LSB);
 *         |x| >= 128 saturates to +/-127.9999999; NaN/subnormal/0 -> 0.
 * @note constexpr: usable both at compile time and runtime.
 * @performance CH32M030 @ -Og: ~1.28 us/op.
 */
constexpr q24_t q24(float x);
constexpr q24_t q24(int x);
/** @brief Delete double: avoid silent double->float truncation in q24(3.14); write 3.14f instead. */
constexpr q24_t q24(double) = delete;

#define __TEMPLATE_INT_ONLY template<typename T, typename std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>, int> = 0>
#define __TEMPLATE_FLOAT_ONLY template<typename T, typename std::enable_if_t<std::is_floating_point_v<T>, int> = 0>
#define __TEMPLATE_VOLATILE_ONLY template<typename T, typename std::enable_if_t<std::is_volatile_v<T>, int> = 0>

class q24_t{
public:
    q24_raw_t val;  ///< raw Q7.24 value

    /** @brief Construct from any integral math value (bool excluded, avoids implicit 0/1 traps).
     *  Treats the int as a real number: q24_t(32) == q24_t(32.0f) == 32.0.
     *  For raw bit re-interpretation use q24_t::from_raw(), never this ctor. */
    __TEMPLATE_INT_ONLY
    constexpr explicit q24_t(T x) : val(q24(static_cast<int>(x)).val) {};
    /** @brief Construct from float (saturating conversion via q24(float)). */
    constexpr explicit q24_t(float x) : val(q24(x).val) {};
    constexpr explicit q24_t(double) = delete;
    /** @brief Default-construct to 0. */
    constexpr explicit q24_t() : val(0) {};
    constexpr q24_t(const q24_t&) = default;

    // Declaring operator=(const volatile q24_t&) counts as a copy-assignment operator and
    // suppresses the implicit one; restore it explicitly so plain copy-assign stays constexpr.
    constexpr q24_t& operator=(const q24_t&) = default;
    /** @brief Assign to a volatile object (e.g. theta_ = Q24_ZERO), like native volatile int. */
    volatile q24_t& operator=(const q24_t& other) volatile { val = other.val; return *this; }
    /** @brief Copy from a volatile shared variable: single volatile-read snapshot. */
    q24_t(const volatile q24_t& other) : val(other.val) {}
    /** @brief Copy-assign from a volatile source: single volatile-read snapshot. */
    q24_t& operator=(const volatile q24_t& other) {
        val = other.val; 
        return *this;
    }

    /** @brief Convert to float (cvtq24float, error <= 1 ulp).
     *  Must re-interpret .val as raw bits via from_raw: a math ctor would saturate
     *  (val=16777216 -> Q24_MAX=127.9999 instead of 1.0). */
    constexpr float cvt2float() const{
        return cvtq24float(q24_t::from_raw(this->val));
    };
    /** @brief Convert a volatile lvalue to float (e.g. (float)theta_), single volatile read. */
    float cvt2float() const volatile{
        return cvtq24float(q24_t::from_raw(val));
    };
    /** @brief Explicit (float) cast, same as cvt2float(). */
    constexpr explicit operator float() const{
        return cvtq24float(q24_t::from_raw(this->val));
    };
    /** @brief (float) cast of a volatile lvalue, single volatile read. */
    explicit operator float() const volatile{
        return cvtq24float(q24_t::from_raw(val));
    };
    // Integer conversion: returns the mathematical integer part, truncated toward zero
    // (matches (int)1.5f==1, (int)-1.5f==-1). Bare val>>24 floors negatives (-1.5 -> -2),
    // so add 1 to turn floor into truncation-toward-zero. For raw bits use raw()/.val,
    // never this conversion.

    __TEMPLATE_INT_ONLY
    constexpr explicit operator T() const{
        const int32_t r = val >> 24;
        return static_cast<T>((val < 0 && (val & 0xFFFFFF) != 0) ? r + 1 : r);
    };
    /** @brief (int) cast of a volatile lvalue: snapshot val once, avoids multiple volatile reads. */
    __TEMPLATE_INT_ONLY
    explicit operator T() const volatile{
        const int32_t v = val;
        const int32_t r = v >> 24;
        return static_cast<T>((v < 0 && (v & 0xFFFFFF) != 0) ? r + 1 : r);
    };
    /** @brief Raw Q7.24 bit access (bit-packing / DMA / config storage). */
    constexpr q24_raw_t raw() const { return val; }
    /** @brief const volatile overload: single volatile read for ISR side. */
    q24_raw_t raw() const volatile { return val; }

    // constexpr explicit q24_t (q24_raw_t val) : val(val) {}
    static constexpr q24_t from_raw(q24_raw_t val) { return q24_t(val, from_raw_tag{}); }
private:
    struct from_raw_tag{};
    constexpr q24_t(q24_raw_t val, from_raw_tag) : val(val) {}
};

// constexpr conversion implementation (GCC): usable both in compile-time constant
// evaluation and at runtime; __builtin_bit_cast does the float <-> uint32_t punning.
constexpr float cvtq24float(q24_t q){
    if(q.val == 0){
        return 0.0f;
    }
    uint32_t sign = 0;
    uint32_t abs_val = (uint32_t)q.val;
    if(q.val < 0){
        sign = 0x80000000;
        abs_val = 0u - abs_val;    // unsigned negation (avoids -INT32_MIN UB)
    }

    uint32_t zero_cnt = __builtin_clz(abs_val);
    uint32_t valid_cnt = 32 - zero_cnt;
    constexpr uint32_t exp_bias = -24 + 127 - 1;

    uint32_t exp_bits = (valid_cnt + exp_bias) << 23;
    // Left-shift the leading 1 out past bit 31 (that is the point: it must NOT
    // land in the mantissa) and keep the top 23 fraction bits. Masking the
    // shift count with 31 keeps this branch-free and avoids the shift-by-32
    // that is UB in C++ when abs_val == 1 (only then there are no fraction
    // bits to keep, so (1 << 0) >> 9 = 0 is still the correct mantissa).
    uint32_t mantissa_bits = (abs_val << ((zero_cnt + 1) & 31)) >> 9;

    uint32_t result = sign | exp_bits | mantissa_bits;
    return bit_cast(float, result);
};

constexpr q24_t q24(float x){
    uint32_t raw = bit_cast(uint32_t, x);
    uint32_t exp = (raw >> 23) & 0xFF;
    uint32_t mantissa = raw & 0x007FFFFF;
    if(exp == 0){                       // +-0 / subnormal -> 0
        return q24_t::from_raw(0);
    }
    if(exp == 0xFFu){                   // Inf -> saturate, NaN -> 0
        if(mantissa != 0)   return q24_t::from_raw(0);
        return (raw & 0x80000000u) ? q24_t(-INT32_MAX) : q24_t(INT32_MAX);
    }
    uint32_t sign = raw & 0x80000000;
    uint32_t abs_val = mantissa | 0x00800000;
    constexpr int32_t exp_bias = -127 + 1;
    int32_t shift = (int32_t)exp + exp_bias;
    if(shift >= 8){
        abs_val = INT32_MAX;
    } else if(shift >= 0){
        abs_val <<= shift;
    } else if(shift > -24){
        abs_val >>= (int32_t)(0u - (uint32_t)shift);
    } else {
        abs_val = 0;
    }
    if(sign){
        return q24_t::from_raw(-(int32_t)abs_val);
    } else {
        return q24_t::from_raw((int32_t)abs_val);
    }
}

constexpr q24_t q24(int x){
    if(x >= 128) return q24_t::from_raw(INT32_MAX);
    if(x <= -128) return q24_t::from_raw(INT32_MIN);
    // Shift via unsigned: left-shifting a negative int is UB / rejected in constant
    // expressions. |x| <= 127 here, so the round-trip cast keeps the exact bits.
    return q24_t::from_raw(static_cast<int32_t>(static_cast<uint32_t>(x) << 24));
}

/* ===================== 4. INTERNAL HELPERS ===================== */

/** @brief Saturate a 64-bit intermediate to Q7.24 [-128, 127.9999999]. */
static inline constexpr q24_t satq24(int64_t x){
    if(x > 0x7FFFFFFF) return q24_t::from_raw(0x7FFFFFFF);
    if(x < INT32_MIN)  return q24_t::from_raw(0x80000000);
    return q24_t::from_raw((int32_t)x);
}

static inline constexpr bool is_float_ov_q24(float x){
    uint32_t raw = bit_cast(uint32_t, x);
    uint32_t exp = (raw >> 23) & 0xFF;
    uint32_t mant = raw & 0x007FFFFFu;
    uint32_t sign = raw & 0x80000000u;
    if (exp == 0xFFu) return true;            // Inf / NaN：非法，走 float 路径
    if (exp >= 134){
        // |x| >= 128：仅恰好 -128.0f（Q24_MIN，负号且尾数为 0）合法
        return !(sign && (exp == 134) && (mant == 0));
    }
    return false;                             // 0 / 次正规 / |x| < 128：均在范围内
}

static inline constexpr int float_ov_q24(float x){
    uint32_t raw = bit_cast(uint32_t, x);
    uint32_t exp = (raw >> 23) & 0xFF;
    uint32_t mant = raw & 0x007FFFFFu;
    uint32_t sign = raw & 0x80000000u;
    if (exp == 0xFFu){
        if (mant != 0) return 2;              // NaN
        return sign ? -1 : 1;                 // -Inf / +Inf
    }
    if (exp >= 134){
        if (sign && (exp == 134) && (mant == 0)) return 0;  // -128.0f no overflow
        return sign ? -1 : 1;                 // 负下溢 / 正上溢
    }
    return 0;                                 // 0 / 次正规 / |x| < 128：no overflow
}

/* ===================== 5. ARITHMETIC OPERATORS ===================== */

static inline __attribute__((always_inline)) constexpr q24_t q24_add_w(q24_t a, q24_t b){
    return q24_t::from_raw((int32_t)((uint32_t)a.val + (uint32_t)b.val));
}

static inline __attribute__((always_inline)) constexpr q24_t q24_sub_w(q24_t a, q24_t b){
    return q24_t::from_raw((int32_t)((uint32_t)a.val - (uint32_t)b.val));
}

/* Exact (un-clamped) Q7.24 multiply: rounded 64-bit product; may exceed int32 when the
 * true result leaves Q7.24. The saturating operator* clamps via satq24; q24_mul_w narrows
 * it for callers who guarantee the result stays in range. */
static inline __attribute__((always_inline))constexpr int64_t q24_mul(q24_t a, q24_t b){
    return ((int64_t)a.val * (int64_t)b.val + (1 << 23)) >> 24;
}

/* Non-saturating multiply: caller guarantees |a*b| < 128 (result fits Q7.24), so the low
 * 32 bits of q24_mul are exact. The 64-bit intermediate is unavoidable (raw product ~2^62);
 * this only skips the satq24 clamp. */
static inline __attribute__((always_inline)) constexpr q24_t q24_mul_w(q24_t a, q24_t b){
    return q24_t::from_raw((int32_t)q24_mul(a, b));
}

// All basic arithmetic ops (+ - * / and unary minus) are header-local constexpr and
// foldable at compile time, e.g. constexpr X = A*B (see FOC_ADC_SCALE_V in foc_openloop.h).
// Operators ALWAYS saturate; reach for the *_w helpers where a call site must not clamp.
/** @brief Saturating addition, bit-exact within range. */
static inline constexpr q24_t operator+(q24_t a, q24_t b){
    return satq24((int64_t)a.val + (int64_t)b.val);
}

/** @brief Unary minus; negating INT32_MIN saturates to INT32_MAX. */
static inline __attribute__((always_inline)) constexpr q24_t operator-(q24_t a){
    return q24_t::from_raw((a.val == INT32_MIN) ? INT32_MAX : -a.val);
}
/** @brief Saturating subtraction, bit-exact within range. */
static inline constexpr q24_t operator-(q24_t a, q24_t b){
    return satq24((int64_t)a.val - (int64_t)b.val);
}

/** @brief Saturating multiply, round-half-up, error <= 0.5 LSB. */
static inline constexpr q24_t operator*(q24_t a, q24_t b){
    return satq24(q24_mul(a, b));
}

/** @brief Exact division (64-bit truncating, < 1 LSB; defined in iqmath.cpp). */
q24_t q24_div(q24_t a, q24_t b);
/** @brief LUT+Newton fast division (worst err <= 8 LSB; defined in iqmath.cpp, LUT only in .cpp). */
q24_t q24_fast_div(q24_t a, q24_t b);
/** @brief Division operator: constant evaluation uses the 64-bit exact path, runtime dispatches to q24_fast_div. Div-by-zero -> +/-max. */
static inline constexpr q24_t operator/(q24_t a, q24_t b){
    if (__builtin_is_constant_evaluated()) {
        if (b.val == 0) return q24_t::from_raw(a.val >= 0 ? INT32_MAX : INT32_MIN);
        int64_t quotient = ((int64_t)a.val << 24) / (int64_t)b.val;
        return satq24(quotient);
    }
#ifdef QMATH_ENABLE_FAST_DIV
    return q24_fast_div(a, b);
#else
    return q24_div(a, b);
#endif
}

/* ===================== 6. OPERATOR SUGAR ===================== */

// The overloads in this section are syntactic sugar (int mixed arithmetic, compound
// assignment, comparisons, volatile support), generated with small local macros to avoid
// repetitive text. They are all header-local static inline and must stay here: declaring
// always_inline and defining in .cpp would make the body invisible across TUs
// ("function body not available").
//
// The macros are grouped into operator "families" that share the same parameter tuple;
// each family macro emits all overload flavors (plain / int-mixed / float-mixed /
// volatile) for one operator in a single body, so each macro is invoked once per operator.

/* ---- 6.1 Binary arithmetic  + - * /  (param: OP) ---- */
// int is treated as a math constant via q24(int) saturating conversion (float semantics):
// q + 5 == q + 5.0f. float/double treated as math constants via q24(float) (saturating).
// Template constrained to floating-point types only, so double binds T=double directly
// (no ambiguity with the int overloads); long/int literals fall back to the int overloads.
#define Q24_DEF_ARITH_OP(OP) \
    static inline constexpr q24_t operator OP(q24_t a, int b){ return a OP q24(b); } \
    static inline constexpr q24_t operator OP(int a, q24_t b){ return q24(a) OP b; } \
    __TEMPLATE_FLOAT_ONLY \
    static inline constexpr q24_t operator OP(q24_t a, T b){ return is_float_ov_q24(b) ? q24(a.cvt2float() OP (float)b) : a OP q24((float)b); } \
    __TEMPLATE_FLOAT_ONLY \
    static inline constexpr q24_t operator OP(T a, q24_t b){ return is_float_ov_q24(a) ? q24((float)a OP b.cvt2float()) : q24((float)a) OP b; }
Q24_DEF_ARITH_OP(+)
Q24_DEF_ARITH_OP(-)
Q24_DEF_ARITH_OP(*)
Q24_DEF_ARITH_OP(/)
#undef Q24_DEF_ARITH_OP

/* ---- 6.2 Compound assignment  += -= *= /=  (params: OP, BOP) ---- */
// Reuse the constexpr basic ops above (and the int mixed ops for ++/--); foldable at
// compile time, inlineable at call sites. volatile: RMW non-atomic, same as native
// volatile int (use std::atomic or single-writer discipline for atomics); always_inline
// must inline in ISRs to avoid unpredictable latency.
#define Q24_DEF_ASSIGN_OP(OP, BOP) \
    static inline constexpr void operator OP(q24_t &a, q24_t b){ a = a BOP b; } \
    static inline constexpr void operator OP(q24_t &a, int b){ a = a BOP q24(b); } \
    __TEMPLATE_FLOAT_ONLY \
    static inline constexpr void operator OP(q24_t &a, T b){ a = a BOP q24((float)b); } \
    static inline void __attribute__((always_inline)) operator OP(volatile q24_t &a, q24_t b){ a.val = (a BOP b).val; } \
    static inline void __attribute__((always_inline)) operator OP(volatile q24_t &a, int b){ a = a BOP q24(b); } \
    __TEMPLATE_FLOAT_ONLY \
    static inline void __attribute__((always_inline)) operator OP(volatile q24_t &a, T b){ a = a BOP q24((float)b); }
Q24_DEF_ASSIGN_OP(+=, +)
Q24_DEF_ASSIGN_OP(-=, -)
Q24_DEF_ASSIGN_OP(*=, *)
Q24_DEF_ASSIGN_OP(/=, /)
#undef Q24_DEF_ASSIGN_OP

/* ---- 6.3 Increment / decrement  ++ --  (params: OP, BOP) ---- */
// Postfix returns the old value (single volatile-read snapshot on volatile).
#define Q24_DEF_INC_OP(OP, BOP) \
    static inline constexpr void operator OP(q24_t &a){ a = a BOP 1; } \
    static inline constexpr q24_t operator OP(q24_t &a, int){ q24_t old = a; a = a BOP 1; return old; } \
    static inline void __attribute__((always_inline)) operator OP(volatile q24_t &a){ a.val = (a BOP 1).val; } \
    static inline q24_t __attribute__((always_inline)) operator OP(volatile q24_t &a, int){ q24_t old = a; a.val = (a BOP 1).val; return old; }
Q24_DEF_INC_OP(++, +)
Q24_DEF_INC_OP(--, -)
#undef Q24_DEF_INC_OP

/* ---- 6.4 Comparison  > < >= <= == !=  ---- */
// int is treated as a math constant (float semantics): q > 5 == q > 5.0f.
// Never mix with q24_raw_t: that compares raw bits as math values (16777216 > 5 disaster).
// q24(int) saturates for |b| >= 128, so decide out-of-range ints up front
// (b >= 128 -> always < ; b <= -N -> always > ; within [-128,127] q24(b)==b<<24 exactly).
// volatile compares via SFINAE on std::is_volatile_v (single volatile read of .val), so
// only volatile lvalues match; non-volatile/const args fall back to the plain overloads.
//
// NOTE: reverse operators are generated AFTER all forward operators, because their body
// (b ROP a) calls the forward operator ROP(q24_t, int/float) of the paired token, which
// must already be declared.

// int / volatile forward family (params: OP, TOP, BOT, N)
#define Q24_DEF_CMP_INT(OP, TOP, BOT, N) \
    static inline constexpr bool operator OP(q24_t a, q24_t b){ return a.val OP b.val; } \
    static inline constexpr bool operator OP(q24_t a, int b){ \
        return (b >= 128) ? TOP : (b <= -N) ? BOT : a.val OP q24(b).val; } \
    template<typename T, typename std::enable_if_t<std::is_volatile_v<T>, int> = 0> \
    static inline bool operator OP (T &a, q24_t b){ return a.val OP b.val; }
Q24_DEF_CMP_INT(>, false, true, 129)
Q24_DEF_CMP_INT(<, true, false, 128)
Q24_DEF_CMP_INT(>=, false, true, 128)
Q24_DEF_CMP_INT(<=, true, false, 129)
Q24_DEF_CMP_INT(==, false, false, 129)
Q24_DEF_CMP_INT(!=, true, true, 129)
#undef Q24_DEF_CMP_INT

// int reverse family (params: OP, ROP); must come after all forward int overloads.
#define Q24_DEF_REV_CMP_INT(OP, ROP) \
    static inline constexpr bool operator OP(int a, q24_t b){ return b ROP a; }
Q24_DEF_REV_CMP_INT(>, <)
Q24_DEF_REV_CMP_INT(<, >)
Q24_DEF_REV_CMP_INT(>=, <=)
Q24_DEF_REV_CMP_INT(<=, >=)
Q24_DEF_REV_CMP_INT(==, ==)
Q24_DEF_REV_CMP_INT(!=, !=)
#undef Q24_DEF_REV_CMP_INT

// float/double forward family (params: OP, TOP, BOT, NANV); float_ov_q24 classifies:
// +2 NaN -> NANV, +1 +Inf/over -> TOP, -1 -Inf/under -> BOT, else exact compare.
#define Q24_DEF_CMP_FLT(OP, TOP, BOT, NANV) \
    __TEMPLATE_FLOAT_ONLY \
    static inline constexpr bool operator OP(q24_t a, T b){ \
        int cond = float_ov_q24(b); \
        if (cond == 2) return NANV; \
        if (cond == 1) return TOP; \
        if (cond == -1) return BOT; \
        return a.val OP q24((float)b).val; }
Q24_DEF_CMP_FLT(>, false, true, false)
Q24_DEF_CMP_FLT(<, true, false, false)
Q24_DEF_CMP_FLT(>=, false, true, false)
Q24_DEF_CMP_FLT(<=, true, false, false)
Q24_DEF_CMP_FLT(==, false, false, false)
Q24_DEF_CMP_FLT(!=, true, true, true)
#undef Q24_DEF_CMP_FLT

// float/double reverse family (params: OP, ROP); must come after all forward float overloads.
#define Q24_DEF_REV_CMP_FLT(OP, ROP) \
    __TEMPLATE_FLOAT_ONLY \
    static inline constexpr bool operator OP(T a, q24_t b){ return b ROP a; }
Q24_DEF_REV_CMP_FLT(>, <)
Q24_DEF_REV_CMP_FLT(<, >)
Q24_DEF_REV_CMP_FLT(>=, <=)
Q24_DEF_REV_CMP_FLT(<=, >=)
Q24_DEF_REV_CMP_FLT(==, ==)
Q24_DEF_REV_CMP_FLT(!=, !=)
#undef Q24_DEF_REV_CMP_FLT

/* ===================== 7. Q24 CONSTANTS ===================== */

/**
 * @name    Q24 constants
 * @brief   Usable directly in expressions (e.g. x * Q24_PI).
 * @note All constexpr, usable in constant expressions / static_assert.
 *       ln2/log2e/ln10/log10e are dual-named aliases (C library M_LN2/M_LOG2E...
 *       vs TI IQmath LOG2_E/LOG2E... naming).
 *       Keep the raw hexadecimal values, do NOT switch to q24(float): float literals
 *       truncate toward zero and would shift some constants by 1 LSB.
 * @{
 */
constexpr q24_t Q24_MAX          = q24_t::from_raw(INT32_MAX);                  /*  127.9999999 */
constexpr q24_t Q24_MIN          = q24_t::from_raw(INT32_MIN);                  /* -128.0        */
constexpr q24_t Q24_ZERO         = q24_t::from_raw(0);                          /*    0.0        */
constexpr q24_t Q24_ONE          = q24_t::from_raw(1 << 24);                    /*    1.0        */
constexpr q24_t Q24_HALF         = q24_t::from_raw(1 << 23);                    /*    0.5        */
constexpr q24_t Q24_TWO          = q24_t::from_raw(2 << 24);                    /*    2.0        */
constexpr q24_t Q24_LN2          = q24_t::from_raw(0x00B17218);                 /*    0.6931472  ln2      */
constexpr q24_t Q24_LOG2_E       = Q24_LN2;                           /* alias = ln2           */
constexpr q24_t Q24_E            = q24_t::from_raw(0x02B7E151);                 /*    2.7182818  e        */
constexpr q24_t Q24_LOG2E        = q24_t::from_raw(0x01715476);                 /*    1.4426950  log2(e)  */
constexpr q24_t Q24_INVLN2       = Q24_LOG2E;                         /* alias = 1/ln2         */
constexpr q24_t Q24_LN10         = q24_t::from_raw(0x024D7637);                 /*    2.3025851  ln10     */
constexpr q24_t Q24_LOG10E       = q24_t::from_raw(0x006F2DEC);                 /*    0.4342945  log10(e) */
constexpr q24_t Q24_IVLN10       = Q24_LOG10E;                        /* alias = 1/ln10        */
constexpr q24_t Q24_PI           = q24_t::from_raw(0x03243F6B);                 /*    3.1415927  π        */
constexpr q24_t Q24_PI_2         = q24_t::from_raw(0x01921FB5);                 /*    1.5707963  π/2      */
constexpr q24_t Q24_TWOPI        = q24_t::from_raw(0x06487ED5);                 /*    6.2831853  2π       */
constexpr q24_t Q24_PI_4         = q24_t::from_raw(0x00C90FDB);                 /*    0.7853982  π/4      */
constexpr q24_t Q24_3PI_4        = q24_t::from_raw(0x025B2F90);                 /*    2.3561945  3π/4     */
constexpr q24_t Q24_1_PI         = q24_t::from_raw(0x00517CC2);                 /*    0.3183099  1/π      */
constexpr q24_t Q24_2_PI         = q24_t::from_raw(0x00A2F983);                 /*    0.6366198  2/π      */
constexpr q24_t Q24_SQRT2        = q24_t::from_raw(0x016A09E6);                 /*    1.4142136  √2       */
constexpr q24_t Q24_SQRT1_2      = q24_t::from_raw(0x00B504F3);                 /*    0.7071068  1/√２     */
constexpr q24_t Q24_SQRT3        = q24_t::from_raw(0x01BB67AF);                 /*    1.7320508  √3       */
constexpr q24_t Q24_SQRTPI       = q24_t::from_raw(0x01C5BF89);                 /*    1.7724539  √π       */
constexpr q24_t Q24_2_SQRTPI     = q24_t::from_raw(0x0120DD75);                 /*    1.1283792  2/√π     */
/** @} */

/* ===================== 8. MATH FUNCTION DECLARATIONS ===================== */

/** @brief Power: x^y for any integer y (negative exponent = reciprocal); 0^0 = 1; overflow saturates. */
q24_t q_pow(q24_t x, int y);

/**
 * @brief Remainder: x - y * trunc(x / y).
 * @param[in] x dividend
 * @param[in] y divisor
 * @return remainder; 0 if y == 0. Bit-exact when |x/y| < 128.
 */
q24_t q_mod(q24_t x, q24_t y);

/** @brief Absolute value; |INT32_MIN| saturates to INT32_MAX. */
q24_t q_abs(q24_t x);

/** @brief 1/sqrt(x); 0 for x <= 0. Worst err ~9.6e-4 at x = 1/128^2. */
q24_t q_inv_sqrt(q24_t x);

/**
 * @brief sqrt(x); 0 for x <= 0.
 * @param[in] x non-negative Q7.24 input
 * @return sqrt(x) (Q7.24). LUT seed + 2 Newton iterations internally.
 */
q24_t q_sqrt(q24_t x);

/** @brief sin(x), x in radians. 0.05 rad LUT + linear interp, err ~3.1e-4. */
q24_t q_sin(q24_t x);

/** @brief cos(x), x in radians; implemented as sin(x + pi/2); |x| > ~126.4 saturates. */
q24_t q_cos(q24_t x);

/** @brief Simultaneous sin(x) & cos(x), x in radians: one shared reduction + quadrant split.
 *  Saves one q_mod division vs calling q_sin/q_cos separately for the same angle.
 *  @param[out] s  sin(x)   (may be NULL)
 *  @param[out] c  cos(x)   (may be NULL) */
void q_sincos(q24_t x, q24_t* s, q24_t* c);

/** @brief tan(x), x in radians; err grows near poles (x -> pi/2). */
q24_t q_tan(q24_t x);

/** @brief asin(x), x in [-1, 1]; err grows as |x| -> 1 (input quantization). */
q24_t q_asin(q24_t x);

/** @brief acos(x), x in [-1, 1]; err grows as |x| -> 1. */
q24_t q_acos(q24_t x);

/** @brief atan(x), x in [-128, 128); err ~3e-5. */
q24_t q_atan(q24_t x);

/** @brief atan2(y, x) over all quadrants; err grows when |y/x| exceeds Q7.24 range. */
q24_t q_atan2(q24_t y, q24_t x);

#endif