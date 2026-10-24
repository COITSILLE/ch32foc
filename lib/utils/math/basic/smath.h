#pragma once

/*================= CONFIGURATION ====================*/
#define FORCE_USING_S_MATH 1 // set to 1 to force using smath implementation even if FPU is present.
/*====================================================*/

#include <stdint.h>
#include "sys_interface.h"

#if defined(__FPU_PRESENT)
#define S_MATH_IMPL_CONDITION ((FORCE_USING_S_MATH == 1) || (__FPU_PRESENT == 0))
#else
#define S_MATH_IMPL_CONDITION (1)
#endif

#if S_MATH_IMPL_CONDITION
    #define HUGE_VAL (__builtin_huge_val())
    #define HUGE_VALF (__builtin_huge_valf())
    #define HUGE_VALL (__builtin_huge_vall())
    #define INFINITY (__builtin_inff())
    #define NAN (__builtin_nanf(""))
#else
    #include <math.h>
#endif

#ifndef MAXFLOAT
#define MAXFLOAT	3.40282347e+38F
#endif
#ifndef _M_LN2
#define _M_LN2      0.693147180559945309417
#endif
#ifndef M_E
#define M_E		    2.7182818284590452354F
#endif
#ifndef M_LOG2E
#define M_LOG2E		1.4426950408889634074F
#endif
#ifndef M_LOG10E
#define M_LOG10E	0.43429448190325182765F
#endif
#ifndef M_LN2
#define M_LN2		_M_LN2
#endif
#ifndef M_LN10
#define M_LN10		2.30258509299404568402F
#endif
#ifndef M_PI
#define M_PI		3.14159265358979323846F
#endif
#ifndef M_PI_2
#define M_PI_2		1.57079632679489661923F
#endif
#ifndef M_PI_4
#define M_PI_4		0.78539816339744830962F
#endif
#ifndef M_1_PI
#define M_1_PI		0.31830988618379067154F
#endif
#ifndef M_2_PI
#define M_2_PI		0.63661977236758134308F
#endif
#ifndef M_2_SQRTPI
#define M_2_SQRTPI	1.12837916709551257390F
#endif
#ifndef M_SQRT2
#define M_SQRT2		1.41421356237309504880F
#endif
#ifndef M_SQRT1_2
#define M_SQRT1_2	0.70710678118654752440F
#endif
#ifndef M_TWOPI
#define M_TWOPI     6.28318530717958647692F
#endif
#ifndef M_3PI_4
#define M_3PI_4		2.3561944901923448370E0F
#endif
#ifndef M_SQRTPI
#define M_SQRTPI    1.77245385090551602792981F
#endif
#ifndef M_LN2LO
#define M_LN2LO     1.9082149292705877000E-10F
#endif
#ifndef M_LN2HI
#define M_LN2HI     6.9314718036912381649E-1F
#endif
#ifndef M_SQRT3
#define M_SQRT3	    1.73205080756887719000F
#endif
#ifndef M_IVLN10
#define M_IVLN10    0.43429448190325182765F  // 1 / log(10)
#endif
#ifndef M_LOG2_E
#define M_LOG2_E    _M_LN2
#endif
#ifndef M_INVLN2
#define M_INVLN2    1.4426950408889633870E0  // 1 / log(2)
#endif

#ifndef RAD_TO_DEG_C
#define RAD_TO_DEG_C 57.2957795130823208768f
#endif
#ifndef DEG_TO_RAD_C
#define DEG_TO_RAD_C 0.01745329251994329577f
#endif
#ifndef RAD_TO_DEG
#define RAD_TO_DEG(x) ((x) * RAD_TO_DEG_C)
#endif
#ifndef DEG_TO_RAD
#define DEG_TO_RAD(x) ((x) * DEG_TO_RAD_C)
#endif

#ifndef ABS
#define ABS(x) ((x) >= 0 ? (x) : -(x))
#endif
#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef ROUND
#define ROUND(x) ((x) >= 0 ? (int)((x) + 0.5f) : (int)((x) - 0.5f))
#endif
#ifndef CEIL_DIV
#define CEIL_DIV(a, b) (((a) + (b) - 1) / (b))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** 
    Accuracy:
    [inv_sqrt] n=60000 max=4.58006389e-05 mean=3.88322327e-06 rmse=8.08861079e-06 p99=3.57047424e-05 worst_x=0.010009 fast=9.99535001 ref=9.99539581
    [sqrt] n=60000 max=3.60734531e-05 mean=1.06264713e-05 rmse=1.52922087e-05 p99=3.50191211e-05 worst_x=100.000000 fast=9.99996393 ref=10.00000000
    [sin] n=120000 max=1.24946933e-03 mean=5.35166139e-04 rmse=6.51059886e-04 p99=1.23671154e-03 worst_x=296.901306 fast=0.99853422 ref=0.99978368
    [cos] n=120000 max=1.24946940e-03 mean=5.35174746e-04 rmse=6.51066719e-04 p99=1.23671071e-03 worst_x=-295.330496 fast=0.99853449 ref=0.99978396
    [tan] n=219860 max=3.32670603e-03 mean=3.74713454e-04 rmse=8.13318766e-04 p99=3.07159119e-03 worst_x=0.785402 fast=1.00333385 ref=1.00000714
    [atan] n=120000 max=3.74167769e-03 mean=2.73219088e-04 rmse=5.55896264e-04 p99=2.98872106e-03 worst_x=6.925058 fast=1.42364286 ref=1.42738454
    [asin] n=120000 max=2.65374334e-03 mean=3.35483963e-04 rmse=6.39243309e-04 p99=2.42023841e-03 worst_x=-0.749998 fast=-0.84540519 ref=-0.84805893
    [acos] n=120000 max=2.65374334e-03 mean=3.35483963e-04 rmse=6.39243309e-04 p99=2.42023841e-03 worst_x=-0.749998 fast=2.41620151 ref=2.41885526
    [atan2] n=120000 max=3.74168047e-03 mean=2.35549961e-03 rmse=2.62679603e-03 p99=3.74107522e-03 worst=(x=83.298306, y=-576.237885) fast=2.99429001 ref=2.99803169
    
    Performance on 72MHz STM32F1 -O0(time in us):
    inv_sqrt std:10.786 s:6.407 ratio:0.59x
    sqrt     std:8.380 s:7.295 ratio:0.87x
    sin      std:16.732 s:11.451 ratio:0.68x
    cos      std:17.211 s:12.217 ratio:0.71x
    tan      std:27.883 s:12.149 ratio:0.44x
    asin     std:27.217 s:15.460 ratio:0.57x
    acos     std:25.316 s:18.420 ratio:0.73x
    atan     std:21.185 s:8.930 ratio:0.42x
    atan2    std:24.769 s:12.634 ratio:0.51x

    Performance on 200MHz Renesas RA6E2 -Og(time in us):
    inv_sqrt std:0.390 s:0.155 ratio:0.40x
    sqrt     std:0.285 s:0.315 ratio:1.11x
    sin      std:1.167 s:0.335 ratio:0.29x
    cos      std:1.214 s:0.425 ratio:0.35x
    tan      std:1.028 s:0.322 ratio:0.31x
    asin     std:1.008 s:0.726 ratio:0.72x
    acos     std:1.113 s:1.051 ratio:0.94x
    atan     std:0.797 s:0.259 ratio:0.32x
    atan2    std:1.462 s:0.523 ratio:0.36x

    Performance on 200MHz Renesas RA6E2 -O2(time in us):
    inv_sqrt std:0.185 s:0.145 ratio:0.78x
    sqrt     std:0.275 s:0.155 ratio:0.56x
    sin      std:0.911 s:0.308 ratio:0.34x
    cos      std:1.083 s:0.328 ratio:0.30x
    tan      std:0.967 s:0.291 ratio:0.30x
    asin     std:0.949 s:0.344 ratio:0.36x
    acos     std:0.878 s:0.494 ratio:0.56x
    atan     std:0.720 s:0.224 ratio:0.31x
    atan2    std:1.317 s:0.388 ratio:0.29x

    Performance on 80MHz MSPM0G3507 -Og(time in us):
    inv_sqrt std:31.937 s:30.994 ratio:0.97x
    sqrt     std:19.831 s:34.913 ratio:1.76x
    sin      std:70.470 s:44.578 ratio:0.63x
    cos      std:72.780 s:50.245 ratio:0.69x
    tan      std:124.072 s:52.146 ratio:0.42x
    asin     std:113.587 s:66.090 ratio:0.58x
    acos     std:105.882 s:76.634 ratio:0.72x
    atan     std:96.868 s:45.241 ratio:0.47x
    atan2    std:111.806 s:57.336 ratio:0.51x

    Performance on 80MHz MSPM0G3507 -Ofast, 2nd inv_sqrt iteration removed(time in us):
    inv_sqrt std:31.568 s:16.807 ratio:0.53x
    sqrt     std:19.714 s:20.413 ratio:1.04x
    sin      std:70.344 s:43.985 ratio:0.63x
    cos      std:72.653 s:49.210 ratio:0.68x
    tan      std:123.934 s:48.713 ratio:0.39x
    asin     std:113.450 s:49.482 ratio:0.44x
    acos     std:105.747 s:57.865 ratio:0.55x
    atan     std:96.736 s:43.133 ratio:0.45x
    atan2    std:114.464 s:57.601 ratio:0.50x

    Performance on CH32M030 -Ofast, 2nd inv_sqrt iteration removed(time in us):
    inv_sqrt std:25.698 s:16.106 ratio:0.62x
    sqrt     std:21.561 s:19.518 ratio:0.90x
    sin      std:67.270 s:30.409 ratio:0.48x
    cos      std:69.587 s:32.383 ratio:0.49x
    tan      std:119.237 s:36.324 ratio:0.30x
    asin     std:100.303 s:38.500 ratio:0.38x
    acos     std:94.396 s:42.748 ratio:0.45x
    atan     std:88.529 s:23.112 ratio:0.26x
    atan2    std:100.348 s:29.754 ratio:0.29x

    inv_sqrt, sqrt, asin and acos can be faster if removing the 2nd iteration in s_inv_sqrt
    */
float s_abs(float x);
float s_mod(float x, float y);
float s_inv_sqrt(float x);
float s_sqrt(float x);
float s_sin(float x);
float s_cos(float x);
float s_tan(float x);
float s_asin(float x);
float s_acos(float x);
float s_atan(float x);
float s_atan2(float y, float x);

#ifdef __cplusplus
}
#endif


