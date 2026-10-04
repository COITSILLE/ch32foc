#pragma once

#include "ch32m030.h"
#include <stdint.h>


#define INV_DUTY_SCALE_BITS   16 //q16

static inline void inv_set_duty(uint16_t u, uint16_t v, uint16_t w)
{
    const uint32_t arr = (uint32_t)TIM1->ATRLR;
    TIM_SetCompare1(TIM1, (uint16_t)((((uint32_t)u + 1u) * arr) >> INV_DUTY_SCALE_BITS));
    TIM_SetCompare2(TIM1, (uint16_t)((((uint32_t)v + 1u) * arr) >> INV_DUTY_SCALE_BITS));
    TIM_SetCompare3(TIM1, (uint16_t)((((uint32_t)w + 1u) * arr) >> INV_DUTY_SCALE_BITS));
}
