#pragma once

#include "three_phase_inverter.h"
#include "iqmath.h"
#include "mt6701_ssi.h"
#include "alg_pid.h"
#include "foc_math.h"
#include "delay.h"
#include "motor_params.h"
#include <cstdint>

static constexpr q24_t CURRENT_SCALE = q24(3.3f / (50e-3f * 8.0f));
static constexpr q24_t CURRENT_CALIB_DEFAULT = q24(1986.0f / 4096.0f); // 1.6v v bias

static constexpr float FOC_DT = 100e-6f;

static constexpr q24_t K_L   = q24(L_s / FOC_DT);
static constexpr q24_t K_LAMBDA = q24(Lambda / FOC_DT);

static constexpr uint8_t SPD_DECIM = 10;                        /* 10kHz ÷10 = 1kHz */
static constexpr float   SPD_DT    = SPD_DECIM * FOC_DT;   /* 1ms */

static constexpr float   OBS_DT = FOC_DT;

static constexpr q24_t K_B_div_J = q24(B / J * FOC_DT);
static constexpr q24_t K_Kt_div_J = q24(K_t / J * FOC_DT);

/* 外环工作模式：默认 Current = 不接外环（iq 给定仍由外部直接给 pid_iq，行为与原来一致） */
enum class FocLoopMode : uint8_t { Current = 0, Speed, Pos };

class FocDriver{
public:
    FocDriver(MT6701_SSI& enc);
    foc_pi_controller pid_id;
    foc_pi_controller pid_iq;
    foc_pi_controller pid_spd;    /* 速度环：机械角速度(rad/s) → iq 给定(A) */
    PID<float> pid_pos;   /* 位置环：连续机械角(rad) → 速度给定(rad/s) */
    
    void outputVoltage(q24_t u, q24_t v, q24_t w);

    void calibCurrent();
    q24_t calibZeroPos();

    void setVbus(q24_t vbus);
    q24_t getVbus() const { return vbus; }

    q24_t getMechanAng() const { return mech_ang; }
    q24_t getElecAng() const { return elec_ang; }
    q24_t getMechSpeed() const { return mech_speed; }
    void setAngCalib(q24_t calib) { ang_calib = calib; }
    q24_t getAngCalib() const { return ang_calib; }

    void setLoopMode(FocLoopMode mode);
    FocLoopMode getLoopMode() const { return loop_mode_; }

    void setCurrentCutoffFreq(float cutoff_freq) {
        current_filt_coeff_ = q24(1.0f - 1.0f / (1.0f + 2.0f * M_PI * cutoff_freq * FOC_DT));
        if (current_filt_coeff_ < 0) current_filt_coeff_ = Q24_ZERO;
        else if (current_filt_coeff_ > 1) current_filt_coeff_ = Q24_ONE; 
    }
    void setSpeedFiltCoeff(float cutoff_freq) {
        speed_filt_coeff_ = q24(1.0f - 1.0f / (1.0f + 2.0f * M_PI * cutoff_freq * FOC_DT));
        if (speed_filt_coeff_ < 0) speed_filt_coeff_ = Q24_ZERO;
        else if (speed_filt_coeff_ > 1) speed_filt_coeff_ = Q24_ONE;
    }

    void setEsoCoeffs(float l1, float l2) {
        eso_coeffs.l1 = q24(l1 * FOC_DT);
        eso_coeffs.l2 = q24(l2 * FOC_DT);
    }

    void setSpeedABFiltCoeff(float a, float b) {
        abfilt_coeffs.a = q24(a * OBS_DT);
        abfilt_coeffs.b = q24(b * OBS_DT / 32.0f);
    }

    void setDisturbFiltCoeff(float cutoff_freq) {
        disturb_filt_coeff_ = q24(1.0f - 1.0f / (1.0f + 2.0f * M_PI * cutoff_freq * FOC_DT));
        if (disturb_filt_coeff_ < 0) current_filt_coeff_ = Q24_ZERO;
        else if (disturb_filt_coeff_ > 1) current_filt_coeff_ = Q24_ONE; 
    }

    /* current loop debug */
    q24_t getIu() const { return current.u; }
    q24_t getIv() const { return current.v; }
    q24_t getIw() const { return -(current.u + current.v); }
    q24_t getId()   const { return i_d_fb_; }
    q24_t getIq()   const { return i_q_fb_; }
    q24_t getVdRef() const { return v_d_ref_; }
    q24_t getVqRef() const { return v_q_ref_; }
    q24_t getCurrentCalibU() const { return current_calib.u; }
    q24_t getCurrentCalibV() const { return current_calib.v; }

    q24_t getABFiltState1() const { return theta_hat; }
    q24_t getABFiltState2() const { return mech_speed; }

    q24_t getEsoState1() const { return eso_states.mech_spd_hat; }
    q24_t getEsoState2() const { return eso_states.disturb_hat; }

    /* outer loop debug */
    q24_t getSpeedRef() const { return pid_spd.getTarget(); }
    q24_t getSpeedFb()  const { return mech_speed; }
    q24_t getPosRef()   const { return q24(pid_pos.getTarget()); }
    q24_t getPosFb()    const {
        if(loop_mode_ == FocLoopMode::Pos) return mech_ang_cont_;
        else return mech_ang; 
    }
    q24_t getIqRef()    const { return pid_iq.getTarget(); }

    volatile bool calibrating = false;

    void updateCurrLoop();
    void updateSpeedLoop();
    void updatePosLoop();

    void observeMechSpeed();
    void observeMechSpeed2();
private:
    MT6701_SSI& encoder;
    q24_t vbus = Q24_ZERO;
    q24_t inv_vbus = Q24_MAX;
    q24_t mech_ang = Q24_ZERO;
    int32_t turns = 0;
    q24_t elec_ang = Q24_ZERO;
    q24_t elec_ang_last = Q24_ZERO;
    q24_t mech_ang_diff = Q24_ZERO;
    q24_t mech_speed = Q24_ZERO;
    q24_t disturbance = Q24_ZERO;

    inline void readMechanAngFromEnc() { 
        mech_ang = encoder.readAngleRad();
        turns = encoder.readAngleTurns();
    };
    void updateMechSpeed();
    
    void eso(q24_t mech_speed_meas);

    struct{
        q24_t u = Q24_ZERO;
        q24_t v = Q24_ZERO;
    } current;

    struct{
        q24_t u = CURRENT_CALIB_DEFAULT;
        q24_t v = CURRENT_CALIB_DEFAULT;
    } current_calib;

    q24_t ang_calib = Q24_ZERO;

    /* debug vars */
    q24_t i_d_fb_  = Q24_ZERO;   /* d 轴电流反馈（A，IIR 滤波后） */
    q24_t i_q_fb_  = Q24_ZERO;   /* q 轴电流反馈（A，IIR 滤波后） */
    q24_t v_d_ref_ = Q24_ZERO;   /* d 轴电压参考（V） */
    q24_t v_q_ref_ = Q24_ZERO;   /* q 轴电压参考（V） */

    /* dq 电流反馈一阶 IIR 低通状态（ISR 内维护） */
    q24_t i_d_fb_filt_ = Q24_ZERO;
    q24_t i_q_fb_filt_ = Q24_ZERO;

    bool initialized = false;

    q24_t  mech_ang_last_  = Q24_ZERO;    // 上一拍机械角（差分基准）
    bool   mech_spd_init_ = false;

    volatile q24_t current_loop_cmd = Q24_ZERO;
    volatile q24_t speed_loop_cmd = Q24_ZERO;
    
    q24_t current_filt_coeff_ = q24(0.2f);
    q24_t speed_filt_coeff_   = q24(0.15f);
    q24_t disturb_filt_coeff_ = q24(0.5f);
    
    q24_t theta_hat = Q24_ZERO;
    struct abfilt_coeff{
        q24_t a;
        q24_t b;
    } abfilt_coeffs = {Q24_ZERO, Q24_ZERO};
    // expected bandwidth: 
    struct eso_coeff{
        q24_t l1;
        q24_t l2;
    } eso_coeffs = {Q24_ZERO, Q24_ZERO};

    struct eso_state{
        q24_t mech_spd_hat;
        q24_t disturb_hat;
    } eso_states = {Q24_ZERO, Q24_ZERO};

    /* ---- 外环状态 ---- */
    FocLoopMode loop_mode_   = FocLoopMode::Current;
    q24_t mech_ang_cont_     = Q24_ZERO;  // 连续机械角（rad，多圈累加；位置环反馈，±128rad 饱和）
};
