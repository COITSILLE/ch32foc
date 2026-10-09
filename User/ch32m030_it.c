/********************************** (C) COPYRIGHT *******************************
 * File Name          : ch32m030_it.c
 * Author             : WCH
 * Version            : V1.0.0
 * Date               : 2024/09/01
 * Description        : Main Interrupt Service Routines.
*********************************************************************************
* Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
* Attention: This software (modified or not) and binary are used for 
* microcontroller manufactured by Nanjing Qinheng Microelectronics.
*******************************************************************************/
#include "ch32m030_it.h"
#include "ch32m030.h"
#include "ch32m030_gpio.h"
#include "ch32m030_adc.h"
#include "ch32m030_tim.h"
#include "ch32m030_uart.h"
#include <stdint.h>

void NMI_Handler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void HardFault_Handler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void ADC_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void DMA1_Channel3_IRQHandler(void) __attribute__((interrupt));
void USART1_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void TIM2_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));

void ADC_JEOC_irqHandler(void);
void updateEncoder(void);
void ssi_irqHandler();
void task_outer_loop(void);

/*********************************************************************
 * @fn      NMI_Handler
 *
 * @brief   This function handles NMI exception.
 *
 * @return  none
 */
void NMI_Handler(void)
{
    while(1)
    {
        __NOP();
    }
}

/*********************************************************************
 * @fn      HardFault_Handler
 *
 * @brief   This function handles Hard Fault exception.
 *
 * @return  none
 */
void HardFault_Handler(void)
{
    NVIC_SystemReset();
    while (1)
    {
        __NOP();
    }
}

void DMA1_Channel3_IRQHandler(void)
{
    if (DMA_GetITStatus(DMA1_IT_TC3))
    {
        DMA_ClearITPendingBit(DMA1_IT_TC3);
        ssi_irqHandler();
    }
}

/*********************************************************************
 * @fn      ADC1_IRQHandler
 *
 * @brief   ADC1_2 Interrupt Service Function.
 *
 * @return  none
 */
void ADC_IRQHandler(void)
{
    if (ADC_GetITStatus(ADC1, ADC_IT_JEOC))
    {
        ADC_JEOC_irqHandler();
    }

    ADC_ClearITPendingBit(ADC1, ADC_IT_JEOC);
}

void USART1_IRQHandler(void)
{

    //irqHandler()
    
}

void TIM2_IRQHandler(void){
    if (TIM_GetITStatus(TIM2, TIM_IT_Update) == SET){
        task_outer_loop();
    }
    TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
}