#pragma once

#include "sys_interface.h"
#include <stdint.h>

#ifdef __cplusplus

template<uint16_t BITS>
class SimuSSI
{
public:
    struct Config
    {
        TIM_TypeDef         *tim;           
        uint16_t             clk_pin;        
        uint16_t             dio_pin;        
        uint16_t             csn_pin;        
        GPIO_TypeDef        *csn_pin_port;
        DMA_Channel_TypeDef *clk_dma;
        DMA_Channel_TypeDef *dio_dma;
        IRQn_Type            clk_dma_irqn;
        IRQn_Type            dio_dma_irqn;
        uint32_t             clk_dma_tc_flag;
        uint32_t             dio_dma_tc_flag;
    };
    explicit SimuSSI(const Config &cfg) : cfg_(cfg) {}

    /**
     * @brief Fill the clk_gpio and ssi_raw arrays with the next frame of data.
     * @note  This function must be called before the initialization of peripherals.
    */
    void init();

    void start();
    void abort();

    void irqHandler();

    uint32_t clk_gpio[BITS * 2] = {};
    uint16_t ssi_raw[BITS * 2 + 8]   = {};
    volatile bool ssi_busy       = false;
    volatile uint32_t ssi_raw_data        = 0;

    const Config &config() const { return cfg_; }

private:
    Config cfg_;
    uint32_t dio_gpio_pos_ = 0;
};

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

#endif   // __cplusplus