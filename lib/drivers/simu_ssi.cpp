#include "simu_ssi.h"

// ==================== Init ====================
template<uint16_t BITS>
void SimuSSI<BITS>::init()
{
    // See GPIO_BSHR_xx
    const uint32_t CLK_GPIO_BSHR_SET   = cfg_.clk_pin;
    const uint32_t CLK_GPIO_BSHR_RESET = cfg_.clk_pin << 16;
    dio_gpio_pos_ = __builtin_ctz(cfg_.dio_pin); 
    //fill the clk sequence table
    for(int i = 0; i < (BITS); ++i){
        clk_gpio[i * 2]     = CLK_GPIO_BSHR_SET;
        clk_gpio[i * 2 + 1] = CLK_GPIO_BSHR_RESET;
    }
}

// ==================== Start ====================
template<uint16_t BITS>
void SimuSSI<BITS>::start()
{
    if(ssi_busy) return;
    ssi_busy = true;

    TIM_Cmd(cfg_.tim, DISABLE);

    GPIO_ResetBits(cfg_.csn_pin_port, cfg_.csn_pin);

    DMA_Cmd(cfg_.clk_dma, DISABLE);
    DMA_Cmd(cfg_.dio_dma, DISABLE);

    DMA_SetCurrDataCounter(cfg_.clk_dma, BITS * 2);
    DMA_SetCurrDataCounter(cfg_.dio_dma, BITS * 2 + 8);
    cfg_.clk_dma->MADDR = (uint32_t)clk_gpio;
    cfg_.dio_dma->MADDR = (uint32_t)ssi_raw;
    DMA_ClearFlag(cfg_.clk_dma_tc_flag | cfg_.dio_dma_tc_flag);

    DMA_Cmd(cfg_.clk_dma, ENABLE);
    DMA_Cmd(cfg_.dio_dma, ENABLE);

    TIM_SetCounter(cfg_.tim, 0);
    TIM_Cmd(cfg_.tim, ENABLE);
}

template<uint16_t BITS>
void SimuSSI<BITS>::abort(){
    TIM_Cmd(cfg_.tim, DISABLE);
    GPIO_SetBits(cfg_.csn_pin_port, cfg_.csn_pin);
    DMA_Cmd(cfg_.clk_dma, DISABLE);
    DMA_Cmd(cfg_.dio_dma, DISABLE);
    ssi_busy = false;
}

// ==================== DMA IRQ ====================
template<uint16_t BITS>
void SimuSSI<BITS>::irqHandler()
{
    DMA_ClearFlag(cfg_.clk_dma_tc_flag | cfg_.dio_dma_tc_flag);
    TIM_Cmd(cfg_.tim, DISABLE);
    GPIO_SetBits(cfg_.csn_pin_port, cfg_.csn_pin);

    uint32_t raw = 0;
    for(int i = 0; i < BITS; ++i){
        raw <<= 1;
        raw |= (ssi_raw[i * 2] >> dio_gpio_pos_) & 1u;
    }
    ssi_raw_data = raw;
    ssi_busy = false;
}

template class SimuSSI<24>;