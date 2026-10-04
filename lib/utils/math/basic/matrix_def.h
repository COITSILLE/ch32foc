#pragma once

/*================= CONFIGURATION ====================*/
#define SYSTEM_INTERFACE "debug.h" // the header that act as the interface to the underlying system, it should include necessary headers and define necessary macros for smath to work.
/*====================================================*/

#include SYSTEM_INTERFACE

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
}

template<size_t R, size_t C>
struct matrix_t {
    static constexpr size_t row = R;
    static constexpr size_t col = C;
    static constexpr size_t size = R * C;

    float data[size];

    float& operator[](size_t i)       { return data[i]; }
    float  operator[](size_t i) const { return data[i]; }

    float  at(size_t r, size_t c) const { return data[r * C + c]; }
    float& at(size_t r, size_t c)       { return data[r * C + c]; }
};

const matrix_t<2, 2> identity_2x2 = {{
    1.0f, 0.0f,
    0.0f, 1.0f
}};
const matrix_t<3, 3> identity_3x3 = {{
    1.0f, 0.0f, 0.0f,
    0.0f, 1.0f, 0.0f,
    0.0f, 0.0f, 1.0f
}};

const matrix_t<4, 4> identity_4x4 = {{
    1.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 1.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 1.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 1.0f
}};

const matrix_t<2, 2> zero_2x2 = {{0.0f}};
const matrix_t<3, 3> zero_3x3 = {{0.0f}};
const matrix_t<4, 4> zero_4x4 = {{0.0f}};

template<size_t N>
using Vec_C_t = matrix_t<N, 1>;    // N 维列向量

template<size_t N>
using Vec_R_t = matrix_t<1, N>;    // N 维行向量

#endif