#pragma once

#include "sys_interface.h"
#include "uart_halfduplex.h"
#include "sstrtotype.h"

struct cmd_frame{
    uint8_t cmd;
    uint8_t data[4];
    
};

struct data_frame{
    uint8_t data[4];
};

void unpack_data_frame(uint8_t *buf, struct data_frame *frame){

}
void pack_cmd_frame(uint8_t *buf, struct cmd_frame *frame){

}

class DeviceProtocol{
public:
    void unpack_data_frame(uint8_t *buf, struct data_frame *frame);
    void pack_cmd_frame(uint8_t *buf, struct cmd_frame *frame);
private:
    bool checksum();
    bool is_last_frame_valid;
};