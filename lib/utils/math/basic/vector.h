#pragma once

#include "matrix_def.h"

#ifndef NAN
#define NAN __builtin_nanf("")
#endif

#ifdef __cplusplus

using Vec1_t = float;
#define Vec1_NAN NAN

struct Vec2_t : matrix_t<2, 1> {
    float& x()       { return data[0]; }
    float  x() const { return data[0]; }
    float& y()       { return data[1]; }
    float  y() const { return data[1]; }
};

inline Vec2_t operator-(Vec2_t a, Vec2_t b) { return {{a.x()-b.x(), a.y()-b.y()}}; }
inline Vec2_t operator+(Vec2_t a, Vec2_t b) { return {{a.x()+b.x(), a.y()+b.y()}}; }

struct Vec3_t : matrix_t<3, 1> {
    float& x()       { return data[0]; }
    float  x() const { return data[0]; }
    float& y()       { return data[1]; }
    float  y() const { return data[1]; }
    float& z()       { return data[2]; }
    float  z() const { return data[2]; }
};

inline Vec3_t operator-(Vec3_t a, Vec3_t b) { return {{a.x()-b.x(), a.y()-b.y(), a.z()-b.z()}}; }
inline Vec3_t operator+(Vec3_t a, Vec3_t b) { return {{a.x()+b.x(), a.y()+b.y(), a.z()+b.z()}}; }

constexpr Vec2_t Vec2_NAN = {{NAN, NAN}};
constexpr Vec3_t Vec3_NAN = {{NAN, NAN, NAN}};

#endif