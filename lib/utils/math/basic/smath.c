/** 
 * @brief fast math functions for float type, using some approximations and look-up tables.
 * @attention only for MCUs without FPU(like STM32F103), for MCUs with FPU, use the standard math library instead. It's actually faster.
*/
#include "smath.h"

#if S_MATH_IMPL_CONDITION
float s_abs(float x){
    return x >= 0 ? x : -x;
}

float s_mod(float x, float y){
    if (y == 0) return NAN; // handle division by zero
    float mod = x - y * (int)(x / y);
    return mod;
}

float s_inv_sqrt(float x){
    long i;  
    float x2, y;  
    const float threehalfs = 1.5F;  
    
    x2 = x * 0.5F;  
    y = x;  
    i = * ( long * ) &y;                       // evil floating point bit level hacking  
    i = 0x5f3759df - ( i >> 1 );               // what the fuck?  
    y = * ( float * ) &i;  
    y = y * ( threehalfs - ( x2 * y * y ) );   // 1st iteration  
    // y = y * ( threehalfs - ( x2 * y * y ) );   // 2nd iteration, this can be removed  
 
    return y;  
}

float s_sqrt(float x){
    return x * s_inv_sqrt(x);
}

static const float sin_table_rad[] = {
    0.0f,                                   // sin(0)
    0.049979169270678331f,                  // sin(0.05)
    0.099833416646828155f,                  // sin(0.1)
    0.14943813247359924f,                   // sin(0.15)
    0.19866933079506122f,                   // sin(0.2)
    0.24740395925452294f,                   // sin(0.25)
    0.2955202066613396f,                    // sin(0.3)
    0.3428978074554514f,                    // sin(0.35)
    0.38941834230865052f,                   // sin(0.4)
    0.43496553411123023f,                   // sin(0.45)
    0.47942553860420301f,                   // sin(0.5)
    0.52268722893065922f,                   // sin(0.55)
    0.56464247339503548f,                   // sin(0.6)
    0.60518640573603955f,                   // sin(0.65)
    0.64421768723769113f,                   // sin(0.7)
    0.68163876002333412f,                   // sin(0.75)
    0.71735609089952279f,                   // sin(0.8)
    0.75128040514029271f,                   // sin(0.85)
    0.78332690962748341f,                   // sin(0.9)
    0.81341550478937374f,                   // sin(0.95)
    0.8414709848078965f,                    // sin(1.0)
    0.86742322559401697f,                   // sin(1.05)
    0.89120736006143542f,                   // sin(1.1)
    0.9127639402605211f,                    // sin(1.15)
    0.9320390859672264f,                    // sin(1.2)
    0.9489846193555862f,                    // sin(1.25)
    0.96355818541719296f,                   // sin(1.3)
    0.97572335782665909f,                   // sin(1.35)
    0.98544972998846025f,                   // sin(1.4)
    0.99271299103758848f,                   // sin(1.45)
    0.99749498660405445f,                   // sin(1.5)
    0.99978376418935699f,                   // sin(1.55)
    0.99957360304150511f,                   // sin(1.6)
    };

float s_sin(float x){
    
    if (x < 0){
        while(x < 0) x += M_TWOPI;
    }
    else{
        while(x > M_TWOPI) x -= M_TWOPI;
    }
    uint8_t sign = 0;
    if (x > M_PI){
        sign = 1;
        x -= M_PI;
    }
    x = x > M_PI_2? (M_PI - x) : x;

    float idx_float = x * 20.0f;
    int idx_low = (int)idx_float;
    int idx_high = idx_low + 1;
    
    if (idx_high >= 33) {
        return sign ? -1.0f : 1.0f;  // sin(π/2)=1, sin(3π/2)=-1
    }
    
    float t = idx_float - idx_low;

    float y = sin_table_rad[idx_low] * (1.0f - t) + sin_table_rad[idx_high] * t;
    
    return sign ? -y : y;
}

float s_cos(float x){
    return s_sin(x + M_PI_2);
}

float s_tan(float x){
    //TODO: approximate
    if (x < 0){
        while(x < 0) x += M_PI;
    }
    else{
        while(x > M_PI) x -= M_PI;
    }
    uint8_t sign = 0;
    if (x > M_PI_2){
        sign = 1;
        x = M_PI - x;
    }
	if (x < M_PI_4){
        float x2 = x * x;
        float res = x * (1.0f + x2 * (0.33333333f + x2 * (0.13333333f + 0.05396825397f * x2)));
		return sign ? -res : res;
    }
	else{
		float m = M_PI_2 - x;
		float m2 = m * m;
        float res = 1.0f / (m * (1.0f + m2 * (0.33333333f + m2 * (0.13333333f + 0.05396825397f * m2))));
		return sign ? -res : res;
    }
}


float s_atan(float x){
    if (x > 1.0f) {
        float inv_x = 1.0f / x;
        return M_PI_2 - (inv_x * M_PI_4 + 0.273082f * inv_x * (1.0f - ABS(inv_x)));
    } else if (x < -1.0f) {
        float inv_x = 1.0f / x;
        return -M_PI_2 - (inv_x * M_PI_4 + 0.273082f * inv_x * (1.0f - ABS(inv_x)));
    }
    return x * M_PI_4 + 0.273082f * x * (1.0f - ABS(x));
}

float s_asin(float x){
    float i = ABS(x);
    if (i >= 1.0f) return (x > 0) ? M_PI_2 : -M_PI_2;
    if (i < 0.85f)
        return (3 * x) / (2 + s_sqrt(1.0f - x * x));
    else {
        float m = 1.0f - i;
        float res = M_PI_2 - (M_SQRT2 * s_sqrt(m) * (1 + 0.0833333f * m));
        return (x < 0) ? -res : res;
    }
}

float s_acos(float x){
    if (x >= 1.0f) return 0;
    if (x <= -1.0f) return M_PI;
    return M_PI_2 - s_asin(x);
}

float s_atan2(float y, float x){
    float ratio = y / x; 
    if (x > 0) {
        return s_atan(ratio);
    } 
    else if (x < 0) {
        return s_atan(ratio) + ((y < 0) ? -M_PI : M_PI);
    } 
    else {  // x == 0
        return (y > 0) ? M_PI_2 : (y < 0) ? -M_PI_2 : 0.0f;
    }
}

#else
// for MCUs with FPU, use the standard math library instead. The performance doesn't justify the additional accuracy loss.
float s_abs(float x){
    return fabsf(x);
}

float s_inv_sqrt(float x){
    return 1.0f / sqrtf(x);
}

float s_sqrt(float x){
    return sqrtf(x);
}

float s_sin(float x){
    return sinf(x);
}

float s_cos(float x){
    return cosf(x);
}

float s_tan(float x){
    return tanf(x);
}

float s_atan(float x){
    return atanf(x);
}

float s_asin(float x){
    return asinf(x);
}

float s_acos(float x){
    return acosf(x);
}

float s_atan2(float y, float x){
    return atan2f(y, x);
}

#endif