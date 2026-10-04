#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "sys_interface.h"
#include <stdint.h>
#include "rtos_handler.h"

#if defined(USING_FREERTOS)
 #include "FreeRTOSConfig.h"
#endif

#ifdef UINT64_TIMESTAMP
 extern volatile uint64_t ms_ticks;
#else
 extern volatile uint32_t ms_ticks;
#endif

#define NON_BLOCK_DELAY(t)                      \
    do{                                         \
        static uint32_t lastmm = 0;             \
        if (Systick_getMsTick() - lastmm < t) return; \
        lastmm = Systick_getMsTick();           \
    }while (0)

/**
 * @brief Initialize SysTick timekeeping.
 * @param None.
 * @return None.
 */
void SystickTimestamp_init(void);
/**
 * @brief Get the millisecond tick.
 * @param None.
 * @return Tick count in milliseconds.
 */
uint32_t Systick_getMsTick(void);
/**
 * @brief Get the microsecond tick.
 * @param None.
 * @return Tick count in microseconds.
 */
uint64_t Systick_getUsTick(void);

#if defined(NO_RTOS)
/**
 * @brief SysTick interrupt handler.
 * @param None.
 * @return None.
 */

extern void SysTick_Handler(void);
#elif defined (USING_FREERTOS)
 #if (configUSE_TICK_HOOK != 1)
  #error configUSE_TICK_HOOK must be set to 1 in FreeRTOSConfig.h to use Systick timekeeping with FreeRTOS.
 #endif
extern void vApplicationTickHook(void);
#endif

#ifdef __cplusplus
}
#endif

//#endif /*RTOS*/

