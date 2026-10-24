#pragma once

#include "sys_interface.h"
#include "rtos_handler.h"
#include <stdint.h>

/**
 * @brief  Blocking delay by exact cycle count (SysTick CNT timestamp based).
 *         Only reads SysTick->CNT; no register is touched and counting stays continuous;
 *         the accuracy depends only on the fixed cost of reading CNT (~1-2 cycles);
 *         unsigned subtraction is safe across the 1ms auto-reload (total elapsed must be < 2^31 cycles).
 */
static inline void delay_cycles(uint32_t ncycles)
{
    if (ncycles == 0U) { return; }
    const uint32_t start = SysTick->CNT;
    while ((SysTick->CNT - start) < ncycles) {}
}

/**
 * @brief  Blocking delay in microseconds (SysTick CNT timestamp based).
 */
static inline void delay_us_nortos(uint32_t __us)
{
    const uint32_t cycles_per_us = SystemCoreClock / 1000000UL;
    const uint32_t max_us = (uint32_t)(0xFFFFFFFFUL / cycles_per_us);
    if (__us > max_us) { __us = max_us; }
    delay_cycles(__us * cycles_per_us);
}

/**
 * @brief  Blocking delay in milliseconds (SysTick CNT timestamp based).
 */
static inline void delay_ms_nortos(uint32_t ms)
{
    while (ms--) {
        delay_us_nortos(1000U);
    }
}

#if defined(USING_FREERTOS)

//TODO

#endif  // USING_FREERTOS