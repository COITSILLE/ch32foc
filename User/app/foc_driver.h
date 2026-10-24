#pragma once

#include "three_phase_inverter.h"
#include "iqmath.h"
#include "mt6701_ssi.h"
#include "foc_math.h"
#include "delay.h"
#include "motor_params.h"
#include <cstdint>

static constexpr float OPA_GAIN = 8.0f;
static constexpr float EQ_GAIN = OPA_GAIN * (1e3f + 10e3f) / 10e3f; // OPA gain = 8, input resistor = 10k, filter resistor = 1k
static constexpr float SAMPLE_RESISTOR = 50e-3f;
static constexpr q24_t CURRENT_SCALE = q24(3.3f / (SAMPLE_RESISTOR * EQ_GAIN)); // 50mohm sample resistor
static constexpr q24_t CURRENT_CALIB_DEFAULT = q24(1986.0f / 4096.0f); // 1.6v v bias

static constexpr q24_t MAX_ABS_CURRENT = q24(1.6f / EQ_GAIN / SAMPLE_RESISTOR);

static constexpr float FOC_DT = 100e-6f;

static constexpr q24_t K_L   = q24(L_s / FOC_DT);
static constexpr q24_t K_LAMBDA = q24(Lambda / FOC_DT);

static constexpr uint8_t SPD_DECIM = 10;  // 10kHz / 10 = 1kHz
static constexpr float   SPD_DT    = SPD_DECIM * FOC_DT;  // 1ms

static constexpr float   OBS_DT = FOC_DT;

/* Speed scaling: internal q24 speed = w[rad/s] / SPD_UNIT, full scale 128*SPD_UNIT = +-2048 rad/s.*/
static constexpr float   SPD_UNIT       = 16.0f;// 2^4 from q7.24 to q11.20
static constexpr q24_t   INV_SPD_UNIT = q24(1.0f / SPD_UNIT);
static constexpr float   MAX_ABS_SPEED  = (float)Q24_MAX * SPD_UNIT;

static constexpr float   POS_UNIT_Q = 1024.0f;
static constexpr float   POS_UNIT = M_TWOPI * POS_UNIT_Q; // 2^10 from q7.24 to q17.14, 2pi for 1.0 representing 1 rev
static constexpr float   INV_POS_UNIT =1.0f / POS_UNIT;
static constexpr float   POS_LOOP_KP_SCALE = 1 / 1024.0f;
static constexpr q24_t   POS_LOOP_ERR_SCALE  = q24(1024.0f);
static constexpr float   MAX_ABS_POS = (float)Q24_MAX * POS_UNIT;
// note: the two 1024.0f(POS_UNIT_Q and POS_LOOP_ERR_SCALE) is different.


static constexpr q24_t K_B_div_J = q24(B / J * FOC_DT);
static constexpr q24_t K_Kt_div_J = q24(K_t / J * FOC_DT / SPD_UNIT);

/** @brief Outer loop mode; CURRENT bypasses the outer loops (iq reference comes from setIqRef). */
enum class FocLoopMode : uint8_t { CURRENT = 0, SPEED, POS };

/**
 * @brief Field-oriented current, speed and position controller for one PMSM.
 * @note  updateCurrentLoop() runs in the ADC ISR, the outer loops run in the TIM2 ISR.
 */
class FocDriver {
public:
    /** @brief Bind the driver to an angle sensor. */
    FocDriver(MT6701_SSI& encoder);

    /** @brief Clear the controllers and let the inverter drive the bridge. */
    void enable() {
        reset();
        m_en = true;
    }
    /** @brief Stop driving and park all three phases at 50% duty. */
    void disable() {
        if (!m_en) return;
        m_en = false;
        reset();
        inv_set_duty(0x8000, 0x8000, 0x8000);
    }

    /** @brief Average the phase-current ADC zero offsets; the bridge must be idle. */
    void calibCurrent();
    /**
     * @brief Force the rotor onto the d-axis and latch the electrical angle offset.
     * @return Electrical angle calibration (rad, q7.24).
     */
    q24_t calibZeroPos();
    /**
     * @brief Latch the current encoder position as the continuous angle origin.
     * @return Raw 14-bit encoder reading used as the new origin.
     */
    uint16_t setContAngZeroOffset() {
        __disable_irq();
        const uint16_t raw = m_encoder.readRaw();
        const int32_t  ang = m_encoder.readAngleTurns() * (1 << 14) + raw;
        __enable_irq();

        m_cont_ang_zero.raw = raw;
        m_cont_ang_zero.ref = ang;
        return raw;
    }
    /** @brief Restore a previously saved continuous angle origin (raw 14-bit reading). */
    void applyContAngZeroOffset(uint16_t offset_raw) {
        __disable_irq();
        const uint16_t raw = m_encoder.readRaw();
        const int32_t  ang = m_encoder.readAngleTurns() * (1 << 14) + raw;
        __enable_irq();

        constexpr int32_t HALF_TURN = 0x2000; constexpr int32_t FULL_TURN = 0x4000;
        m_cont_ang_zero.raw = offset_raw & 0x3FFF;
        int32_t d = (int32_t)raw - (int32_t)m_cont_ang_zero.raw;
        if (d > HALF_TURN) d -= FULL_TURN;
        else if (d < -HALF_TURN) d += FULL_TURN;
        m_cont_ang_zero.ref = ang - d;
    }

    /** @brief Set the DC bus voltage; also refreshes the 1/Vbus scale. */
    void setVbus(q24_t vbus);
    /** @brief Latest DC bus voltage (V, q7.24). */
    q24_t getVbus() const { return m_vbus; }
    /** @brief Switch the outer loop mode; references are re-seeded to the current state. */
    void setLoopMode(FocLoopMode mode);
    /** @brief Active outer loop mode. */
    FocLoopMode getLoopMode() const { return m_loop_mode; }
    /** @brief Set the electrical angle offset (rad, q7.24). */
    void setAngCalib(q24_t calib) { m_ang_calib = calib; }
    /** @brief Latest electrical angle offset (rad, q7.24). */
    q24_t getAngCalib() const { return m_ang_calib; }

    /** @brief Tune the current loop from a target bandwidth (Hz). */
    void setCurrentLoopBandWidth(float bw) {
        if (bw <= 0) return;
        constexpr float k_current = R_s / L_s;
        float kp_iq = M_TWOPI * L_s * bw;;
        float kp_id = kp_iq * 0.8f;
        m_pid_iq.setKp(q24(kp_iq));
        m_pid_iq.setKiDt(q24(kp_iq * k_current * FOC_DT));
        m_pid_id.setKp(q24(kp_id));
        m_pid_id.setKiDt(q24(kp_id * k_current * FOC_DT));
    }
    /** @brief Set the d/q voltage output limits of the current loop (V, q7.24). */
    void setCurrentLoopLimits(q24_t vd_limit_v, q24_t vq_limit_v) {
        m_pid_id.setOutputLimit(vd_limit_v);
        m_pid_iq.setOutputLimit(vq_limit_v);
    }
    /** @brief Set the d-axis current reference (A, q7.24). */
    void setIdRef(q24_t i_d_a) {
        m_pid_id.setTarget(i_d_a); 
    }
    /** @brief Set the q-axis current reference (A, q7.24); saturated to MAX_ABS_CURRENT. */
    void setIqRef(q24_t i_q_a) {
        if (i_q_a > MAX_ABS_CURRENT) i_q_a = MAX_ABS_CURRENT;
        else if (i_q_a < -MAX_ABS_CURRENT) i_q_a = -MAX_ABS_CURRENT;
        m_pid_iq.setTarget(i_q_a);
    }
    /** @brief iq reference (A, q7.24). */
    q24_t getIqRef()    const { return m_pid_iq.getTarget(); }

    /** @brief Set the speed loop bandwidth (Hz), keeping the current damping. */
    void setSpeedLoopBandWidth(float bw) {
        if (bw <= 0) return;
        m_speed_params.bw = bw;
        updateSpeedLoopGains(); 
    }
    /** @brief Set the speed loop bandwidth (Hz) and damping ratio at once. */
    void setSpeedLoopBandWidth(float bw, float zeta) {
        if (bw <= 0 || zeta <= 0) return;
        m_speed_params = {bw, zeta};
        updateSpeedLoopGains();
    }
    /** @brief Set the speed loop damping ratio, keeping the current bandwidth. */
    void setSpeedLoopDamping(float zeta) {
        if (zeta <= 0) return;
        m_speed_params.zeta = zeta;
        updateSpeedLoopGains();
    }
    /** @brief Set the iq output limit of the speed loop (A, q7.24). */
    void setSpeedLoopLimit(q24_t iq_limit_a) {
        if (iq_limit_a < 0) return;
        if (iq_limit_a > MAX_ABS_CURRENT) iq_limit_a = MAX_ABS_CURRENT;
        m_pid_speed.setOutputLimit(iq_limit_a);
    }
    /** @brief Set the speed reference (rad/s); saturated to MAX_ABS_SPEED. */
    void setSpeedRef(float w_rad_s) { 
        if (w_rad_s > MAX_ABS_SPEED) w_rad_s = MAX_ABS_SPEED;
        else if (w_rad_s < -MAX_ABS_SPEED) w_rad_s = -MAX_ABS_SPEED;
        m_pid_speed.setTarget(w_rad_s * INV_SPD_UNIT);
    }
    /** @brief Speed reference (scaled: rad/s = value * SPD_UNIT). */
    q24_t getSpeedRef() const { return m_pid_speed.getTarget(); }

    /** @brief Tune the position loop from a target bandwidth (Hz). */
    void setPosLoopBandWidth(float bw) {
        if (bw <= 0) return;
        float wn_exp_pos = M_TWOPI * bw;
        float kp_pos = wn_exp_pos;
        q24_t kp_pos_scaled = q24(kp_pos * POS_UNIT * POS_LOOP_KP_SCALE / SPD_UNIT);
        m_pid_pos.setKp(kp_pos_scaled);

    }
    /** @brief Set the speed output limit of the position loop (rad/s). */
    void setPosLoopLimit(float w_limit_radps) {
        if (w_limit_radps < 0) return;
        if (w_limit_radps > MAX_ABS_SPEED) w_limit_radps = MAX_ABS_SPEED;
        m_pid_pos.setOutputLimit(q24(w_limit_radps / SPD_UNIT)); // rad/(s*16)
    }
    /** @brief Set the position reference (mechanical rad, counted from the zero offset). */
    void setPosRefFloat(float pos_rad) { 
        m_pos_ref = q24(pos_rad * INV_POS_UNIT);
    }
    /** @brief Set the position reference from raw q17.14 counts. */
    void setPosRefRaw(int32_t pos_raw) {
        m_pos_ref = q24_t::from_raw(pos_raw);
    }
    /** @brief Position reference (q17.14 counts). */
    q24_t getPosRef()   const { return m_pos_ref; }

    /** @brief Set the dq current feedback IIR cutoff frequency (Hz). */
    void setCurrentFiltCutoffFreq(float cutoff_freq) {
        m_current_filt_coeff = q24(1.0f - 1.0f / (1.0f + 2.0f * M_PI * cutoff_freq * FOC_DT));
        if (m_current_filt_coeff < 0) m_current_filt_coeff = Q24_ZERO;
        else if (m_current_filt_coeff > 1) m_current_filt_coeff = Q24_ONE; 
    }
    /** @brief Set the speed feed-forward IIR cutoff frequency (Hz). */
    void setSpeedFFFiltCutoffFreq(float cutoff_freq) {
        m_speed_filt_coeff = q24(1.0f - 1.0f / (1.0f + 2.0f * M_PI * cutoff_freq * FOC_DT));
        if (m_speed_filt_coeff < 0) m_speed_filt_coeff = Q24_ZERO;
        else if (m_speed_filt_coeff > 1) m_speed_filt_coeff = Q24_ONE;
    }
    /** @brief Set the disturbance estimate IIR cutoff frequency (Hz). */
    void setDisturbFiltCutoffFreq(float cutoff_freq) {
        m_disturb_filt_coeff = q24(1.0f - 1.0f / (1.0f + 2.0f * M_PI * cutoff_freq * FOC_DT));
        if (m_disturb_filt_coeff < 0) m_disturb_filt_coeff = Q24_ZERO;
        else if (m_disturb_filt_coeff > 1) m_disturb_filt_coeff = Q24_ONE; 
    }

    /** @brief Place the ESO poles from a target bandwidth (Hz). */
    void setEsoBandWidth(float bw) {
        if (bw <= 0) return;
        float wn_exp_eso = M_TWOPI * bw;
        float l1 = 2.0f * wn_exp_eso; // approximation
        float l2 = wn_exp_eso * wn_exp_eso * J / K_t;
        setEsoCoeffs(l1, l2);
    }
    /** @brief Set the extended state observer gains directly. */
    void setEsoCoeffs(float l1, float l2) {
        m_eso_coeffs.l1 = q24(l1 * FOC_DT);
        m_eso_coeffs.l2 = q24(l2 * SPD_UNIT * FOC_DT);
    }

    /** @brief Set the alpha-beta filter bandwidth (Hz), keeping the current damping. */
    void setSpeedABFiltBandWidth(float bw) {
        if (bw <= 0) return;
        m_abfilt_params.bw = bw;
        updateSpeedABFiltCoeffs();
    }
    /** @brief Set the alpha-beta filter bandwidth (Hz) and damping ratio at once. */
    void setSpeedABFiltBandWidth(float bw, float zeta) {
        if (bw <= 0) return;
        if (zeta < 0) return;
        m_abfilt_params = {bw, zeta};
        updateSpeedABFiltCoeffs();
    }
    /** @brief Set the alpha-beta filter damping ratio, keeping the current bandwidth. */
    void setSpeedABFiltDamping(float zeta) {
        if (zeta < 0) return;
        m_abfilt_params.zeta = zeta;
        updateSpeedABFiltCoeffs();
    }
    /** @brief Set the speed alpha-beta filter coefficients directly. */
    void setSpeedABFiltCoeff(float a, float b) {
        m_abfilt_coeffs.a = q24(a * OBS_DT);
        m_abfilt_coeffs.b = q24(b * OBS_DT / (32.0f * SPD_UNIT));
    }

    /** @brief One current loop step: sample, transform, regulate, modulate (ADC ISR). */
    void updateCurrentLoop();
    /** @brief One speed loop step; no-op in CURRENT mode (TIM2 ISR). */
    void updateSpeedLoop();
    /** @brief One position loop step; no-op outside POS mode (TIM2 ISR). */
    void updatePosLoop();
    /** @brief Apply a phase voltage vector (V, q7.24) through the inverter. */
    void outputVoltage(q24_t u, q24_t v, q24_t w);
    /** @brief Clear every controller and observer state. */
    void reset() {
        __disable_irq();

        m_pid_iq.reset();
        m_pid_id.reset();
        m_pid_speed.reset();
        m_pid_pos.reset();
        
        m_current_loop_cmd = Q24_ZERO;
        m_speed_loop_cmd = Q24_ZERO;

        m_mech_ang_diff = Q24_ZERO;
        m_mech_speed = Q24_ZERO;

        m_eso_states.mech_speed_hat = Q24_ZERO;
        m_eso_states.disturb_hat = Q24_ZERO;

        m_disturbance = Q24_ZERO;

        m_mech_speed_init = false;

        m_i_d_fb_filt = m_i_d_fb;
        m_i_q_fb_filt = m_i_q_fb;
        m_theta_hat = m_mech_ang;

        __enable_irq();
    }

    /** @brief Latest mechanical angle (rad, q7.24, wrapped to [0, 2pi)). */
    q24_t getMechAng() const { return m_mech_ang; }
    /** @brief Latest electrical angle (rad, q7.24). */
    q24_t getElecAng() const { return m_elec_ang; }
    /** @brief Latest mechanical speed (scaled: rad/s = value * SPD_UNIT). */
    q24_t getMechSpeed() const { return m_mech_speed; }

    /* current loop debug */
    /** @brief Phase U current (A, q7.24). */
    q24_t getIu() const { return m_current.u; }
    /** @brief Phase V current (A, q7.24). */
    q24_t getIv() const { return m_current.v; }
    /** @brief Phase W current (A, q7.24), reconstructed from U and V. */
    q24_t getIw() const { return -(m_current.u + m_current.v); }
    /** @brief d-axis current feedback (A, q7.24, filtered). */
    q24_t getId()   const { return m_i_d_fb; }
    /** @brief q-axis current feedback (A, q7.24, filtered). */
    q24_t getIq()   const { return m_i_q_fb; }
    /** @brief d-axis voltage reference (V, q7.24, after decoupling and clamping). */
    q24_t getVdRef() const { return m_v_d_ref; }
    /** @brief q-axis voltage reference (V, q7.24, after decoupling and clamping). */
    q24_t getVqRef() const { return m_v_q_ref; }
    /** @brief Phase U current zero offset (ADC counts, q7.24). */
    q24_t getCurrentCalibU() const { return m_current_calib.u; }
    /** @brief Phase V current zero offset (ADC counts, q7.24). */
    q24_t getCurrentCalibV() const { return m_current_calib.v; }
    /** @brief Alpha-beta filter angle state (rad, q7.24). */
    q24_t getABFiltState1() const { return m_theta_hat; }
    /** @brief Alpha-beta filter speed state (scaled: rad/s = value * SPD_UNIT). */
    q24_t getABFiltState2() const { return m_mech_speed; }
    /** @brief ESO observed speed (scaled: rad/s = value * SPD_UNIT). */
    q24_t getEsoState1() const { return m_eso_states.mech_speed_hat; }
    /** @brief ESO observed disturbance current (A, q7.24). */
    q24_t getEsoState2() const { return m_eso_states.disturb_hat; }

    /* outer loop debug; speed values are scaled: rad/s = cvt2float() * SPD_UNIT */
    /** @brief Speed feedback (scaled: rad/s = value * SPD_UNIT). */
    q24_t getSpeedFb()  const { return m_mech_speed; }
    /** @brief Continuous position feedback (q17.14 counts). */
    q24_t getPosFb()    const { return m_mech_ang_cont; }

    volatile bool m_en = false;
    volatile bool m_calibrating = false;
private:
    MT6701_SSI& m_encoder;

    /* loops: only reachable through the set*Loop* / set*Ref setters above */
    FocPiController m_pid_id;
    FocPiController m_pid_iq;
    FocPiController m_pid_speed;
    FocPiController m_pid_pos;

    struct {
        float bw;
        float zeta;
    } m_speed_params = {0.0f, 1.0f};
    struct {
        float bw;
        float zeta;
    } m_abfilt_params = {0.0f, 0.9f};

    struct AbfiltCoeff {
        q24_t a;
        q24_t b;
    } m_abfilt_coeffs = {Q24_ZERO, Q24_ZERO};
    struct EsoCoeff {
        q24_t l1;
        q24_t l2;
    } m_eso_coeffs = {Q24_ZERO, Q24_ZERO};
    struct EsoState {
        q24_t mech_speed_hat;  // scaled: w / SPD_UNIT
        q24_t disturb_hat;  // A
    } m_eso_states = {Q24_ZERO, Q24_ZERO};

    q24_t m_current_filt_coeff = q24(0.2f);
    q24_t m_speed_filt_coeff   = q24(0.15f);
    q24_t m_disturb_filt_coeff = q24(0.5f);

    q24_t m_vbus = Q24_ZERO;
    q24_t m_inv_vbus = Q24_MAX;

    struct {
        q24_t u = Q24_ZERO;
        q24_t v = Q24_ZERO;
    } m_current;
    struct {
        q24_t u = CURRENT_CALIB_DEFAULT;
        q24_t v = CURRENT_CALIB_DEFAULT;
    } m_current_calib;
    q24_t m_ang_calib = Q24_ZERO;

    q24_t m_i_d_fb  = Q24_ZERO;   // d-axis current feedback (A, after the IIR filter)
    q24_t m_i_q_fb  = Q24_ZERO;   // q-axis current feedback (A, after the IIR filter)
    q24_t m_v_d_ref = Q24_ZERO;   // d-axis voltage reference (V)
    q24_t m_v_q_ref = Q24_ZERO;   // q-axis voltage reference (V)
    // dq current feedback first-order IIR low-pass state (updated in the ISR)
    q24_t m_i_d_fb_filt = Q24_ZERO;
    q24_t m_i_q_fb_filt = Q24_ZERO;

    q24_t m_mech_ang = Q24_ZERO;
    int32_t m_turns = 0;
    q24_t m_elec_ang = Q24_ZERO;
    q24_t m_mech_ang_diff = Q24_ZERO;
    q24_t m_mech_speed = Q24_ZERO;  // scaled: w / SPD_UNIT
    q24_t m_disturbance = Q24_ZERO;

    q24_t  m_mech_ang_last  = Q24_ZERO;    // mechanical angle of the previous tick (delta reference)
    bool   m_mech_speed_init = false;
    q24_t m_theta_hat = Q24_ZERO;

    volatile q24_t m_current_loop_cmd = Q24_ZERO;
    volatile q24_t m_speed_loop_cmd = Q24_ZERO;  // scaled: w / SPD_UNIT

    /* ---- outer loop state ---- */
    FocLoopMode m_loop_mode   = FocLoopMode::CURRENT;
    q24_t m_mech_ang_cont     = Q24_ZERO;
    q24_t m_pos_ref           = Q24_ZERO;
    struct {
        uint16_t raw;
        int32_t  ref;   /* q17.14 continuous angle (m_turns*16384 + raw), may be negative */
    } m_cont_ang_zero = {0, 0};

    void updateSpeedLoopGains() {
        float wn = M_TWOPI * m_speed_params.bw;
        m_pid_speed.setKp(q24(2.0f * m_speed_params.zeta * wn * J / K_t * SPD_UNIT));
        m_pid_speed.setKiDt(q24(wn * wn * J / K_t * SPD_UNIT * SPD_DT));
    }
    void updateSpeedABFiltCoeffs() {
        float wn = M_TWOPI * m_abfilt_params.bw;
        setSpeedABFiltCoeff(2.0f * wn * m_abfilt_params.zeta, wn * wn);
    }
    inline void readMechAngFromEncoder() { 
        m_mech_ang = m_encoder.readAngleRad();

        uint16_t mech_ang_raw = m_encoder.readRaw();
        m_turns = m_encoder.readAngleTurns();

        m_mech_ang_cont = q24_t::from_raw((m_turns * (1 << 14) | mech_ang_raw) - m_cont_ang_zero.ref);
        // q17.14(2pi*rad) reinterpret as q24_t for calc
    };
    void updateMechSpeed();
    void observeMechSpeed();
    void observeDisturb(q24_t mech_speed_meas);
};
