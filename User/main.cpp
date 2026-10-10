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
#include "ch32m030.h"
#include "ch32m030_flash.h"
#include "debug.h"
#include "delay.h"
#include "peripheral.h"
#include "smath.h"
#include "three_phase_inverter.h"
#include "systick_time.h"
#include "sstrtotype.h"
#include "ch32m030_dma.h"

/*Include Cxx headers*/
#include "mt6701_ssi.h"
#include "iqmath.h"
#include "foc_driver.h"


/* ========= Function Prototypes ========= */
static void tm_tx_frame(void);
extern "C" void updateEncoder(void);
static void task_vbus(void);
static void task_state(void);
extern "C" void task_outer_loop(void);
static void task_ramp(void);


/* ========= Global Variables ========= */
/* Global Variable */

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

SimuSSI<24> ssi(ssi_cfg);

/* Global MT6701 encoder instance */
MT6701_SSI g_encoder(ssi);

FocDriver g_foc(g_encoder);

static volatile uint32_t g_tick_count = 0;

/* 编码器节拍使能：上电默认关。g_encoder.init() 之前绝不能发异步读——那一阶段有阻塞式同步
 * I2C 读（init/校准），同一 i2c_node 上异步与同步混用会毁状态机。main() 里 init 完成后置 true。 */
static volatile bool g_enc_tick_enable = false;

/* 两种模式共用：故障打印去重 */
static bool     g_fault_reported = false;

constexpr q24_t FOC_ADC_SCALE_V = q24(3.3f * (14e3f + 100e3f) / 14e3f);
constexpr q24_t vbus_calib      = q24(-0.01f);
constexpr q24_t FOC_VBUS_MIN_V  = q24(5.0f);

#define FOC_CALIB_ZERO 1
const uint32_t FOC_CALIB_ADDR = 0x0800FF80u;

/* ===== 运行模式选择（编译期宏，改完重新编译烧写）=====
 * FOC_LOOP_MODE：0=电流环（iq 给定直接给 pid_iq，原行为）；1=速度环；2=位置环（级联速度环）。
 *   参数见 foc_driver.h 的 SPEED_ / POS_ 常量与 FocDriver::setLoopMode()。
 * FOC_TM_LOOP  ：0=发电流环遥测（网页 foc_monitor.html）；1=发速度/位置环遥测
 *   （网页 foc_loop_monitor.html）。**两者互斥**：通道数/帧宽相同（10 通道 = 44B），
 *   DMA 双缓冲与波特率占用不变，只是 10 个通道的语义与对应网页不同。 */
#define FOC_LOOP_MODE 1
#define FOC_TM_LOOP   2

#if !FOC_TM_LOOP
/* 电流环遥测通道索引：Iu Iv Iw | Id Iq | Vd Vq | 角度rad | 预测电角rad | 控制拍计数 */
enum {
    TM_IU = 0, TM_IV, TM_IW,       /* 相电流（A） */
    TM_ID,     TM_IQ,              /* d/q 轴电流反馈（A） */
    TM_VD,     TM_VQ,              /* d/q 轴电压参考（V） */
    TM_ANG,    TM_PRED,            /* 转子机械角（rad）/ 控制环预测电角（rad） */
    TM_TSTAMP,                     /* 控制拍计数（32 位；见 g_tick_count 注释） */
    TM_NCH
};
#else
enum {
    TM_SREF = 0, TM_SFB,           /* 速度给定/反馈（rad/s，机械） */
    TM_PREF,     TM_PFB,           /* 位置给定/反馈（rad，连续机械角） */
    TM_IQR,      TM_IQ,  TM_ID,    /* iq 给定/反馈、id 反馈（A） */
    TM_VD,       TM_VQ,            /* d 轴/q 轴电压参考（V，含解耦项与限幅后的值） */
    TM_ANG,      TM_ELEC,          /* 转子机械角（rad，0~2π）/ 电角度（rad） */
    TM_TSTAMP,                     /* 控制拍计数（32 位；见 g_tick_count 注释） */
    TM_NCH
};
#endif
/* 帧长：TM_NCH 个数据字 + 1 个尾字。
 * ⚠️ 改 2kHz 抽取值(CTRL_DECIM_2KHZ)或帧宽要同步看带宽：
 *    帧长(TM_NCH*4+4) × 速率 ≤ 波特率/10，留 ≥20% 余量；且发送时长必须 < 1 帧周期
 *    （tm_dma_kick 每帧会 DISABLE 通道，会截断在发的帧）。 */
static constexpr uint16_t TM_FRAME_BYTES = TM_NCH * 4 + 4;   /* 电流环 44 / 速度位置环 52 */

/* ===== 2kHz DMA 遥测发送（开环/闭环共用；节拍来源见 ctrl_tick_2khz）===== */
static uint32_t s_txbuf[2][TM_NCH + 1];   /* 每槽 = TM_NCH 数据 + 1 尾字，4 对齐 */
static uint8_t  s_tx_slot = 0;            /* 下帧要装载的缓冲槽（ping-pong） */


/* ========= Main ========= */

int main(void)
{
    SystemCoreClockUpdate();
    Delay_Init();
#if (SDI_PRINT == SDI_PR_OPEN)
    SDI_Printf_Enable();
#else
    USART_Printf_Init(2000000);
#endif
    SystickTimestamp_init();

    printf("[boot] 0 uart 2Mbaud + systick ok\r\n");
    
    /* Read first; programming option bytes on every boot is unnecessary. */
    const uint16_t desired_user_ob = OB_STOP_NoRST | OB_STDBY_NoRST |
                                     OB_RST_NoEN | OB_RST_PIN_PC0 |
                                     OB_OV_CFG_NoEN | 0x0041u;
    const uint32_t current_user_ob = FLASH_GetUserOptionByte();
    printf("UserOptionByte=%08lX (expected low byte=%02X)\r\n",
           (unsigned long)current_user_ob,
           (unsigned)(desired_user_ob & 0x00FFu));

    if ((current_user_ob & 0x00FFu) != (uint32_t)(desired_user_ob & 0x00FFu)) {
        FLASH_Status status = FLASH_UserOptionByteConfig(
            OB_STOP_NoRST,
            OB_STDBY_NoRST,
            OB_RST_NoEN,
            OB_RST_PIN_PC0,
            OB_OV_CFG_NoEN
        );
        printf("UserOptionByte update status=%d; power-cycle required\r\n",
               (int)status);
    }

    delay_ms_nortos(5000);

    printf("SystemClk:%d\r\n", SystemCoreClock);
    printf("ChipID:%08x\r\n", DBGMCU_GetCHIPID());

    g_foc.calibrating = true;

    ssi.init();
    PERIPHS_INIT();
    printf("[boot] 2 <- PERIPHS_INIT ok\r\n");

    USART1_DMA_TX_Init();

    const bool enc_ok = g_encoder.init();
    printf("[boot] SSI first frame: %s raw=%u ang_q24=%d\r\n",
           enc_ok ? "OK" : "FAIL",
           (unsigned)g_encoder.readRaw(),
           (int)g_encoder.readAngleRad().val);
    g_enc_tick_enable = enc_ok;

    inv_set_duty(0x8000, 0x8000, 0x8000);

    constexpr float k_current = R_s / L_s;
    constexpr float f_exp_i = 400.0f;
    constexpr float kp_iq = M_TWOPI * L_s * f_exp_i;;
    constexpr float kp_id = kp_iq * 0.8f;
    g_foc.pid_iq.setKp(q24(kp_iq));
    g_foc.pid_iq.setKiDt(q24(kp_iq * k_current * FOC_DT));
    g_foc.pid_iq.setOutputLimit(q24(0.0f));// This will be changed by task_vbus() after vbus is known
    g_foc.pid_iq.setTarget(q24(0.0f));
    g_foc.pid_id.setKp(q24(kp_id));
    g_foc.pid_id.setKiDt(q24(kp_id * k_current * FOC_DT));
    g_foc.pid_id.setOutputLimit(q24(0.0f));// This will be changed by task_vbus() after vbus is known
    g_foc.pid_id.setTarget(q24(0.0f));
    
    constexpr float f_exp_spd = 10.0f;
    constexpr float wn_exp_spd = M_TWOPI * f_exp_spd;
    constexpr float zeta_exp_spd = 0.8f;
    constexpr float kp_spd = 2.0f * zeta_exp_spd * wn_exp_spd * J / K_t;
    constexpr float ki_spd = wn_exp_spd * wn_exp_spd * J / K_t;
    g_foc.pid_spd.setKp(q24(kp_spd));
    g_foc.pid_spd.setKiDt(q24(ki_spd * SPD_DT));
    g_foc.pid_spd.setOutputLimit(q24(1.60f));
    g_foc.pid_spd.setTarget(q24(0.0f));

    g_foc.pid_pos.setFactors(10.0f, 0.0f, 0.0f, 5e-3f); 
    g_foc.pid_pos.setTarget(0.0f);
    g_foc.pid_pos.setLimit(60.0f, 0.0f);
    g_foc.setLoopMode((FocLoopMode)FOC_LOOP_MODE);
    
    constexpr float f_exp_eso = 200.0f;
    constexpr float wn_exp_eso = M_TWOPI * f_exp_eso;
    constexpr float eso_l1 = 2 * wn_exp_eso; //appro
    constexpr float eso_l2 = wn_exp_eso * wn_exp_eso * J / K_t;
    g_foc.setEsoCoeffs(eso_l1, eso_l2);

    g_foc.setCurrentCutoffFreq(2000);
    g_foc.setSpeedFiltCoeff(300);

    while (1) {
        task_vbus();     /* 1000ms */
        task_state();    /* 50ms（编码器由控制环 2kHz 节拍驱动） */
        // task_outer_loop();
        // task_ramp();     /* ~1ms（闭环参考：电流环给 iq；FOC_LOOP_MODE=1/2 给速度/位置） */
    }
}


/* ======== Function Implementation ======== */
extern "C" void ssi_irqHandler(){
    ssi.irqHandler();
}

static inline bool CtrlTick_div5(void)
{
    static uint8_t dc = 0;
    if (++dc < 5) return false;
    dc = 0;
    return true;
}

static inline bool CtrlTick_div2(void)
{
    static uint8_t dc = 0;
    if (++dc < 2) return false;
    dc = 0;
    return true;
}

extern "C" void updateEncoder(void)
{
    if (!g_enc_tick_enable) return;

    g_encoder.update_async();
}

extern "C" void ADC_JEOC_irqHandler(void)
{
    g_tick_count++;   /* 控制拍计数：先自增，本拍若发帧带的就是当前拍号 */
    
    updateEncoder();           /* 编码器异步读（原 TIM3 1kHz 中断驱动，现并入本 ISR） */
    
    g_foc.updateCurrLoop();
    if (CtrlTick_div5()) {          /* 10kHz ÷5 → 2kHz */
        tm_tx_frame();               /* 2kHz 遥测帧：取本拍 update() 刚写好的量并启动 DMA */
    }
}

static void foc_store_zero_calib(q24_t zero_calib)
{
    __disable_irq();
    FLASH_Unlock_Fast();
    FLASH_ErasePage_Fast(FOC_CALIB_ADDR);
    while(FLASH_WaitForLastOperation(100000) != FLASH_COMPLETE) {}
    FLASH_BufReset();
    FLASH_BufLoad(FOC_CALIB_ADDR, bit_cast(uint32_t, zero_calib.val), 0u);
    while(FLASH_WaitForLastOperation(100000) != FLASH_COMPLETE) {}
    FLASH_ProgramPage_Fast(FOC_CALIB_ADDR);
    while(FLASH_WaitForLastOperation(100000) != FLASH_COMPLETE) {}
    FLASH_Lock();
    __enable_irq();
}

extern "C" void task_outer_loop(void)
{
    // NON_BLOCK_DELAY(1);

    if(CtrlTick_div2()){
        GPIO_WriteBit(GPIOA, GPIO_Pin_7, Bit_SET);
        g_foc.updatePosLoop();
        GPIO_WriteBit(GPIOA, GPIO_Pin_7, Bit_RESET);
    }

    g_foc.updateSpeedLoop();
}

static void task_vbus(void)
{
    NON_BLOCK_DELAY(5000);

    const uint16_t vbus_avg = ADC_GetAverageValue(ADC1);
    // const uint16_t vbus_avg = ADC_GetConversionValue(ADC1);
    q24_t vbus = q24_t::from_raw((int32_t)vbus_avg << 12);
    vbus = vbus * FOC_ADC_SCALE_V + vbus_calib;

    constexpr q24_t LIMIT_SCALE = q24(0.5f / M_SQRT2);
    g_foc.pid_id.setOutputLimit(vbus *  LIMIT_SCALE * 0.65f);
    g_foc.pid_iq.setOutputLimit(vbus *  LIMIT_SCALE * 1.35f);
    g_foc.setVbus(vbus);
}

static void task_state(void)
{
    NON_BLOCK_DELAY(50);
    if (g_foc.getVbus() < FOC_VBUS_MIN_V) {
        if (!g_fault_reported) { g_fault_reported = true; printf("[CTRL] waiting Vbus >= 5V\r\n"); }
        return;
    }
    g_fault_reported = false;

    static uint8_t seq = 0;

    static bool s_vbus_dbg = false;
    if (!s_vbus_dbg) {
        s_vbus_dbg = true;
        char vb[16]; s_ftoa(g_foc.getVbus().cvt2float(), vb, 2);
        printf("[CALIB] Vbus ready = %s V (seq=%u)\r\n", vb, (unsigned)seq);
    }
    if (seq == 0) {
        seq = 1;
        g_foc.calibrating = true;

        TIM_CtrlPWMOutputs(TIM1, ENABLE);
        inv_set_duty(0x8000, 0x8000, 0x8000);
        delay_ms_nortos(50);
        g_foc.calibCurrent();
        char b0[16], b1[16];
        s_ftoa(g_foc.getCurrentCalibU().cvt2float(), b0, 3);
        s_ftoa(g_foc.getCurrentCalibV().cvt2float(), b1, 3);
        printf("iu_cal_on:  %s, iv_cal_on:  %s\r\n", b0, b1);
    }
#if (FOC_CALIB_ZERO == 1)
    else if (seq == 1) {
        seq = 2;
        TIM_CtrlPWMOutputs(TIM1, ENABLE);    /* 角度校准开 PWM：需输出对齐电压 */
        printf("[CALIB] seq1 angle align (PWM on) -> calibZeroPos() ...\r\n");
        q24_t zc = g_foc.calibZeroPos();
        char zs[16]; s_ftoa(zc.cvt2float(), zs, 3);
        printf("[CALIB] seq1 aligned %s rad -> erase+write flash @%08lX ...\r\n",
               zs, (unsigned long)FOC_CALIB_ADDR);
        // foc_store_zero_calib(zc);
        printf("[CALIB] seq1 flash write done\r\n");
        g_foc.calibrating = false;           /* 校准完成，恢复正常控制环 */

        q24_t bk_rd = q24_t::from_raw(*(const uint32_t*)FOC_CALIB_ADDR);
        char b0[16]; s_ftoa(bk_rd.cvt2float(), b0, 3);
        printf("[CALIB] stored: %s; set FOC_CALIB_ZERO=0 next build\r\n", b0);
        q24_t now_elec_ang = g_foc.getMechanAng() * POLE_PAIRS;   /* 电角度 = 机械角度 * 极对数 */
        q24_t now_ang_calib = g_foc.getAngCalib();
        char b1[16]; s_ftoa(now_ang_calib.cvt2float(), b1, 3);
        char b2[16]; s_ftoa(now_elec_ang.cvt2float(), b2, 3);
        printf("[CALIB] now elec ang = %s rad, ang_calib = %s rad\r\n", b2, b1);
    }
#else
    else if (seq == 1) {
        seq = 2;
        q24_t zc = q24_t::from_raw(*(const uint32_t*)FOC_CALIB_ADDR);
        g_foc.setAngCalib(zc);
        TIM_CtrlPWMOutputs(TIM1, ENABLE);    /* 校准就绪，开 PWM 正常运行 */
        g_foc.calibrating = false;           /* 校准完成，恢复正常控制环 */
        printf("[CALIB] zero loaded from flash\r\n");
    }
#endif
}

#if !FOC_LOOP_MODE
/* ===== iq 参考发生器（闭环 稳定测试）=====
 * 正弦：周期 2s、幅度 0.3A（稳态跟踪 / 噪声底 / 极限环 观察用）。
 * 相位按本 task 的节拍累加：1ms 一拍 → 一周期 2000 拍 = 2s。
 * 校准期把相位清 0：校准结束从过零点起振，起步不跳台阶（不用额外的起振判断）。 */
static constexpr q24_t IQ_SIN_AMP  = q24(0.30f);                        /* 幅度（A） */
static constexpr q24_t IQ_SIN_STEP = q24(6.283185307179586f / 2000.0f); /* 每 1ms 拍 = 2π/2000 */

// #define FOC_TAU_MEASURE 1   /* 临时：kt/J + 两段惰行对比（PWM 开 / 关）。删本行 + task_ramp 里 #if 块即恢复 */

static void task_ramp(void)
{
#if FOC_TAU_MEASURE
    static constexpr q24_t TAU_IQ = q24(0.15f);
    static constexpr uint32_t TAU_TA = 60000u;
    static const uint32_t s_lt[8] = { 10000u, 20000u, 40000u, 80000u, 160000u, 320000u, 640000u, 1280000u };

    static uint8_t  s_st = 0;
    static uint8_t  s_g1 = 0, s_g2 = 0, s_nc = 0;
    static uint32_t s_us0 = 0, s_t1 = 0, s_t2 = 0;
    static q24_t    s_ang = Q24_ZERO;
    static float    s_w = 0.0f, s_w1 = 0.0f, s_w2 = 0.0f, s_kj = -1.0f;
    static float    s_wa[8] = {0.0f}, s_wb[8] = {0.0f};

    NON_BLOCK_DELAY(1);
    if (s_st >= 5) return;

    const q24_t a_now = g_foc.getMechanAng();
    q24_t dd = q24_sub_w(a_now, s_ang);
    s_ang = a_now;
    if      (dd.val >  Q24_PI.val) dd = q24_sub_w(dd, Q24_TWOPI);
    else if (dd.val < -Q24_PI.val) dd = q24_add_w(dd, Q24_TWOPI);
    s_w = dd.cvt2float() * 1000.0f;

    const uint32_t el = (uint32_t)(Systick_getUsTick() - s_us0);

    if (s_st == 0) {
        g_foc.pid_iq.setTarget(Q24_ZERO);
        if (g_foc.calibrating) return;
        s_ang = a_now;
        s_us0 = (uint32_t)Systick_getUsTick(); s_st = 1;
    }
    else if (s_st == 1 || s_st == 3) {
        g_foc.pid_iq.setTarget(TAU_IQ);
        if (s_st == 1) {
            if (!s_g1 && el >= 10000u) { s_w1 = s_w; s_t1 = el; s_g1 = 1; }
            if (!s_g2 && el >= 40000u) {
                s_w2 = s_w; s_t2 = el; s_g2 = 1;
                s_kj = (s_w2 - s_w1) / ((s_t2 - s_t1) * 1e-6f) / TAU_IQ.cvt2float();
            }
        }
        if (el >= TAU_TA) {
            s_nc = 1;
            if (s_st == 1) {
                s_wa[0] = s_w; s_st = 2;
            } else {
                s_wb[0] = s_w; s_st = 4;
                g_foc.pid_iq.setTarget(Q24_ZERO);
                inv_set_duty(0x8000, 0x8000, 0x8000);
                TIM_CtrlPWMOutputs(TIM1, DISABLE);
            }
            s_us0 = (uint32_t)Systick_getUsTick();
        }
    }
    else if (s_st == 2 || s_st == 4) {
        g_foc.pid_iq.setTarget(Q24_ZERO);
        float *p = (s_st == 2) ? s_wa : s_wb;
        while (s_nc < 8 && el >= s_lt[s_nc]) { p[s_nc] = s_w; ++s_nc; }
        if (s_nc >= 8) s_st = (s_st == 2) ? 3 : 5;
    }

    if (s_st >= 5) {
        g_foc.pid_iq.setTarget(Q24_ZERO);
        inv_set_duty(0x8000, 0x8000, 0x8000);
        TIM_CtrlPWMOutputs(TIM1, DISABLE);
        g_foc.calibrating = true;
        char b0[16], b1[16], c[8][12], d[8][12];
        s_ftoa(s_kj, b0, 0);
        s_ftoa(s_wa[0], b1, 1);
        for (uint8_t i = 0; i < 8; ++i) { s_ftoa(s_wa[i], c[i], 1); s_ftoa(s_wb[i], d[i], 1); }
        printf("[TAU] kt/J=%s  w0=%s\r\n", b0, b1);
        printf("[TAU] A pwm-on  %s %s %s %s %s %s %s %s\r\n", c[0], c[1], c[2], c[3], c[4], c[5], c[6], c[7]);
        printf("[TAU] B pwm-off %s %s %s %s %s %s %s %s\r\n", d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7]);
    }
#else
    static q24_t s_phase = Q24_ZERO;

    NON_BLOCK_DELAY(1);   /* 1ms 是本 task 的下限（NON_BLOCK_DELAY 是 ms 基准） */

    if (g_foc.calibrating) {       /* 校准期输出 0 并复位相位 */
        s_phase = Q24_ZERO;
        g_foc.pid_iq.setTarget(Q24_ZERO);
        return;
    }

    s_phase = s_phase + IQ_SIN_STEP;
    if (s_phase >= Q24_TWOPI) s_phase = s_phase - Q24_TWOPI;

    g_foc.pid_iq.setTarget(IQ_SIN_AMP * q_sin(s_phase));
#endif
}
#else
/* ===== 外环参考发生器（速度环/位置环 稳定测试）=====
 * 同上：1ms 一拍的正弦，周期 2s。
 *   速度环：幅度 ±SPD_REF_AMP；位置环：绕"进入时连续机械角"摆动 ±POS_REF_AMP。
 * 校准期清相位/基准：校准结束后从过零点起振，且位置环基准 = 校准后的实际角度。 */
static constexpr q24_t REF_STEP    = q24(6.283185307179586f / 2000.0f); /* 每 1ms 拍 = 2π/2000 */
static constexpr q24_t SPD_REF_AMP = q24(30.0f);                        /* 速度给定幅值（rad/s，≈573rpm） */
static constexpr q24_t POS_REF_AMP = q24(1.5707963268f);                /* 位置给定幅值（rad，= 90°） */

/* 速度环阶跃序列（rad/s）：0.5 → 1 → 1.5 → -1.5 → 1 → 0.5 → 0，每档保持 SPD_HOLD_MS */
static constexpr q24_t    SPD_SEQ[7]  = { q24(5), q24(10), q24(15), q24(-15),
                                          q24(-10), q24(-5), q24(0) };
static constexpr uint8_t  SPD_SEQ_N   = 7u;
static constexpr uint32_t SPD_HOLD_MS = 500u;

/* 每 ~1ms：给出外环目标（速度环给 pid_sp，位置环给 pid_pos） */
static void task_ramp(void)
{
    static q24_t s_phase = Q24_ZERO;
    static q24_t s_ang0  = Q24_ZERO;
    static bool  s_base  = false;
    static uint32_t s_ms = 0;
    static uint8_t  s_idx = 0;

    NON_BLOCK_DELAY(1);

    if (g_foc.calibrating) {       /* 校准期输出 0 并复位相位/位置基准 */
        s_phase = Q24_ZERO; s_base = false;
        g_foc.pid_spd.setTarget(Q24_ZERO);
        g_foc.pid_pos.setTarget(0);
        return;
    }
    if (!s_base) {                 /* 位置环基准：校准后第一次进本 task 时的连续机械角 */
        s_base = true;
        s_ang0 = g_foc.getPosFb();
        s_ms   = Systick_getMsTick(); s_idx = 0;
    }

    s_phase = s_phase + REF_STEP;
    if (s_phase >= Q24_TWOPI) s_phase = s_phase - Q24_TWOPI;

#if (FOC_LOOP_MODE == 2)
    // g_foc.pid_pos.setTarget(q24_add_w(s_ang0, POS_REF_AMP * q_sin(s_phase)));
    g_foc.pid_pos.setTarget(0.0f);
#else
#define SPD_SIN_REF 1   /* 临时：速度给定换成正弦（±SPD_REF_AMP，周期 2s）。删本行即回到阶跃序列 */
#ifdef SPD_SIN_REF
    g_foc.pid_spd.setTarget(SPD_REF_AMP * q_sin(s_phase));
#else
    const uint32_t now = Systick_getMsTick();
    if (((uint32_t)(now - s_ms) >= SPD_HOLD_MS) && (s_idx + 1u < SPD_SEQ_N)) {
        s_ms = now;
        ++s_idx;
    }
    g_foc.pid_spd.setTarget(SPD_SEQ[s_idx]);
#endif
#endif
}
#endif  /* 参考发生器：电流环 / 外环 二选一 */

/* 帧时间戳 = 控制拍计数（定义见文件上方 g_tick_count）。 */
static inline uint32_t tm_stamp(void)
{
    return g_tick_count;
}

/* 触发一次 44B DMA 发送（当前槽）并切到另一槽。 */
static inline void tm_dma_kick(void)
{
    DMA_Cmd(DMA1_Channel6, DISABLE);
    DMA1_Channel6->CNTR  = TM_FRAME_BYTES;
    DMA1_Channel6->MADDR = (uint32_t)s_txbuf[s_tx_slot];
    DMA_ClearFlag(DMA1_FLAG_TC6);
    DMA_Cmd(DMA1_Channel6, ENABLE);
    s_tx_slot ^= 1;
}

static void tm_tx_frame(void)
{
    /* 校准期（g_foc.calibrating=true）主循环有 printf 写 USART1，暂停 DMA 避免毁帧 */
    if (g_foc.calibrating) return;

    uint32_t *d = s_txbuf[s_tx_slot];
#if !FOC_TM_LOOP
    const int32_t iu = g_foc.getIu().val;
    const int32_t iv = g_foc.getIv().val;
    d[TM_IU]   = (uint32_t)iu;
    d[TM_IV]   = (uint32_t)iv;
    d[TM_IW]   = (uint32_t)(-(iu + iv));
    d[TM_ID]   = (uint32_t)g_foc.getId().val;
    d[TM_IQ]   = (uint32_t)g_foc.getIq().val;
    d[TM_VD]   = (uint32_t)g_foc.getVdRef().val;
    d[TM_VQ]   = (uint32_t)g_foc.getVqRef().val;
    d[TM_ANG]  = (uint32_t)g_foc.getMechanAng().val;
    d[TM_PRED] = (uint32_t)g_foc.getPredElecAng().val;   /* 控制环预测电角（含 LEAD_TICKS 补偿） */
#else
    d[TM_SREF] = (uint32_t)g_foc.getSpeedRef().val;   /* 速度给定（速度环基准 / 位置环输出） */
    d[TM_SFB]  = (uint32_t)g_foc.getSpeedFb().val;
    d[TM_PREF] = (uint32_t)g_foc.getPosRef().val;
    d[TM_PFB]  = (uint32_t)g_foc.getPosFb().val;
    d[TM_IQR]  = (uint32_t)g_foc.getIqRef().val;      /* iq 给定（速度环输出） */
    d[TM_IQ]   = (uint32_t)g_foc.getIq().val;
    d[TM_ID]   = (uint32_t)g_foc.getId().val;
    d[TM_VD]   = (uint32_t)g_foc.getVdRef().val;
    d[TM_VQ]   = (uint32_t)g_foc.getVqRef().val;
    d[TM_ANG]  = (uint32_t)g_foc.getMechanAng().val;
    d[TM_ELEC] = (uint32_t)g_foc.getElecAng().val;
#endif
    d[TM_TSTAMP] = tm_stamp();
    d[TM_NCH]  = 0x7F800000u;           /* JustFloat 尾 00 00 80 7F */
    tm_dma_kick();
}
