#include "ch32m030.h"
#include "ch32m030_adc.h"
#include "ch32m030_tim.h"
#include "ch32m030_dma.h"
#include "ch32m030_uart.h"
#include "core_riscv.h"
#include "debug.h"
#include "peripheral.h"
#include "system_ch32m030.h"
#include "systick_time.h"
#include "foc_config.h"

void UART_Init(uint32_t baudrate){
    GPIO_InitTypeDef  GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;

    RCC_PB2PeriphClockCmd(RCC_PB2Periph_USART1 | RCC_PB2Periph_GPIOB | RCC_PB2Periph_AFIO, ENABLE);
    GPIO_PinRemapConfig(GPIO_PartialRemap2_USART1, ENABLE); //PB6

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_30MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_OD;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    USART_InitStructure.USART_BaudRate = baudrate;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;

    USART_IrDACmd(USART1, DISABLE);
    USART_Init(USART1, &USART_InitStructure);
    USART_Cmd(USART1, ENABLE);
    USART_HalfDuplexCmd(USART1, ENABLE);

    NVIC_SetPriority(USART1_IRQn, (uint8_t)(3 << 6));
    NVIC_EnableIRQ(USART1_IRQn);

    DMA_InitTypeDef dma = {0};

    RCC_HBPeriphClockCmd(RCC_HBPeriph_DMA1, ENABLE);
    DMA_DeInit(UART_DMA_TX_CHANNEL);
    DMA_DeInit(UART_DMA_RX_CHANNEL);

    dma.DMA_PeripheralBaseAddr = (uint32_t)&USART1->DATAR;
    dma.DMA_MemoryBaseAddr     = (uint32_t)NULL;
    dma.DMA_DIR                = DMA_DIR_PeripheralDST;
    dma.DMA_BufferSize         = 256; //Enough big, handled by interrupt
    dma.DMA_PeripheralInc      = DMA_PeripheralInc_Disable;
    dma.DMA_MemoryInc          = DMA_MemoryInc_Enable;
    dma.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    dma.DMA_MemoryDataSize     = DMA_MemoryDataSize_Byte;
    dma.DMA_Mode               = DMA_Mode_Normal;
    dma.DMA_Priority           = DMA_Priority_High;
    dma.DMA_M2M                = DMA_M2M_Disable;
    DMA_Init(UART_DMA_TX_CHANNEL, &dma);
    dma.DMA_PeripheralBaseAddr = (uint32_t)&USART1->DATAR;
    dma.DMA_MemoryBaseAddr     = (uint32_t)NULL;
    dma.DMA_DIR                = DMA_DIR_PeripheralSRC;
    dma.DMA_BufferSize         = 256; //Enough big, handled by interrupt
    dma.DMA_Mode               = DMA_Mode_Circular;
    DMA_Init(UART_DMA_RX_CHANNEL, &dma);
}

/*********************************************************************
 * @fn      USART1_DMA_TX_Init
 *
 * @brief   2kHz 遥测的 DMA 发送配置（USART1_TX → DMA1_Channel6，Normal 模式）。
 *          需在 USART_Printf_Init(2000000) 之后调用（本函数不改波特率）。
 *          每帧的 MADDR/CNTR 与重使能由 app 层（main.cpp tm_tx_frame）逐帧做；
 *          这里只做一次性的通道/中断/外设 DMA 请求使能。
 * @return  none
 */
void USART1_DMA_TX_Init(void)
{
    DMA_InitTypeDef dma = {0};

    RCC_HBPeriphClockCmd(RCC_HBPeriph_DMA1, ENABLE);
    DMA_DeInit(DMA1_Channel6);

    dma.DMA_PeripheralBaseAddr = (uint32_t)&USART1->DATAR;
    dma.DMA_MemoryBaseAddr     = 0;                 /* 每帧装载时由 app 层设 */
    dma.DMA_DIR                = DMA_DIR_PeripheralDST;
    dma.DMA_BufferSize         = 44;                /* 10 通道×4B + 尾 4B = 44（app 层每帧重设 CNTR） */
    dma.DMA_PeripheralInc      = DMA_PeripheralInc_Disable;
    dma.DMA_MemoryInc          = DMA_MemoryInc_Enable;
    dma.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    dma.DMA_MemoryDataSize     = DMA_MemoryDataSize_Byte;
    dma.DMA_Mode               = DMA_Mode_Normal;   /* 发完硬件自清 EN，逐帧重武装 */
    dma.DMA_Priority           = DMA_Priority_High;
    dma.DMA_M2M                = DMA_M2M_Disable;
    DMA_Init(DMA1_Channel6, &dma);

    /* 不再用 DMA TC 中断：发送完成由启动器(main.cpp tm_tx_1k)轮询 DMA1_FLAG_TC6 判断，
     * 因此这里不使能 TC IT / NVIC（避免无 ISR 时进 weak 死循环）。 */
    USART_DMACmd(USART1, USART_DMAReq_Tx, ENABLE);
}

void GPIO_LED_Init(){
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    RCC_PB2PeriphClockCmd(RCC_PB2Periph_GPIOA, ENABLE);
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_7;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_30MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
    GPIO_SetBits(GPIOA, GPIO_Pin_7);
}

void GPIO_Sel_Init(){
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    RCC_PB2PeriphClockCmd(RCC_PB2Periph_GPIOB, ENABLE);
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_30MHz;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    // EXTEN->EXTEN_KEYR &= 0x00000000; // reset;
    EXTEN->EXTEN_KEYR |= EXTEN_KEY1;
    EXTEN->EXTEN_KEYR |= EXTEN_KEY2;

    EXTEN->EXTEN_CTLR1 &= ~(EXTEN_UDP_PDE);
    EXTEN->EXTEN_CTLR1 |= EXTEN_UDP_PUE;
    EXTEN->EXTEN_CTLR1 &= ~(EXTEN_UDP_AE);

    EXTEN->EXTEN_CTLR1 &= ~(EXTEN_UDP_DAC);
    EXTEN->EXTEN_CTLR1 |= 0xFFFFFFFF & EXTEN_UDP_DAC; // DAC[5:0] = 111111, Pull-up 15.5kohm

    EXTEN->EXTEN_CTLR1 &= ~(EXTEN_UDP_BUFOE); //enable
}

void TIM1_Init(){
    const uint16_t arr = 3600 - 1;
    const uint16_t psc = 1 - 1;
    const uint16_t ccp = 1800;  /* 50% */

    GPIO_InitTypeDef        GPIO_InitStructure = {0};
    TIM_OCInitTypeDef       TIM_OCInitStructure = {0};
    TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure = {0};
    TIM_BDTRInitTypeDef     TIM_BDTRInitStructure = {0};

    RCC_PB2PeriphClockCmd(RCC_PB2Periph_GPIOA | RCC_PB2Periph_GPIOB | RCC_PB2Periph_TIM1 | RCC_PB2Periph_AFIO, ENABLE);

    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_30MHz;
    /* TIM1_CH1 */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;
    GPIO_Init(GPIOB, &GPIO_InitStructure);
    /* TIM1_CH1N */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_8;
    GPIO_Init(GPIOB, &GPIO_InitStructure);
    /* TIM1_CH2 */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_11;
    GPIO_Init(GPIOB, &GPIO_InitStructure);
    /* TIM1_CH2N */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
    GPIO_Init(GPIOB, &GPIO_InitStructure);
    /* TIM1_CH3 */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_13;
    GPIO_Init(GPIOB, &GPIO_InitStructure);
    /* TIM1_CH3N */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_12;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    TIM_TimeBaseInitStructure.TIM_Period = arr;
    TIM_TimeBaseInitStructure.TIM_Prescaler = psc;
    TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    // mode 2 means capture-compare interrupt is triggered on upcounting
    TIM_TimeBaseInitStructure.TIM_CounterMode = TIM_CounterMode_CenterAligned2; 
    TIM_TimeBaseInit(TIM1, &TIM_TimeBaseInitStructure);

    TIM_OCInitStructure.TIM_OCMode = TIM_OCMode_PWM1;
    TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
    TIM_OCInitStructure.TIM_OutputNState = TIM_OutputNState_Enable;
    TIM_OCInitStructure.TIM_Pulse = ccp;
    TIM_OCInitStructure.TIM_OCPolarity = TIM_OCPolarity_High;
    TIM_OCInitStructure.TIM_OCNPolarity = TIM_OCNPolarity_High;
    TIM_OCInitStructure.TIM_OCIdleState = TIM_OCIdleState_Reset;
    TIM_OCInitStructure.TIM_OCNIdleState = TIM_OCNIdleState_Reset;
    TIM_OC1Init(TIM1, &TIM_OCInitStructure);
    TIM_OC2Init(TIM1, &TIM_OCInitStructure);
    TIM_OC3Init(TIM1, &TIM_OCInitStructure);

    // calculate deadtime register value
    const float deadtime_ns = 100.0f;
    const float tdtg_ns = 1e9f / __CORECLOCK_HZ__;
    uint16_t dtg = (uint16_t)(deadtime_ns / tdtg_ns + 0.999f);  /* ceil */
    if (dtg > 0xFFu) dtg = 0xFFu;

    TIM_BDTRInitStructure.TIM_OSSIState = TIM_OSSIState_Enable;
    TIM_BDTRInitStructure.TIM_OSSRState = TIM_OSSRState_Enable;
    TIM_BDTRInitStructure.TIM_LOCKLevel = TIM_LOCKLevel_OFF;
    TIM_BDTRInitStructure.TIM_DeadTime = dtg;
    TIM_BDTRInitStructure.TIM_Break = TIM_Break_Disable;
    TIM_BDTRInitStructure.TIM_AutomaticOutput = TIM_AutomaticOutput_Disable;
    TIM_BDTRConfig(TIM1, &TIM_BDTRInitStructure);

    TIM_CtrlPWMOutputs(TIM1, ENABLE);
    TIM_OC1PreloadConfig(TIM1, TIM_OCPreload_Enable);
    TIM_OC2PreloadConfig(TIM1, TIM_OCPreload_Enable);
    TIM_OC3PreloadConfig(TIM1, TIM_OCPreload_Enable);
    TIM_OC5PreloadConfig(TIM1, TIM_OCPreload_Disable);

    // triggers ADC injected conversion
    const int16_t trig_bias = -50; //the sign is relevant to the center aligned mode
    TIM_SetCompare5(TIM1, arr - 1 + trig_bias);

    TIM_ARRPreloadConfig(TIM1, ENABLE);

    TIM1->SWEVGR = TIM_UG;
    TIM_Cmd(TIM1, ENABLE);
}

void TIM2_Init(void)
{
    TIM_TimeBaseInitTypeDef t = {0};

    RCC_PB1PeriphClockCmd(RCC_PB1Periph_TIM2, ENABLE);

    t.TIM_Prescaler = 72 - 1;    
    t.TIM_Period = 1000 - 1; // 1kHz
    t.TIM_ClockDivision = TIM_CKD_DIV1;
    t.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM2, &t);

    TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);
    NVIC_SetPriority(TIM2_IRQn, 2 << 6);
    NVIC_EnableIRQ(TIM2_IRQn);

    TIM_Cmd(TIM2, ENABLE);
}

#define TIM3_PERIOD 72

void TIM3_Init(void)
{
    TIM_TimeBaseInitTypeDef t = {0};
    TIM_OCInitTypeDef       oc = {0};

    RCC_PB1PeriphClockCmd(RCC_PB1Periph_TIM3, ENABLE);

    t.TIM_Prescaler    = 1 - 1;    
    t.TIM_Period       = TIM3_PERIOD - 1;  /* ARR：1 → 72MHz/72/1 = 1MHz */
    t.TIM_ClockDivision = TIM_CKD_DIV1;
    t.TIM_CounterMode   = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM3, &t);

    oc.TIM_OCMode = TIM_OCMode_PWM1;
    oc.TIM_OutputState = TIM_OutputState_Disable;
    oc.TIM_OCNIdleState = TIM_OCNIdleState_Reset;
    oc.TIM_OCPolarity = TIM_OCPolarity_High;
    oc.TIM_Pulse = TIM3_PERIOD / 2;  /* 50% */
    TIM_OC1Init(TIM3, &oc);
    TIM_OC1PreloadConfig(TIM3, TIM_OCPreload_Disable);
    TIM_ARRPreloadConfig(TIM3, DISABLE);

    TIM_DMACmd(TIM3, TIM_DMA_CC1, ENABLE);
    TIM_DMACmd(TIM3, TIM_DMA_Update, ENABLE);
}

void Simu_SSI_Perips_Init(){
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    DMA_InitTypeDef  DMA_InitStructure  = {0};

    RCC_PB2PeriphClockCmd(RCC_PB2Periph_GPIOB, ENABLE);
    RCC_HBPeriphClockCmd(RCC_HBPeriph_DMA1, ENABLE);

    GPIO_InitStructure.GPIO_Pin   = SSI_CLK_PIN;   // CLK
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_30MHz;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin   = SSI_DIO_PIN;   // DIO
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_IPU;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_30MHz;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin   = SSI_CSN_PIN;   // CSN
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_30MHz;
    GPIO_Init(GPIOB, &GPIO_InitStructure);
    GPIO_SetBits(GPIOB, SSI_CSN_PIN);

    DMA_DeInit(SSI_CLK_DMA_CHANNEL);
    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&GPIOB->BSHR;
    // DMA_InitStructure.DMA_MemoryBaseAddr     = (uint32_t)clk_gpio;
    DMA_InitStructure.DMA_DIR                = DMA_DIR_PeripheralDST;
    DMA_InitStructure.DMA_BufferSize         = SSI_EDGES;
    DMA_InitStructure.DMA_PeripheralInc      = DMA_PeripheralInc_Disable;
    DMA_InitStructure.DMA_MemoryInc          = DMA_MemoryInc_Enable;
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Word;
    DMA_InitStructure.DMA_MemoryDataSize     = DMA_MemoryDataSize_Word;
    DMA_InitStructure.DMA_Mode               = DMA_Mode_Normal;
    DMA_InitStructure.DMA_Priority           = DMA_Priority_VeryHigh;
    DMA_InitStructure.DMA_M2M                = DMA_M2M_Disable;
    DMA_Init(SSI_CLK_DMA_CHANNEL, &DMA_InitStructure);

    DMA_DeInit(SSI_DIO_DMA_CHANNEL);
    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&GPIOB->INDR;
    // DMA_InitStructure.DMA_MemoryBaseAddr     = (uint32_t)ssi_raw;
    DMA_InitStructure.DMA_DIR                = DMA_DIR_PeripheralSRC;
    DMA_InitStructure.DMA_BufferSize         = SSI_BITS;
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
    DMA_InitStructure.DMA_MemoryDataSize     = DMA_MemoryDataSize_HalfWord;
    DMA_Init(SSI_DIO_DMA_CHANNEL, &DMA_InitStructure);

    // set the rx tc interrupt so that we can process the data after the transfer is complete
    DMA_ITConfig(SSI_DIO_DMA_CHANNEL, DMA_IT_TC, ENABLE);
    NVIC_SetPriority(SSI_DIO_DMA_IRQn, 0 << 6);
    NVIC_EnableIRQ(SSI_DIO_DMA_IRQn);
}

void OPA_Init(void)
{
    OPA_ISP_InitTypeDef OPA_ISP_InitStruct = {0};
    GPIO_InitTypeDef     GPIO_InitStructure = {0};

    RCC_PB2PeriphClockCmd(RCC_PB2Periph_GPIOA, ENABLE);
    RCC_HBPeriphClockCmd(RCC_HBPeriph_OPCM, ENABLE);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10 | GPIO_Pin_11;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_8 | GPIO_Pin_9;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    OPA_ISP_InitStruct.OPA_ISP_GAIN = OPA_ISP_GAIN_8;
    OPA_ISP_InitStruct.OPA_ISP_VFBIAS = OPA_ISP_VFBIAS_1_6V;
    OPA_ISP_InitStruct.OPA_ISP_NESL = OPA_ISP_NESL_ISN;
    OPA_ISP_InitStruct.OPA_ISP_SEL_IO = OPA_ISP_SEL_TO_ADC_ON;
    OPA_ISP_InitStruct.OPA_ISP_QDET = OPA_ISP_QDET_EN_OFF;
    OPA_ISP_InitStruct.OPA_ISP_QDET_PD30K = OPA_ISP_QDET_PD30K_OFF;
    OPA_ISP_InitStruct.OPA_ISP_QDET_VBSEL = OPA_ISP_QDET_VBSEL_OFF;
    OPA_ISP_Init(OPA3_ISP1,&OPA_ISP_InitStruct);
    OPA_ISP_Init(OPA4_ISP2,&OPA_ISP_InitStruct);

    OPA_ISP_Cmd(OPA3_ISP1,ENABLE);
    OPA_ISP_Cmd(OPA4_ISP2,ENABLE);
}

void ADC1_Init(){
    ADC_InitTypeDef  ADC_InitStructure = {0};
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    RCC_PB2PeriphClockCmd(RCC_PB2Periph_GPIOA, ENABLE);
    RCC_PB2PeriphClockCmd(RCC_PB2Periph_ADC1, ENABLE);

    RCC_ADCCLKConfig(FOC_ADC_ADCPRE_SEL);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    ADC_DeInit(ADC1);
    ADC_InitStructure.ADC_Mode = ADC_Mode_Independent;
    ADC_InitStructure.ADC_ScanConvMode = ENABLE;
    ADC_InitStructure.ADC_ContinuousConvMode = ENABLE;
    ADC_InitStructure.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None;
    ADC_InitStructure.ADC_DataAlign = ADC_DataAlign_Right;
    ADC_InitStructure.ADC_NbrOfChannel = 1;
    ADC_Init(ADC1, &ADC_InitStructure);

    // This synchonize sampling of injected channels with the 10kHz PWM.
    ADC_ExternalTrigInjectedConvConfig(ADC1,ADC_ExternalTrigInjecConv_T1_CC5);

    ADC_InjectedSequencerLengthConfig(ADC1, FOC_ADC_JL);
    ADC_InjectedChannelConfig(ADC1, ADC_Channel_9,  1, FOC_ADC_SMPL_SEL);
    ADC_InjectedChannelConfig(ADC1, ADC_Channel_10, 2, FOC_ADC_SMPL_SEL);

    ADC_ExternalTrigInjectedConvCmd(ADC1, ENABLE);

    ADC_RegularChannelConfig(ADC1, ADC_Channel_15, 1, ADC_SampleTime_23Cycles5); //PA2

    ADC_Cmd(ADC1, ENABLE);
    ADC_MovingAverageCmd(ADC1, ENABLE);
    ADC_MovingAverageWindowSizeConfig(ADC1, ADC_MovingWindow_Size_32);

    ADC_ResetCalibration(ADC1);
    while(ADC_GetResetCalibrationStatus(ADC1));
    ADC_StartCalibration(ADC1);
    while(ADC_GetCalibrationStatus(ADC1));

    ADC_SoftwareStartConvCmd(ADC1, ENABLE);

    NVIC_SetPriority(ADC_IRQn, 0<<6);
    NVIC_EnableIRQ(ADC_IRQn);

    // Here the 10kHz interrupt is already on, making the sequence of initialization fragile, but it has to be this way for the current loop to work
    ADC_ITConfig(ADC1, ADC_IT_JEOC, ENABLE);
}