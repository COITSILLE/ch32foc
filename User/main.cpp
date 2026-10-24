/********************************** (C) COPYRIGHT *******************************
 * File Name          : main.c
 * Author             : WCH
 * Version            : V1.0.0
 * Date               : 2024/09/01
 * Description        : Main program body.
 *********************************************************************************
 * Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
 * Attention: This software (modified or not) and binary are used for 
 * microcontroller manufactured by Nanjing Qinheng Microelectronics.
 *******************************************************************************/

/* ========= Includes ========= */
/*Include C headers*/
#include "debug.h"
#include "delay.h"
#include "peripheral.h"
#include "smath.h"
#include "three_phase_inverter.h"
#include "systick_time.h"
#include <cstring>
/*Include Cxx headers*/
#include "mt6701_ssi.h"
#include "iqmath.h"
#include "foc_driver.h"
#include "flash_save_data.h"
#include "protocol.h"
#include "uart_halfduplex.h"

/* ========= Function Prototypes ========= */
static void task_protocol(void);
void uart_tx_cplt_callback(void);
void cmd_execute(bool is_valid, uint8_t tag, uint8_t cmd, uint32_t data);
extern "C" void updateEncoder(void);
static void task_vbus(void);
static void task_state(void);
extern "C" void task_outer_loop(void);
static void task_led(void);
static bool load_config_from_flash(FlashData* fdata);
static void apply_motor_config(MotorConfigs* cfg);
static void save_config_to_flash(FlashData* fdata);

/* ========= Global Variables ========= */
/* Global Variable */
enum class SysStates : uint8_t {
    INIT = 0x00,
    RUN,
    FAULT,

    CALIB_POS,
};
union SysReg {
    uint32_t all;
    struct {
        union {
            uint8_t warning : 8;
            struct {
                uint8_t reserved : 4;
                uint8_t failed_read_flash : 1;
                uint8_t failed_init_encoder : 1;
                uint8_t voltage_fell : 1;
                uint8_t voltage_back_charged : 1;
            };
        } warning;
        union {
            uint8_t fault : 8;
            struct {
                uint8_t reserved : 5;
                uint8_t over_current : 1;
                uint8_t over_voltage : 1;
                uint8_t under_voltage : 1;
            };
        } fault;
        SysStates state : 8;
        uint8_t mcu_flags : 8;
    };
} sys_reg = {0};
bool calib_pos_requested = false;
// bit[31:24] = reserved, bit[23:16] = major, bit[15:8] = minor, bit[7:0] = batch
constexpr uint32_t FIRMWARE_VERSION = 0x00000001u; 

FlashData flash_save_data;
enum BootMode { 
    NORMAL_,
    DEBUG_,
} boot_mode = NORMAL_;

enum class UiStates : uint8_t {
    IDLE = 0,
    INPUT,
    CALIB_POS,
    SAVE_CONFIG,
} ui_state = UiStates::IDLE;

enum class LedMaster : uint8_t {
    COMM = 0,
    ADDR,
} led_master = LedMaster::COMM;

enum class LedAddrStates : uint8_t {
    OFF = 0,
    ON,
    GAP,
} led_addr_state = LedAddrStates::OFF;

SimuSSI<24>::Config ssi_cfg = {
    .tim = SIMU_SSI_TIM,
    .clk_pin = SSI_CLK_PIN,
    .dio_pin = SSI_DIO_PIN,
    .csn_pin = SSI_CSN_PIN,
    .csn_pin_port = SSI_CSN_PIN_PORT,
    .clk_dma = SSI_CLK_DMA_CHANNEL,
    .dio_dma = SSI_DIO_DMA_CHANNEL,
    .clk_dma_irqn = SSI_CLK_DMA_IRQn,
    .dio_dma_irqn = SSI_DIO_DMA_IRQn,
    .clk_dma_tc_flag = SSI_CLK_DMA_TC_FLAG,
    .dio_dma_tc_flag = SSI_DIO_DMA_TC_FLAG,
};

SimuSSI<24> g_ssi(ssi_cfg);

MT6701_SSI g_encoder(g_ssi);

FocDriver g_foc(g_encoder);

static uint8_t rx_buffer[64];
static uint16_t rx_buffer_size = sizeof(rx_buffer);

UART_HalfDuplex::Config uart_cfg = {
    .usart = USART1,
    .rx_buffer = rx_buffer,
    .rx_buffer_size = rx_buffer_size,
    .tx_dma_channel = UART_DMA_TX_CHANNEL,
    .rx_dma_channel = UART_DMA_RX_CHANNEL,
    .tx_dma_tc_flag = UART_DMA_TX_TC_FLAG,
};
UART_HalfDuplex::CallbackFuncs uart_cbs = {
    .tx_cplt = uart_tx_cplt_callback,
    .rx_cplt = nullptr,
};

UART_HalfDuplex g_uart{uart_cfg, uart_cbs};
DeviceProtocolRx g_protocol_rx(cmd_execute);
DeviceProtocolTx g_protocol_tx;
static uint8_t tx_buffer[16];

#define LED_ON do{GPIO_ResetBits(GPIOA, GPIO_Pin_LED);}while (0)
#define LED_OFF do{GPIO_SetBits(GPIOA, GPIO_Pin_LED);}while (0)

constexpr q24_t FOC_ADC_SCALE_V = q24(VBUS_FULL);
constexpr q24_t vbus_calib      = q24(-0.01f);

const uint32_t FOC_SAVE_DATA_ADDR = 0x0800FF80u;

/* ========= Main ========= */

int main(void) {
    SystemCoreClockUpdate();
    Delay_Init();
    SystickTimestamp_init();

    /* Read first; programming option bytes on every boot is unnecessary. */
    const uint16_t desired_user_ob = OB_STOP_NoRST | OB_STDBY_NoRST |
                                     OB_RST_NoEN | OB_RST_PIN_PC0 |
                                     OB_OV_CFG_NoEN | 0x0041u;
    const uint32_t current_user_ob = FLASH_GetUserOptionByte();

    if ((current_user_ob & 0x00FFu) != (uint32_t)(desired_user_ob & 0x00FFu)) {
        FLASH_Status status = FLASH_UserOptionByteConfig(
            OB_STOP_NoRST,
            OB_STDBY_NoRST,
            OB_RST_NoEN,
            OB_RST_PIN_PC0,
            OB_OV_CFG_NoEN
        );
        (void)status;
    }
    sys_reg.mcu_flags = 0;
    uint32_t r = RCC->RSTSCKR;
    sys_reg.mcu_flags =
        ((r & RCC_PORRSTF)    ? 1u << 0 : 0)
        | ((r & RCC_SFTRSTF)    ? 1u << 1 : 0)
        | ((r & RCC_WWDGRSTF)   ? 1u << 2 : 0)
        | ((r & RCC_LPWRRSTF)   ? 1u << 3 : 0)
        | ((r & RCC_ADCAWDRSTF) ? 1u << 4 : 0);
    RCC_ClearFlag();

    GPIO_Sel_Init();
    GPIO_LED_Init();
    GPIO_ResetBits(GPIOA, GPIO_Pin_LED);
    delay_ms_nortos(2000);
    GPIO_SetBits(GPIOA, GPIO_Pin_LED);

    if (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_0) == Bit_RESET) {
        boot_mode = DEBUG_;
    }
    memset(&flash_save_data, 0, sizeof(flash_save_data));
    if (!load_config_from_flash((FlashData*)&flash_save_data)) {
        sys_reg.warning.failed_read_flash = 1;
    }

    g_foc.m_calibrating = true;

    g_ssi.init();
    /* Half-duplex protocol: UART + DMA setup. The TX buffer and MADDR are reloaded by transmit_async on every frame. */
    UART_Init(flash_save_data.baudrate);

    g_uart.init();
    g_protocol_rx.setAddr(flash_save_data.addr);
    g_uart.receive_variable_on();

    TIM1_Init();
    OPA_Init();
    TIM3_Init();
    Simu_SSI_Perips_Init();
    TIM2_Init();

    const bool encoder_ok = g_encoder.init();
    if (!encoder_ok) {
        sys_reg.warning.failed_init_encoder = 1;
    }

    ADC1_Init();
    apply_motor_config((MotorConfigs*)&flash_save_data.cfg);

    while (1) {
        task_vbus();

        task_protocol();

        task_state();
        
        task_led();
    }
}

/* ======== Function Implementation ======== */
extern "C" void ssi_irqHandler() {
    g_ssi.irqHandler();
}

extern "C" void uart_irqHandler() {
    g_uart.irqHandler();
}

static inline bool ctrl_tick_div2(void) {
    static uint8_t dc = 0;
    if (++dc < 2) return false;
    dc = 0;
    return true;
}

extern "C" void updateEncoder(void) {
    g_encoder.update_async();
}

extern "C" void ADC_JEOC_irqHandler(void) {
    if (ADC_GetITStatus(ADC1, ADC_IT_JEOC) == RESET) return;

    updateEncoder();
    
    if (sys_reg.state == SysStates::RUN) {
        g_foc.updateCurrentLoop();
    }

    ADC_ClearITPendingBit(ADC1, ADC_IT_JEOC);
}

extern "C" void ADC_VBUS_irqHandler(void) {
    auto goFault = [&]() {
        sys_reg.state = SysStates::FAULT;
        inv_set_duty(0x8000, 0x8000, 0x8000);
    };

    if (ADC_GetITStatus(ADC1, ADC_IT_AWD_OV) == SET) {
        ADC_ITConfig(ADC1, ADC_IT_AWD_OV, DISABLE);
        sys_reg.fault.over_voltage = 1;
        goFault();
        ADC_ClearITPendingBit(ADC1, ADC_IT_AWD_OV);
    }
    if (ADC_GetITStatus(ADC1, ADC_IT_AWD_DN) == SET) {
        ADC_ITConfig(ADC1, ADC_IT_AWD_DN, DISABLE);
        sys_reg.fault.under_voltage = 1;
        goFault();
        ADC_ClearITPendingBit(ADC1, ADC_IT_AWD_DN);
    }
    if (ADC_GetITStatus(ADC1, ADC_IT_AVG_OV) == SET) {
        sys_reg.warning.voltage_back_charged = 1;
        ADC_ClearITPendingBit(ADC1, ADC_IT_AVG_OV);
    }
    if (ADC_GetITStatus(ADC1, ADC_IT_AVG_DN) == SET) {
        sys_reg.warning.voltage_fell = 1;
        ADC_ClearITPendingBit(ADC1, ADC_IT_AVG_DN);
    }
}

static void save_config_to_flash(FlashData* fdata) {
    __disable_irq();
    FLASH_Unlock_Fast();
    FLASH_ErasePage_Fast(FOC_SAVE_DATA_ADDR);
    while (FLASH_WaitForLastOperation(100000) != FLASH_COMPLETE) {}
    FLASH_BufReset();

    size_t size_word = sizeof(FlashData) / sizeof(uint32_t);
    uint32_t* data_word = reinterpret_cast<uint32_t*>(fdata);
    for (int i = 0; i < (size_word + 1) / 2; i++) {
        uint32_t data1 = data_word[i * 2];
        if (i * 2 + 1 < size_word) {
            uint32_t data2 = data_word[i * 2 + 1];
            FLASH_BufLoad(FOC_SAVE_DATA_ADDR + i * 8, data1, data2);
        } else {
            FLASH_BufLoad(FOC_SAVE_DATA_ADDR + i * 8, data1, 0);
        }
        while (FLASH_WaitForLastOperation(100000) != FLASH_COMPLETE) {}
    }

    FLASH_ProgramPage_Fast(FOC_SAVE_DATA_ADDR);
    while (FLASH_WaitForLastOperation(100000) != FLASH_COMPLETE) {}

    FLASH_Lock_Fast();
    FLASH_Lock();
    __enable_irq();
}

static bool load_config_from_flash(FlashData* fdata) {
    const FlashData* fdata_raw = reinterpret_cast<const FlashData*>(FOC_SAVE_DATA_ADDR);
    if (fdata_raw->version == FLASH_DATA_VERSION && fdata_raw->done == FLASH_DATA_DONE_SIGN) {
        *fdata = *fdata_raw;
        return true;
    } else {
        *fdata = flash_data_default;
        return false;
    }
}

static void apply_motor_config(MotorConfigs* cfg) {
    g_foc.setLoopMode(cfg->loop_mode);
    g_foc.setCurrentLoopBandWidth(cfg->current_loop_bw);
    g_foc.setSpeedLoopBandWidth(cfg->speed_loop_bw, cfg->speed_loop_zeta);
    g_foc.setPosLoopBandWidth(cfg->pos_loop_bw);
    g_foc.setEsoBandWidth(cfg->eso_bw);
    g_foc.setSpeedABFiltBandWidth(cfg->abfilt_bw, cfg->abfilt_zeta);
    g_foc.setCurrentFiltCutoffFreq(cfg->current_filt_cutoff);
    g_foc.setSpeedFFFiltCutoffFreq(cfg->speed_ff_filt_cutoff);
    g_foc.setDisturbFiltCutoffFreq(cfg->disturb_filt_cutoff);

    g_foc.setSpeedLoopLimit(q24(cfg->speed_loop_limit));
    g_foc.setPosLoopLimit(cfg->pos_loop_limit);
    g_foc.setAngCalib(cfg->zero_ang_calib);
    g_foc.applyContAngZeroOffset(cfg->cont_ang_zero_offset);
}

extern "C" void task_outer_loop(void) {
    if (sys_reg.state != SysStates::RUN) return;
    
    if (ctrl_tick_div2()) {
        
        g_foc.updatePosLoop();
        
    }
    
    g_foc.updateSpeedLoop();
    
}

static void task_vbus(void) {
    if (sys_reg.warning.voltage_fell || sys_reg.warning.voltage_back_charged) {
        goto update_vbus;
    }
    NON_BLOCK_DELAY(500);

update_vbus:
    const uint16_t vbus_avg = ADC_GetAverageValue(ADC1);
    // const uint16_t vbus_avg = ADC_GetConversionValue(ADC1);
    q24_t vbus = q24_t::from_raw((int32_t)vbus_avg << 12);
    vbus = q24_add_w(q24_mul_w(vbus, FOC_ADC_SCALE_V), vbus_calib);

    constexpr q24_t LIMIT_SCALE = q24(0.5f / M_SQRT2);
    constexpr q24_t VD_SCALE = q24(1.273f);
    constexpr q24_t VQ_SCALE = q24(1.273f);
    g_foc.setCurrentLoopLimits(vbus * LIMIT_SCALE * VD_SCALE, vbus * LIMIT_SCALE * VQ_SCALE);
    g_foc.setVbus(vbus);
}

static void task_state(void) {
    NON_BLOCK_DELAY(10);

    static uint32_t ovc_timestamp = 0;
    static bool may_ovc = false;

    switch (sys_reg.state) {
        case SysStates::INIT:
            inv_set_duty(0x8000, 0x8000, 0x8000);
            g_foc.calibCurrent();
            sys_reg.state = SysStates::RUN;
            ADC1_ResetVbusIrq();
            break;
        case SysStates::FAULT:
            g_foc.disable();
            break;
        case SysStates::RUN:
            if (boot_mode == DEBUG_ && calib_pos_requested) {
                sys_reg.state = SysStates::CALIB_POS;
                calib_pos_requested = false;
            }
            if (ABS((float)g_foc.getIq()) > flash_save_data.cfg.ovc_threshold) {
                if (!may_ovc) {
                    may_ovc = true;
                    ovc_timestamp = Systick_getMsTick();
                }
            }
            else {
                may_ovc = false;
            }
            break;
        case SysStates::CALIB_POS:
            flash_save_data.cfg.zero_ang_calib = g_foc.calibZeroPos();
            /* keep the fault state if a fault occurred during calibration */
            if (sys_reg.state == SysStates::CALIB_POS) sys_reg.state = SysStates::RUN;
            break;
        default:
            sys_reg.state = SysStates::FAULT;
            break;
    }

    if (sys_reg.state != SysStates::RUN) {
        may_ovc = false;
    }

    if (may_ovc && (Systick_getMsTick() - ovc_timestamp > flash_save_data.cfg.ovc_timeout_ms)) {
        may_ovc = false;
        sys_reg.fault.over_current = 1;
        sys_reg.state = SysStates::FAULT;
        inv_set_duty(0x0000, 0x0000, 0x0000);
    }

    g_protocol_rx.updateTimeoutState();

    static int32_t last_turn = g_encoder.readAngleTurns();
    static uint32_t input_timestamp = 0;
    if (sys_reg.state == SysStates::RUN && boot_mode == DEBUG_ && !g_foc.m_en) {
        int32_t now_turn = g_encoder.readAngleTurns();
        int32_t dturn = now_turn - last_turn;

        switch (ui_state) {
            case UiStates::IDLE:
                if (ABS(dturn) > 1) {
                    ui_state = UiStates::INPUT;
                    led_master = LedMaster::ADDR;
                    input_timestamp = Systick_getMsTick();
                    last_turn = now_turn;
                }
                break;
            case UiStates::INPUT:
                if (dturn > 0) {
                    if (flash_save_data.addr < 7) flash_save_data.addr++;
                    input_timestamp = Systick_getMsTick();
                    last_turn = now_turn;
                }
                else if (dturn < 0) {
                    if (flash_save_data.addr > 0)      flash_save_data.addr--;
                    input_timestamp = Systick_getMsTick();
                    last_turn = now_turn;
                }
                if (Systick_getMsTick() - input_timestamp > 10000) {
                    ui_state = UiStates::CALIB_POS;
                    input_timestamp = Systick_getMsTick();
                }
                break;
            case UiStates::CALIB_POS:
                sys_reg.state = SysStates::CALIB_POS;
                ui_state = UiStates::SAVE_CONFIG;
                break;
            case UiStates::SAVE_CONFIG:
                save_config_to_flash(&flash_save_data);
                g_protocol_rx.setAddr(flash_save_data.addr);
                last_turn = g_encoder.readAngleTurns();
                ui_state = UiStates::IDLE;
                led_master = LedMaster::COMM;
                break;
        }   
    }
    else {
        last_turn = g_encoder.readAngleTurns();
        if (ui_state != UiStates::IDLE) {
            ui_state = UiStates::IDLE;
            led_master = LedMaster::COMM;
        }
    }

    if (boot_mode == NORMAL_) {
        bool sel_pin = GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_0) == Bit_RESET;
        if (sel_pin) {
            led_master = LedMaster::ADDR;
        }
        else {
            led_master = LedMaster::COMM;
        }
    }
}

static void task_led(void) {
    NON_BLOCK_DELAY(150);
    static uint8_t now_addr_cnt = 0;
    static uint8_t gap_cnt = 0;
    static LedMaster last_led_master = LedMaster::COMM;
    static uint8_t now_addr = 0;
    if (led_master != last_led_master) {
        last_led_master = led_master;
        now_addr_cnt = 0;
        led_addr_state = LedAddrStates::OFF;
        now_addr = flash_save_data.addr;
        LED_OFF;
        return;
    }
    if (led_master == LedMaster::ADDR) {
        switch (led_addr_state) {
            case LedAddrStates::OFF:
                led_addr_state = LedAddrStates::ON;
                LED_ON;
                break;
            case LedAddrStates::ON:
                now_addr_cnt++;
                if (now_addr_cnt >= (now_addr ? now_addr : 8)) { // if addr=0x00, blink 8 times
                    now_addr_cnt = 0;
                    led_addr_state = LedAddrStates::GAP;
                    gap_cnt = 0;
                }
                else {
                    led_addr_state = LedAddrStates::OFF;
                }
                LED_OFF;
                break;
            case LedAddrStates::GAP:
                gap_cnt++;
                if (gap_cnt > 2) {
                    gap_cnt = 0;
                    now_addr = flash_save_data.addr;
                    led_addr_state = LedAddrStates::OFF;
                }
                LED_OFF;
                break;
        }
    }
}

static void task_protocol(void) {
    // NON_BLOCK_DELAY(1);
    uint8_t byte;
    while (g_uart.readByte(byte)) {
        g_protocol_rx.processCmdFrame(byte);
    }
}

void cmd_execute(bool is_valid, uint8_t tag, uint8_t cmd, uint32_t data) {
    if (led_master == LedMaster::COMM) LED_ON;
    uint32_t v[3];
    memset(v, 0, sizeof(v));
    memset(tx_buffer, 0, sizeof(tx_buffer));

    auto fill_feedback = [&v]{
        v[0] = bit_cast(uint32_t, (float)g_foc.getIq());
        v[1] = bit_cast(uint32_t, (float)g_foc.getMechSpeed() * SPD_UNIT);
        v[2] = bit_cast(uint32_t, (float)g_foc.getPosFb() * POS_UNIT);
    };

    int n = 1;
    DataFrameType type = DataFrameType::SHORT;
    bool rejected = false;
    if (!is_valid) {
        n = 3;
        type = DataFrameType::REQUIRE_RESEND;
    }
    else {
        switch ((Cmd)cmd) {
            case Cmd::SET_MODE:
                flash_save_data.cfg.loop_mode = static_cast<FocLoopMode>(data);
                g_foc.setLoopMode(flash_save_data.cfg.loop_mode);
                n = 3;
                break;
            case Cmd::SET_CURRENT:
                g_foc.setIqRef(q24(bit_cast(float, data)));
                n = 3;
                break;
            case Cmd::SET_SPEED:
                g_foc.setSpeedRef(bit_cast(float, data));
                n = 3;
                break;
            case Cmd::SET_POSITION:
                g_foc.setPosRefFloat(bit_cast(float, data));
                n = 3;
                break;
            case Cmd::SET_CURRENT_BW:
                flash_save_data.cfg.current_loop_bw = bit_cast(float, data);
                g_foc.setCurrentLoopBandWidth(flash_save_data.cfg.current_loop_bw);
                n = 3;
                break;
            case Cmd::SET_SPEED_BW:
                flash_save_data.cfg.speed_loop_bw = bit_cast(float, data);
                g_foc.setSpeedLoopBandWidth(flash_save_data.cfg.speed_loop_bw);
                n = 3;
                break;
            case Cmd::SET_SPEED_ZETA:
                flash_save_data.cfg.speed_loop_zeta = bit_cast(float, data);
                g_foc.setSpeedLoopDamping(flash_save_data.cfg.speed_loop_zeta);
                n = 3;
                break;
            case Cmd::SET_POS_BW:
                flash_save_data.cfg.pos_loop_bw = bit_cast(float, data);
                g_foc.setPosLoopBandWidth(flash_save_data.cfg.pos_loop_bw);
                n = 3;
                break;
            case Cmd::SET_ESO_BW:
                flash_save_data.cfg.eso_bw = bit_cast(float, data);
                g_foc.setEsoBandWidth(flash_save_data.cfg.eso_bw);
                n = 3;
                break;
            case Cmd::SET_ABFILT_BW:
                flash_save_data.cfg.abfilt_bw = bit_cast(float, data);
                g_foc.setSpeedABFiltBandWidth(flash_save_data.cfg.abfilt_bw);
                n = 3;
                break;
            case Cmd::SET_ABFILT_ZETA:
                flash_save_data.cfg.abfilt_zeta = bit_cast(float, data);
                g_foc.setSpeedABFiltDamping(flash_save_data.cfg.abfilt_zeta);
                n = 3;
                break;
            case Cmd::GET_FEEDBACK:
                n = 3;
                break;
            case Cmd::GET_CURRENT:
                v[0] = bit_cast(uint32_t, (float)g_foc.getIq());
                n = 1;
                break;
            case Cmd::GET_SPEED:
                v[0] = bit_cast(uint32_t, (float)g_foc.getMechSpeed() * SPD_UNIT);
                n = 1;
                break;
            case Cmd::GET_POSITION:
                v[0] = bit_cast(uint32_t, (float)g_foc.getPosFb() * POS_UNIT);
                n = 1;
                break;
            case Cmd::GET_VBUS:
                v[0] = bit_cast(uint32_t, (float)g_foc.getVbus());
                n = 1;
                break;
            case Cmd::ENABLE:
                if (sys_reg.state == SysStates::RUN) g_foc.enable();
                else rejected = true;
                n = 1;
                break;
            case Cmd::DISABLE:
                if (sys_reg.state == SysStates::RUN) g_foc.disable();
                else rejected = true;
                n = 1;
                break;
            case Cmd::GET_SYSREG:
                v[0] = sys_reg.all;
                n = 1;
                break;
            case Cmd::CLEAR_FAULT:
                if (sys_reg.state == SysStates::FAULT) {
                    sys_reg.fault.fault = 0;
                    sys_reg.warning.warning = 0;
                    inv_set_duty(0x8000, 0x8000, 0x8000);
                    sys_reg.state = SysStates::RUN;
                    ADC1_ResetVbusIrq();
                }
                else rejected = true;
                break;
            case Cmd::NOP:
            default:
                n = 1;
                break;
        }
        if (n == 3) fill_feedback();
        /* DEBUG COMMANDS */
        if (boot_mode == DEBUG_) {
            switch ((CmdDebug)cmd) {
                case CmdDebug::CALIB_ZERO_POS:
                    calib_pos_requested = true;
                    n = 1;
                    break;
                case CmdDebug::GET_ADDR:
                    v[0] = (uint32_t)flash_save_data.addr;
                    n = 1;
                    break;
                case CmdDebug::GET_DVC_INFO:
                    v[0] = FIRMWARE_VERSION;
                    v[1] = DBGMCU_GetCHIPID();
                    v[2] = DBGMCU_GetDEVID();
                    n = 3;
                    break;
                case CmdDebug::SET_BAUDRATE:
                    flash_save_data.baudrate = data;
                    n = 1;
                    break;
                case CmdDebug::SET_DEFAULT_CONFIG: {
                    const uint8_t addr_bak = flash_save_data.addr;
                    flash_save_data = flash_data_default;
                    flash_save_data.addr = addr_bak;
                    n = 1;
                    break;
                }
                case CmdDebug::SAVE_CONFIG: {
                    bool en_bak = g_foc.m_en;
                    g_foc.disable();
                    save_config_to_flash(&flash_save_data);
                    if (en_bak) g_foc.enable();
                    n = 1;
                    break;
                }
                case CmdDebug::SET_OVC_THRESHOLD:
                    flash_save_data.cfg.ovc_threshold = bit_cast(float, data);
                    n = 1;
                    break;
                case CmdDebug::SET_OVC_TIMEOUT:
                    flash_save_data.cfg.ovc_timeout_ms = bit_cast(uint32_t, data);
                    n = 1;
                    break;
                case CmdDebug::SET_CONT_ANG_ZERO_OFFSET: {
                    uint16_t offset = g_foc.setContAngZeroOffset();
                    g_foc.setPosRefFloat(0.0f); // avoid jump
                    flash_save_data.cfg.cont_ang_zero_offset = (uint32_t)offset;
                    n = 1;
                    break;
                }
                default:
                    break;
            }
        }
        if (rejected) {
            n = 3;
            type = DataFrameType::FAULT;
            fill_feedback();
        }
        else if ((uint8_t)cmd > (uint8_t)CmdDebug::SET_CONT_ANG_ZERO_OFFSET || // reserved
                 ((uint8_t)cmd > (uint8_t)Cmd::CLEAR_FAULT && boot_mode != DEBUG_)) { // not in debug
            n = 3;
            type = DataFrameType::NOT_SUPPORTED;
            fill_feedback();
        }
        else {
            const bool warn = (sys_reg.warning.warning != 0);
            type = (n == 3) ? (warn ? DataFrameType::LONG_WITH_WARNING : DataFrameType::LONG)
                            : (warn ? DataFrameType::SHORT_WITH_WARNING : DataFrameType::SHORT);
        }
    }

    g_protocol_tx.setType(type);
    g_protocol_tx.setTag(tag);
    g_protocol_tx.setData(reinterpret_cast<const uint8_t*>(v), n * (int)sizeof(uint32_t));

    const int len = g_protocol_tx.getTxFrame(tx_buffer);
    bool trans_ok = g_uart.transmit_async(tx_buffer, (uint16_t)len);
    if (!trans_ok) g_protocol_rx.cpltSend(); // busy, discard this frame
}

void uart_tx_cplt_callback(void) {
    g_uart.receive_variable_on();
    g_protocol_rx.cpltSend();
    if (led_master == LedMaster::COMM) LED_OFF;
}
