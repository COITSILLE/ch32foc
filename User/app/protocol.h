#pragma once

#include "sys_interface.h"
#include "uart_halfduplex.h"
#include "sstrtotype.h"

struct long_cmd_frame{
    uint8_t addr;
    uint8_t tag_n_cmd;
    uint8_t data[4];
    uint8_t checksum;
};
struct short_cmd_frame{
    uint8_t addr;
    uint8_t cmd;
    uint8_t checksum;
};

enum class DataFrameType : uint8_t {
    REQUIRE_RESEND = 0,
    SHORT,
    LONG,
};

struct data_frame{
    uint8_t type;
    uint8_t* data;
    uint8_t checksum;
};

enum class Cmd : uint8_t {
    NOP = 0x00,
    GET_FEEDBACK,
    GET_CURRENT,
    GET_SPEED,
    GET_POSITION,
    GET_VBUS,
    GET_MODE,
    GET_ERROR,

    SET_MODE,
    SET_CURRENT,
    SET_SPEED,
    SET_POSITION,

    SET_CURRENT_BW,
    SET_SPEED_BW,
    SET_SPEED_ZETA,
    SET_POS_BW,
    SET_ESO_BW,
    SET_ABFILT_BW,
    SET_ABFILT_ZETA,
};

enum class CmdDebug : uint8_t {
    CALIB_ZERO_POS = 0x13,
    
    GET_ADDR,
    GET_DVC_INFO,

    SET_ADDR,
    SET_BAUDRATE,
    SET_DEFAULT_CONFIG,

    SAVE_CONFIG,
};

constexpr uint8_t GENERIC_DEVICE_ADDR = 0x00;

void do_crc(uint8_t* crc, uint8_t new_data);

using cmd_rcv_callback_t = void(*)(bool is_valid, uint8_t tag, uint8_t cmd, uint32_t data);

class DeviceProtocolRx{
public:
    DeviceProtocolRx(cmd_rcv_callback_t callback) : cmd_callback(callback) {}

    void setAddr(uint8_t addr) { device_addr_ = addr; }

    void processCmdFrame(uint8_t rx_byte);

    void cpltSend() { if (state == AWAITS_SEND) state = IDLE; }
    void rcvIdle() { if (state > IDLE) state = IDLE; } // reset the state machine
private:
    bool is_addr_matched = false;
    enum {
        AWAITS_SEND = 0,
        IDLE,
        CMD,
        DATA,
        CHECKSUM,
    } state = IDLE;

    uint8_t device_addr_ = 0x00;

    cmd_rcv_callback_t cmd_callback = nullptr;

    uint8_t data_index = 0;
    uint8_t data_buf[4] = {0};
    long_cmd_frame last_cmd_frame_ = {0};

    uint32_t timeout_stamp = 0;
};

class DeviceProtocolTx{
public:
    void setTag(uint8_t tag);
    void setType(DataFrameType type);
    void setData(const uint8_t* data, int len);
    int getTxFrame(uint8_t* buf);
private:
    uint8_t tx_buf_[16] = {0};
    int tx_len_ = 0;
};