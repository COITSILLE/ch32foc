#pragma once

#include "sys_interface.h"
#include "rtos_handler.h"
#include <stdint.h>

/*!
 * @brief  Blocking delay by exact cycle count (SysTick CNT timestamp based).
 *         只读 SysTick->CNT，不改任何寄存器，不破坏连续计数；
 *         精度只取决于读 CNT 的固定开销（约 1~2 周期）；
 *         无符号减法可安全跨 1ms 自动重载（总流逝须 < 2^31 周期）。
 */
static inline void delay_cycles(uint32_t ncycles)
{
    if (ncycles == 0U) { return; }
    const uint32_t start = SysTick->CNT;
    while ((SysTick->CNT - start) < ncycles) {
        /* 空转 */
    }
}

/*!
 * @brief  Blocking delay in microseconds (SysTick CNT timestamp based).
 */
static inline void delay_us_nortos(uint32_t __us)
{
    const uint32_t cycles_per_us = SystemCoreClock / 1000000UL;
    const uint32_t max_us = (uint32_t)(0xFFFFFFFFUL / cycles_per_us);
    if (__us > max_us) { __us = max_us; }
    delay_cycles(__us * cycles_per_us);
}

/*!
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

#endif /* USING_FREERTOS */