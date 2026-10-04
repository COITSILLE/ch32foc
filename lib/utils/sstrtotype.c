/** 
 * @brief Rewrite some useful string functions so that there's no need to 
        include the (for stm32) too large stdlib.h
 * @attention These are NOT equivalant at all to the standard string functions. Be careful when using.
*/
#include "sstrtotype.h"

#define NULL ((void*)0)

/**
 * @attention a correct str in the first call must be like "x,y,z\0", 
            while must NOT be like ",x,y,z\0", "x,,y,,,z\0" or "x,y,z,,\0" !
            Only single char delimiter is supported.
*/
char* s_strtok(char *str, const char delim){
    static char *saved_ptr = NULL;

    char *ptr;
    if (str == NULL){
        ptr = saved_ptr;
    }
    else{
        ptr = str;
        saved_ptr = NULL;
    }

    if (ptr == NULL) return NULL;

    uint8_t i = 0;
    while(*(ptr + i) != '\0'){
        if(*(ptr + i) == delim){
            *(ptr + i) = '\0';
            saved_ptr = ptr + i + 1;
            return ptr;
        }
        i++;
    }

    saved_ptr = NULL;
    return ptr;
}

/**
 * @attention a correct num must be like "1" while must NOT be like "a", "12", "-1" or "1a" !
 */
uint8_t s_ctoi(const char num){
    if (num >= '0' && num <= '9') {
        return num - '0';
    }
    return 0;
}

float s_atof(const char *str){
    const char *ptr = str;
    float result = 0.0f;

    _Bool sign = 0;
    _Bool point_flag = 0;
    if (*ptr == '-'){
        sign = 1;
        ptr++;
    }

    float multiplier = 1.0f;
    while (*ptr != '\0'){
        if (*ptr == '.'){
            point_flag = 1;
            ptr++;
        }

        if (!point_flag){
            result = result * 10.0f + (float)s_ctoi(*ptr);
        }
        else{
            multiplier *= 0.1f;
            result = result + (float)s_ctoi(*ptr) * multiplier;
        }
        ptr++;
    }

    return sign ? -result : result;
}

int32_t s_atoi(const char *str){
    const char *ptr = str;
    int32_t result = 0.0f;

    _Bool sign = 0;
    if (*ptr == '-'){
        sign = 1;
        ptr++;
    }
    while (*ptr != '\0'){
        if (*ptr == '.'){
            return sign ? -result : result;
        }
        result = result * 10 + s_ctoi(*ptr);
        ptr++;
    }

    return sign ? -result : result;
}

void s_reverse(char *str, uint16_t len){
    if (str == NULL || len == 0) return;
    char *start = str;
    char *end = str + len - 1;
    while (start < end) {
        char temp = *start;
        *start = *end;
        *end = temp;
        start++;
        end--;
    }
}

uint32_t s_itoa(const int32_t num, char *str, const uint8_t base){
    if (str == NULL){
        return 0;
    }
    if (base < 2 || base > 36){
        str[0] = '\0';
        return 0;
    }

    uint32_t i = 0;
    if (num == 0){
        str[i] = '0';
        str[i+1] = '\0';
        return 1;
    }

    _Bool sign = 0;
    if (num < 0 && base == 10){
        sign = 1;
    }

    uint32_t n = sign ? (uint32_t)(-(int64_t)num) : (uint32_t)num;
    while (n != 0){
        uint8_t rem = n % base;
        str[i++] = (rem > 9)? (rem - 10) + 'A' : rem + '0';
        n = n / base;
    }

    if (sign){
        str[i++] = '-';
    }
    str[i] = '\0';
    s_reverse(str, i);
    return i;
}

uint32_t s_ftoa(const float num, char *str, uint8_t precision){
    if (str == NULL){
        return 0;
    }

    _Bool sign = 0;
    float n = num;
    /* n < 0 对 -0.0 为假，用符号位一并判负零 */
    if (n < 0.0f || (n == 0.0f && __builtin_signbitf(num))){
        sign = 1;
        n = -n;
    }

    /* scale = 10^precision：把数值放大为定点整数后一次四舍五入，
     * 小数位再用整数运算逐位抽取，避免逐轮浮点累积误差。
     * precision == 0 时不输出小数点。 */
    uint32_t scale = 1u;
    float fscale = 1.0f;
    for (uint8_t i = 0; i < precision; i++){
        fscale *= 10.0f;
        scale  *= 10u;
    }
    uint32_t fixed = (uint32_t)(n * fscale + 0.5f); /* 四舍五入到定点整数 */
    uint32_t ip    = fixed / scale;                 /* 整数部分 */
    uint32_t fp    = fixed % scale;                 /* 小数部分(×scale) */

    char *ptr = str;
    if (sign){
        *ptr = '-';
        ptr++;
    }
    uint32_t ip_len = s_itoa((int32_t)ip, ptr, 10);
    while (*ptr != '\0') ptr++;

    uint32_t written = ip_len + (sign ? 1u : 0u);
    if (precision != 0){
        *ptr = '.';
        ptr++;
        uint8_t p = precision;
        while (p != 0){
            fp *= 10u;
            *ptr = (char)('0' + (fp / scale));
            fp %= scale;
            ptr++;
            p--;
        }
        written += 1u + precision;
    }
    *ptr = '\0';
    return written;
}
/**
 * @brief Join an array of floats into a string with specified delimiter and precision. This is to avoid using printf_float libs which is too large.
 */
void s_joinf(char *str, const char delim, const float *args, uint16_t argc, uint8_t precision){
    char *ptr = str;
    if (ptr == NULL) return;
    
    if (args == NULL || argc == 0) {
        ptr[0] = '\0';
        return;
    }
    
    while(*ptr != '\0') ptr++;
    for (uint16_t i = 0; i < argc; i++){
        s_ftoa(args[i], ptr, precision);
        while (*ptr != '\0') ptr++;
        if (i != argc - 1){
            *ptr = delim;
            ptr++;
        }
    }
    *ptr = '\0';
}
