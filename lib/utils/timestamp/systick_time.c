#include "systick_time.h"

/*
 *   QingKeV3_Processor_Manual.PDF
 *   STK_CTLR: [5]INIT [4]MODE(1=down,0=up) [3]STRE(auto-reload)
 *             [2]STCLK(1=HCLK) [1]STIE(irq) [0]STE(enable)
 *   STK_SR  : [0]STK_ISR0 interrupt flag
 */
void SystickTimestamp_init(void){
    #if defined(NO_RTOS)
    uint32_t reload = __CORECLOCK_HZ__ / 1000U;
    if (reload == 0U)
    {
        return;
    }

    SysTick->CTLR = 0U;
    SysTick->SR   = 0U;
    SysTick->CNT  = 0U;
    SysTick->CMP  = reload - 1U;

    /* STE | STIE | STCLK | STRE，MODE=0 向上计数，1ms 周期中断 */
    SysTick->CTLR = (1U<<0) | (1U<<1) | (1U<<2) | (1U<<3);
    NVIC_EnableIRQ(SysTick_IRQn);
    __asm volatile ("fence.i");   /* 同步 PFIC 中断使能状态 */
    #elif defined(USING_FREERTOS)
    return; // FreeRTOS will call vApplicationTickHook on each tick, no need to configure SysTick here
    #endif
}

#ifdef UINT64_TIMESTAMP
volatile uint64_t ms_ticks = 0;
uint64_t Systick_getMsTick(void){
    return ms_ticks;
}
#else
volatile uint32_t ms_ticks = 0;
uint32_t Systick_getMsTick(void){
    return ms_ticks;
}
#endif

uint64_t Systick_getUsTick(void){
    uint32_t ticks;
    uint32_t systick_current;
    do{
        ticks = ms_ticks;
        systick_current = SysTick->CNT;
    } while(ticks != ms_ticks);
    /* 向上计数：CNT 从 0 增到 CMP，经过时间 = CNT / HCLK */
    uint64_t us_part = (uint64_t)systick_current * 1000000 / __CORECLOCK_HZ__;
    return (uint64_t)ticks * 1000 + us_part;
}

#if defined(NO_RTOS)
__attribute__((interrupt("WCH-Interrupt-fast")))
void SysTick_Handler(void){
    SysTick->SR &= ~(1U << 0);  /* 清 STK_ISR0 中断标志 */
    ms_ticks++;
}
#elif defined (USING_FREERTOS)
void vApplicationTickHook(void)
{
    ms_ticks++;
}
#endif

//#endif /*RTOS*/