#pragma once

#include "ch32m030_conf.h"

/* 注：模拟 SSI 的接线/通道宏已随驱动一起收进 lib/drivers/simu_ssi.h 的
 * SimuSSIConfig 配置项（默认值即原宏值），这里不再需要 include 那个 C++ 头。 */

/* ===== ADC 注入组配置（ADC1_Init 使用）===== */
#define FOC_ADC_JL          2u      /* 注入通道数：= ADC1_Init 里 ADC_InjectedSequencerLengthConfig 的实参 */
#define FOC_ADC_SMPL_SEL    ADC_SampleTime_5Cycles5   /* 给 ADC 用的采样时间选择 */
#define FOC_ADC_ADCPRE_SEL  RCC_HB_Div8                /* 给 RCC 用的 ADC 时钟分频选择 */

#define VBUS_MIN_V  8.5f
#define VBUS_MAX_V  21.0f
#define VBUS_FULL   (3.3f * (14e3f + 100e3f) / 14e3f)

#ifdef __cplusplus
extern "C" {
#endif

#define UART_DMA_TX_CHANNEL  DMA1_Channel6
#define UART_DMA_TX_TC_FLAG  DMA1_FLAG_TC6
#define UART_DMA_RX_CHANNEL  DMA1_Channel7
#define GPIO_Pin_Port_LED     GPIOA
#define GPIO_Pin_Port_SEL     GPIOB
#define GPIO_Pin_LED         GPIO_Pin_7
#define GPIO_Pin_SEL         GPIO_Pin_0

void UART_Init(uint32_t baudrate);
void GPIO_LED_Init();
void GPIO_Sel_Init();
void TIM1_Init(void);
void TIM2_Init(void);   /* 采样开始硬件标记：TIM2 从模式复位(TRGO) + PA5/PA4 输出 */
void TIM3_Init(void);   /* 编码器驱动：2kHz 更新中断 */

#define SIMU_SSI_TIM        TIM3

#define SSI_CLK_PIN         GPIO_Pin_5
#define SSI_DIO_PIN         GPIO_Pin_4
#define SSI_CSN_PIN         GPIO_Pin_1
#define SSI_CSN_PIN_PORT    GPIOB
// DMA1_Channel3 is triggered by TIM3_UP, DMA1_Channel1 is triggered by TIM3_CH1
// meaning that CLK_DMA will run first, then DIO_DMA will run after 1/2 period when CLK is falling.
// note: SSI requires sampling on high level of CLK
#define SSI_CLK_DMA_CHANNEL DMA1_Channel1
#define SSI_DIO_DMA_CHANNEL DMA1_Channel3
#define SSI_CLK_DMA_IRQn    DMA1_Channel1_IRQn
#define SSI_DIO_DMA_IRQn    DMA1_Channel3_IRQn
#define SSI_CLK_DMA_TC_FLAG    DMA1_FLAG_TC1
#define SSI_DIO_DMA_TC_FLAG    DMA1_FLAG_TC3
//the remaining 8 bits are for the next frame, which will be discarded
#define SSI_EDGES 48u
#define SSI_BITS  56u

void Simu_SSI_Perips_Init();   /* 模拟 SSI：用于与外部设备通信 */
void OPA_Init(void);
void ADC1_Init(void);
void USART1_DMA_TX_Init(void);   /* 闭环 1kHz DMA 遥测：DMA1_Channel6(=USART1_TX) + TC 中断 */

void ADC1_ResetVbusIrq();
#ifdef __cplusplus
}
#endif