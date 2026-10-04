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

#endif   // __cplusplus