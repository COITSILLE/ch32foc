#include "protocol.h"
#include <cstdint>

bool DeviceProtocol::processCmdFrame(uint8_t rx_byte){
    switch(state){
        case (IDLE):
            data_index = 0;
            last_cmd_frame_.cmd = rx_byte;
            if(last_cmd_frame_.cmd > (uint8_t)Cmd::NOP && last_cmd_frame_.cmd <= (uint8_t)Cmd::GET_ERROR){
                state = AWAITS_SEND;
            }
            else{
                state = DATA;
            }
            break;
        case (DATA):
            data_index++;
            data_buf[data_index] = rx_byte;
            if(data_index == 4){
                state = CHECKSUM;
            }
            break;
        case (CHECKSUM):
            // Process the checksum
            break;
        case (AWAITS_SEND):
        default:
            break;
    }
}

bool checksum(){
    
}