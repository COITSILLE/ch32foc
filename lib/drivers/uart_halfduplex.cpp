#include "uart_halfduplex.h"
#include <assert.h>
#include <cstdint>

UART_HalfDuplex::UART_HalfDuplex(const Config& config, CallbackFuncs cbs)
    : cfg_(config), cbs_(cbs) {
    assert(cfg_.usart != nullptr);
    assert(cfg_.rx_buffer != nullptr); 
}

void UART_HalfDuplex::init(){
    assert(cfg_.usart != nullptr);
    assert(cfg_.usart->CTLR1 & UART_CTLR1_UE);    // USART must be enabled, otherwise no DMA request
    assert(cfg_.usart->CTLR3 & UART_CTLR3_HDSEL); // Ensure half-duplex mode is enabled
    assert(!(cfg_.usart->CTLR1 & (UART_CTLR1_TCIE | UART_CTLR1_IDLEIE))); // Ensure interrupts are disabled. They are managed by this class
    cfg_.usart->CTLR1 &= ~(UART_CTLR1_TE | UART_CTLR1_RE); // disable both
    USART_ClearFlag(cfg_.usart, USART_FLAG_TC);
    clearIdle();
}

void UART_HalfDuplex::irqHandler(){
    if (state == TRANSMITTING && (USART_GetFlagStatus(cfg_.usart, USART_FLAG_TC)) == SET){
        if(cfg_.tx_dma_channel->CNTR != 0){
            USART_ClearFlag(cfg_.usart, USART_FLAG_TC);
            return;
        }
        cfg_.usart->CTLR1 &= ~UART_CTLR1_TE;
        USART_ITConfig(cfg_.usart, USART_IT_TC, DISABLE);
        USART_ClearFlag(cfg_.usart, USART_FLAG_TC);
        USART_DMACmd(cfg_.usart, USART_DMAReq_Tx, DISABLE);
        DMA_Cmd(cfg_.tx_dma_channel, DISABLE);

        state = RECEIVING;

        if (cbs_.tx_cplt) cbs_.tx_cplt();
    }
    else if (state == RECEIVING){
        if (USART_GetFlagStatus(cfg_.usart, USART_FLAG_IDLE) == SET){

            clearIdle();

            if (cbs_.rx_cplt) cbs_.rx_cplt();
        }
    }
    return;
}

bool UART_HalfDuplex::transmit_async(uint8_t* data, uint16_t size){
    if (state != RECEIVING) return false;
    if (size == 0) return false; // ignore if no data

    USART_ITConfig(cfg_.usart, USART_IT_IDLE, DISABLE);

    
    USART_DMACmd(cfg_.usart, USART_DMAReq_Rx, DISABLE);
    DMA_Cmd(cfg_.rx_dma_channel, DISABLE);
    cfg_.usart->CTLR1 &= ~UART_CTLR1_RE;
    clearIdle();
    
    DMA_Cmd(cfg_.tx_dma_channel, DISABLE);
    cfg_.tx_dma_channel->CNTR  = size;
    cfg_.tx_dma_channel->MADDR = (uint32_t)data;
    DMA_ClearFlag(cfg_.tx_dma_tc_flag);

    USART_ClearFlag(cfg_.usart, USART_FLAG_TC);
    USART_ITConfig(cfg_.usart, USART_IT_TC, ENABLE);

    USART_DMACmd(cfg_.usart, USART_DMAReq_Tx, ENABLE);
    DMA_Cmd(cfg_.tx_dma_channel, ENABLE);
    
    cfg_.usart->CTLR1 |= UART_CTLR1_TE;
    
    state = TRANSMITTING;
    return true;
}

bool UART_HalfDuplex::receive_variable_on(){
    clearIdle();
    
    cfg_.usart->CTLR1 &= ~UART_CTLR1_TE;

    USART_DMACmd(cfg_.usart, USART_DMAReq_Tx, DISABLE);
    DMA_Cmd(cfg_.tx_dma_channel, DISABLE);


    DMA_Cmd(cfg_.rx_dma_channel, DISABLE);
    cfg_.rx_dma_channel->CNTR  = cfg_.rx_buffer_size;
    cfg_.rx_dma_channel->MADDR = (uint32_t)cfg_.rx_buffer;
    // DMA_ClearFlag(cfg_.rx_dma_tc_flag);
    USART_DMACmd(cfg_.usart, USART_DMAReq_Rx, ENABLE);
    DMA_Cmd(cfg_.rx_dma_channel, ENABLE);
    cfg_.usart->CTLR1 |= UART_CTLR1_RE;

    USART_ITConfig(cfg_.usart, USART_IT_TC, DISABLE);
    USART_ITConfig(cfg_.usart, USART_IT_IDLE, ENABLE);
    
    rx_idx = 0;

    return true;
}

void UART_HalfDuplex::abort_receive(){
    if (state != RECEIVING) return;

    USART_ITConfig(cfg_.usart, USART_IT_IDLE, DISABLE);
    clearIdle();
    USART_ClearFlag(cfg_.usart, USART_FLAG_TC);

    cfg_.usart->CTLR1 &= ~UART_CTLR1_RE;
    DMA_Cmd(cfg_.rx_dma_channel, DISABLE);
    USART_DMACmd(cfg_.usart, USART_DMAReq_Rx, DISABLE);
}

bool UART_HalfDuplex::readByte(uint8_t &byte){
    uint32_t r = cfg_.rx_dma_channel->CNTR;
    r = (cfg_.rx_buffer_size - r) % cfg_.rx_buffer_size;

    if (r == rx_idx) return false; // no new data

    byte = cfg_.rx_buffer[rx_idx];
    rx_idx = (rx_idx + 1) % cfg_.rx_buffer_size;
    return true;
}