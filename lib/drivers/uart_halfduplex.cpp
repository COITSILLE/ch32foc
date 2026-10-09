#include "uart_halfduplex.h"
#include <assert.h>

UART_HalfDuplex::UART_HalfDuplex(USART_TypeDef* usart, uint8_t* rx_buffer, uint16_t rx_buffer_size) 
    : usart_(usart)
    , rx_rb(rx_buffer, rx_buffer_size) {
    assert(usart_ != nullptr);
    assert(rx_buffer != nullptr);
}

void UART_HalfDuplex::init(uart_halfduplex_callback_funcs cbs){
    assert(usart_ != nullptr);
    assert(usart_->CTLR3 & UART_CTLR3_HDSEL); // Ensure half-duplex mode is enabled
    assert(!(usart_->CTLR1 & (UART_CTLR1_TCIE | UART_CTLR1_RXNEIE | UART_CTLR1_IDLEIE))); // Ensure interrupts are disabled. They are managed by this class
    usart_->CTLR1 &= ~(UART_CTLR1_TE | UART_CTLR1_RE); // disable both

    cbs_ = cbs;
}

void UART_HalfDuplex::irqHandler(){
    if (state == TRANSMITTING && (USART_GetFlagStatus(usart_, USART_FLAG_TC)) == SET){
        if (pending_bytes > 0){
            tx_buffer++;
            uint8_t byte = *tx_buffer;
            USART_SendData(usart_, byte);
            pending_bytes--;
        }
        else{
            state = IDLE;
            usart_->CTLR1 &= ~UART_CTLR1_TE; // disable transmitter
            USART_ITConfig(usart_, USART_IT_TC, DISABLE);
            if (cbs_.tx_cplt) cbs_.tx_cplt();
        }
    }
    else if (state == RECEIVING){
        if (USART_GetFlagStatus(usart_, USART_FLAG_RXNE) == SET){
            uint8_t byte_rcv = USART_ReceiveData(usart_);
            rx_rb.push(byte_rcv);
            rx_size++;
        }
        if (USART_GetFlagStatus(usart_, USART_FLAG_IDLE) == SET){
            state = IDLE;
            usart_->CTLR1 &= ~UART_CTLR1_RE; // disable receiver

            USART_ITConfig(usart_, USART_IT_RXNE, DISABLE);
            clearIdle();
            if (cbs_.rx_cplt) cbs_.rx_cplt(rx_size);
            rx_size = 0;
        }
    }
    return;
}

bool UART_HalfDuplex::transmit_async(uint8_t* data, uint16_t size){
    if (state != IDLE) return false; // ignore if not idle
    if (size == 0) return false; // ignore if no data
    pending_bytes = size - 1;
    state = TRANSMITTING;
    tx_buffer = data;

    USART_ClearFlag(usart_, USART_FLAG_TC);

    usart_->CTLR1 &= ~UART_CTLR1_RE;
    usart_->CTLR1 |= UART_CTLR1_TE;

    uint8_t byte1 = *data;
    USART_SendData(usart_, byte1);

    USART_ITConfig(usart_, USART_IT_RXNE, DISABLE);
    clearIdle();
    USART_ITConfig(usart_, USART_IT_TC, ENABLE);

    return true;
}

bool UART_HalfDuplex::receive_variable(){
    if (state != IDLE) return false; // ignore if not idle
    state = RECEIVING;

    USART_ClearFlag(usart_, USART_FLAG_RXNE);
    clearIdle();
    
    usart_->CTLR1 &= ~UART_CTLR1_TE;
    usart_->CTLR1 |= UART_CTLR1_RE;

    rx_size = 0;

    USART_ITConfig(usart_, USART_IT_TC, DISABLE);
    USART_ITConfig(usart_, USART_IT_RXNE, ENABLE);
    USART_ITConfig(usart_, USART_IT_IDLE, ENABLE);
    
    return true;
}