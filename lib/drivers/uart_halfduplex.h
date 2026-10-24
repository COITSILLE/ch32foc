#pragma once

#include "ch32m030.h"
#include <cstdint>
#include <assert.h>

#ifdef __cplusplus



class UART_HalfDuplex {
public:
    struct Config;
    struct CallbackFuncs;

    UART_HalfDuplex(const Config& config, CallbackFuncs cbs = {nullptr, nullptr});
    void init();
    bool transmit_async(uint8_t* data, uint16_t size);
    bool receive_variable_on();
    void abort_receive();

    void irqHandler();

    bool readByte(uint8_t &byte);

    struct Config {
        USART_TypeDef* usart;
        uint8_t* rx_buffer;
        uint16_t rx_buffer_size;

        DMA_Channel_TypeDef *tx_dma_channel;
        DMA_Channel_TypeDef *rx_dma_channel;
        uint32_t tx_dma_tc_flag;
    };
    struct CallbackFuncs{
        void(*tx_cplt)(void) = nullptr;
        void(*rx_cplt)(void) = nullptr;
    };
private:
    Config cfg_;

    volatile enum { TRANSMITTING = 0, RECEIVING} state = RECEIVING;

    CallbackFuncs cbs_;

    volatile uint32_t rx_idx = 0;

    inline void clearIdle(){
        (void)cfg_.usart->STATR;
        (void)USART_ReceiveData(cfg_.usart);
    }
};

#endif

