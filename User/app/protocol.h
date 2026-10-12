#pragma once

#include "sys_interface.h"
#include "uart_halfduplex.h"
#include "sstrtotype.h"

struct cmd_frame{
    uint8_t cmd;
    uint8_t data[4];
    uint8_t checksum;
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
    GET_CURRENT_BW,
    GET_SPEED_BW,
    GET_POS_BW,
    GET_ESO_BW,
    GET_ABFILT_BW,
    GET_ERROR,

    SET_MODE,
    SET_CURRENT,
    SET_SPEED,
    SET_POSITION,

    SET_CURRENT_BW,
    SET_SPEED_BW,
    SET_POS_BW,
    SET_ESO_BW,
    SET_ABFILT_BW,
};

enum class CmdDebug : uint8_t {
    NOP = 0x00,
    CALIB_CURRENT = 0x00,
    CALIB_ZERO_POS,

};

void unpack_data_frame(uint8_t *buf, struct data_frame *frame){

}
void pack_cmd_frame(uint8_t *buf, struct cmd_frame *frame){

}

class DeviceProtocol{
public:
    void pack_cmd_frame(uint8_t *buf, struct cmd_frame *frame);

    bool processCmdFrame(uint8_t rx_byte);

    void cpltSend() { if (state == AWAITS_SEND) state = IDLE; }
private:
    bool checksum();
    bool is_last_frame_valid;
    enum {
        AWAITS_SEND = 0,
        IDLE,
        CMD,
        DATA,
        CHECKSUM,
    } state;

    uint8_t data_index;
    uint8_t data_buf[4];
    cmd_frame last_cmd_frame_;
};