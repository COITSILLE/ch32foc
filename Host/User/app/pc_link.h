#pragma once

#include "sys_interface.h"
#include "usart.h"

#ifdef __cplusplus

static constexpr uint16_t PC_RX_CAPACITY = 128;
static constexpr uint8_t PC_LINE_CAPACITY = 96;

/* command line (CR/LF stripped), delivered from the main loop context */
using pc_line_callback_t = void(*)(char* line);

class PcLink{
public:
    struct Config{
        UART_HandleTypeDef* huart = &huart2;
        uint32_t tx_timeout_ms = 20;
    };

    PcLink(const Config& config, pc_line_callback_t callback) : cfg_(config), line_callback_(callback) {}

    void init();
    void poll();

    void send(const char* begin, const char* end);
    void sendText(const char* text);

    /* ISR side */
    void onRxComplete();
    void onError();

private:
    void push(uint8_t byte);
    bool pop(uint8_t* byte);

    Config cfg_;
    pc_line_callback_t line_callback_ = nullptr;

    uint8_t rx_byte_ = 0;
    uint8_t rx_buf_[PC_RX_CAPACITY] = {0};
    uint16_t rx_head_ = 0;
    uint16_t rx_tail_ = 0;

    char line_[PC_LINE_CAPACITY] = {0};
    uint8_t line_len_ = 0;
};

#endif
