#pragma once

#include "sys_interface.h"
#include "iqmath.h"
#include "foc_driver.h"

struct alignas(4) motor_configs{
    FocLoopMode loop_mode;

    float current_loop_bw;
    float speed_loop_bw;
    float speed_loop_zeta;
    float pos_loop_bw;
    float eso_bw;
    float abfilt_bw;
    float abfilt_zeta;
    float current_filt_cutoff;
    float speed_ff_filt_cutoff;
    float disturb_filt_cutoff;

    float speed_loop_limit;
    float pos_loop_limit;
    q24_t zero_ang_calib;
};

struct alignas(4) flash_data{
    uint32_t version;
    uint8_t addr;
    uint32_t baudrate;
    motor_configs cfg;
    uint8_t done;
};

#define FLASH_DATA_VERSION 0x1u
#define FLASH_DATA_DONE_SIGN 0xA5u

const motor_configs motor_cfg_default = {
    .loop_mode = FocLoopMode::Current,
    .current_loop_bw = 400.0f,
    .speed_loop_bw = 20.0f,
    .speed_loop_zeta = 1.0f,
    .pos_loop_bw = 2.0f,
    .eso_bw = 200.0f,
    .abfilt_bw = 80.0f,
    .abfilt_zeta = 0.9f,
    .current_filt_cutoff = 2000.0f,
    .speed_ff_filt_cutoff = 500.0f,
    .disturb_filt_cutoff = 300.0f,
    .speed_loop_limit = 1.80f,
    .pos_loop_limit = 60.0f,
    .zero_ang_calib = Q24_ZERO,
};

const flash_data flash_data_default = {
    .version = FLASH_DATA_VERSION,
    .addr = 0x00,
    .baudrate = 115200,
    .cfg = motor_cfg_default,
    .done = FLASH_DATA_DONE_SIGN
};

enum MotorErrors {
    ERR_OVER_VOLTAGE = 1u << 1,
    ERR_UNDER_VOLTAGE = 1u << 2,
    ERR_OVER_CURRENT = 1u << 3,
    ERR_READ_FLASH = 1u << 4,
    ERR_READ_ANG = 1u << 5,
};

