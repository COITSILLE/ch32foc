#include "pc_link.h"

#include <cstring>

void PcLink::init(){
    HAL_UART_Receive_IT(cfg_.huart, &rx_byte_, 1);
}

void PcLink::onRxComplete(){
    push(rx_byte_);
    HAL_UART_Receive_IT(cfg_.huart, &rx_byte_, 1);
}

void PcLink::onError(){
    __HAL_UART_CLEAR_OREFLAG(cfg_.huart);
    HAL_UART_Receive_IT(cfg_.huart, &rx_byte_, 1);
}

void PcLink::send(const char* begin, const char* end){
    static char tx_frame[PC_LINE_CAPACITY + 2];
    const uint16_t len = (uint16_t)(end - begin);
    memcpy(tx_frame, begin, len);
    tx_frame[len] = '\r';
    tx_frame[len + 1u] = '\n';
    HAL_UART_Transmit(cfg_.huart, (const uint8_t*)tx_frame, (uint16_t)(len + 2u), cfg_.tx_timeout_ms);
}

void PcLink::sendText(const char* text){
    send(text, text + strlen(text));
}

void PcLink::push(uint8_t byte){
    const uint16_t next = (uint16_t)((rx_head_ + 1u) % PC_RX_CAPACITY);
    if (next != rx_tail_){
        rx_buf_[rx_head_] = byte;
        rx_head_ = next;
    }
}

bool PcLink::pop(uint8_t* byte){
    if (rx_tail_ == rx_head_){
        return false;
    }
    *byte = rx_buf_[rx_tail_];
    rx_tail_ = (uint16_t)((rx_tail_ + 1u) % PC_RX_CAPACITY);
    return true;
}

void PcLink::poll(){
    uint8_t byte = 0;
    while (pop(&byte)){
        if (byte == '\n'){
            line_[line_len_] = '\0';
            if (line_len_ > 0u && line_callback_ != nullptr){
                line_callback_(line_);
            }
            line_len_ = 0;
        }
        else if (byte == '\r'){
            continue;
        }
        else if (line_len_ >= PC_LINE_CAPACITY - 1u){
            line_len_ = 0;
            sendText("ERR LINE");
        }
        else{
            line_[line_len_++] = (char)byte;
        }
    }
}
