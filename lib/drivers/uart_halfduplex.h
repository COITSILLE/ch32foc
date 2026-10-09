#pragma once

#include "sys_interface.h"
#include "ring_buffer.h"
#include <cstdint>

#ifdef __cplusplus

struct uart_halfduplex_callback_funcs{
    void(*tx_cplt)(void) = nullptr;
    void(*rx_cplt)(uint16_t size) = nullptr;
};

class UART_HalfDuplex {
public:
    UART_HalfDuplex(USART_TypeDef* usart, uint8_t* rx_buffer, uint16_t rx_buffer_size);
    void init(uart_halfduplex_callback_funcs cbs = {nullptr, nullptr});
    bool transmit_async(uint8_t* data, uint16_t size);
    bool receive_variable();

    void irqHandler();

    RingBuffer<uint8_t> rx_rb;
private:
    USART_TypeDef* usart_;
    volatile enum { TRANSMITTING = 0, RECEIVING, IDLE } state = IDLE;
    volatile uint16_t pending_bytes = 0;
    volatile uint16_t rx_size = 0;

    volatile uint8_t* tx_buffer = nullptr;

    uart_halfduplex_callback_funcs cbs_;

    inline void clearIdle(){
        (void)usart_->STATR;
        (void)USART_ReceiveData(usart_);
    }
};

#endif

