#include "foc_driver.h"

FocDriver::FocDriver(MT6701_SSI& encoder)
 : m_encoder(encoder)
 , m_pid_id()
 , m_pid_iq()
 , m_pid_speed()
 , m_pid_pos() {

}

void FocDriver::calibCurrent() {
    const int samples = 32;
    uint16_t iu_adc, iv_adc;
    this->m_current_calib.u = q24(0.0f);
    this->m_current_calib.v = q24(0.0f);
    for (int i = 0; i < samples; i++) {
        uint64_t t0 = Systick_getUsTick();
        while (Systick_getUsTick() - t0 < (uint64_t)120u) {}
        iu_adc = ADC_GetInjectedConversionValue(ADC1, ADC_InjectedChannel_1);
        iv_adc = ADC_GetInjectedConversionValue(ADC1, ADC_InjectedChannel_2);

        this->m_current_calib.u += q24_t::from_raw(iu_adc << 12);
        this->m_current_calib.v += q24_t::from_raw(iv_adc << 12);
    }
    this->m_current_calib.u /= samples;
    this->m_current_calib.v /= samples;
}

q24_t FocDriver::calibZeroPos() {
    m_calibrating = true;
    bool en_bak = m_en;
    if (!en_bak) enable();

    if (m_vbus == 0) {
        m_calibrating = false;
        if (!en_bak) disable();
        return Q24_ZERO;
    }
    outputVoltage(q24(0.0f), q24(0.0f), q24(0.0f));
    delay_ms_nortos(100);
    q24_t v_align = 0.15f * m_vbus;
    outputVoltage(v_align, -v_align / 2, -v_align / 2);
    delay_ms_nortos(1000);
    readMechAngFromEncoder();
    this->m_ang_calib = this->m_mech_ang * POLE_PAIRS;

    m_pid_id.reset();
    m_pid_iq.reset();
    m_calibrating = false;
    if (!en_bak) disable();

    return this->m_ang_calib;
};

void FocDriver::setVbus(q24_t vbus) {
    m_vbus = vbus;
    m_inv_vbus = Q24_ONE / vbus;
}

void FocDriver::setLoopMode(FocLoopMode mode) {
    const bool en_bak = m_en;
    m_en = false;

    __disable_irq();

    reset();

    switch (mode) {
        case FocLoopMode::CURRENT:
            m_pid_iq.setTarget(m_i_q_fb_filt);
            m_pid_id.setTarget(Q24_ZERO);
            break;
        case FocLoopMode::SPEED:
            m_pid_speed.setTarget(m_mech_speed);
            break;
        case FocLoopMode::POS:
            m_pos_ref = m_mech_ang_cont;
            m_pid_speed.setTarget(Q24_ZERO);
            break;
        default:
            break;
    }

    this->m_loop_mode = mode;

    if (en_bak) m_en = true;

    __enable_irq();
}

void FocDriver::updateCurrentLoop() {
    uint16_t iu_adc = ADC_GetInjectedConversionValue(ADC1, ADC_InjectedChannel_1);
    uint16_t iv_adc = ADC_GetInjectedConversionValue(ADC1, ADC_InjectedChannel_2);
    this->m_current.u = -q24_mul_w(q24_sub_w(q24_t::from_raw(iu_adc << 12), m_current_calib.u), CURRENT_SCALE);   // remove the calibrated zero offset
    this->m_current.v = -q24_mul_w(q24_sub_w(q24_t::from_raw(iv_adc << 12), m_current_calib.v), CURRENT_SCALE);
    /* i_w = -(i_u+i_v) is eliminated */

    q24_t i_alpha_fb, i_beta_fb;
    clarke(m_current.u, m_current.v, &i_alpha_fb, &i_beta_fb);
    
    readMechAngFromEncoder();

    // this->m_elec_ang = - q24_sub_w(m_mech_ang * POLE_PAIRS, this->m_ang_calib);
    this->m_elec_ang = q24_sub_w(q24_mul_w(m_mech_ang, q24(POLE_PAIRS)), this->m_ang_calib);

    updateMechSpeed();
    

    q24_t c, s;
    q_sincos(this->m_elec_ang, &s, &c);
    q24_t i_d_fb, i_q_fb;
    park(i_alpha_fb, i_beta_fb, c, s, &i_d_fb, &i_q_fb);

    this->m_i_d_fb_filt = q24_add_w(this->m_i_d_fb_filt, q24_mul_w(q24_sub_w(i_d_fb, this->m_i_d_fb_filt), m_current_filt_coeff));
    this->m_i_q_fb_filt = q24_add_w(this->m_i_q_fb_filt, q24_mul_w(q24_sub_w(i_q_fb, this->m_i_q_fb_filt), m_current_filt_coeff));

    q24_t v_d_ref = (q24_t)this->m_pid_id.getOutput(this->m_i_d_fb_filt);

    if (this->m_loop_mode != FocLoopMode::CURRENT) {
        this->m_pid_iq.setTarget(m_current_loop_cmd);
    }
    q24_t v_q_ref = (q24_t)this->m_pid_iq.getOutput(this->m_i_q_fb_filt);

    /* feedforward decoupling */
    q24_t elec_diff = q24_mul_w(this->m_mech_ang_diff, q24(POLE_PAIRS));
    q24_t w_e_L = q24_mul_w(elec_diff, K_L);
    v_d_ref = q24_sub_w(v_d_ref, q24_mul_w(w_e_L, this->m_i_q_fb_filt));
    v_q_ref = q24_add_w(v_q_ref, q24_mul_w(w_e_L, this->m_i_d_fb_filt));

    /* clamp again after decoupling */
    m_pid_id.clamp(&v_d_ref);
    m_pid_iq.clamp(&v_q_ref);

    /* for debug */
    this->m_i_d_fb  = this->m_i_d_fb_filt;
    this->m_i_q_fb  = this->m_i_q_fb_filt;
    this->m_v_d_ref = v_d_ref;
    this->m_v_q_ref = v_q_ref;

    q24_t v_alpha_ref, v_beta_ref;
    inv_park(v_d_ref, v_q_ref, c, s, &v_alpha_ref, &v_beta_ref);

    q24_t v_u_ref, v_v_ref, v_w_ref;
    inv_clarke(v_alpha_ref, v_beta_ref, &v_u_ref, &v_v_ref, &v_w_ref);

    outputVoltage(v_u_ref, v_v_ref, v_w_ref);
}

void FocDriver::updateSpeedLoop() {
    if (m_loop_mode == FocLoopMode::CURRENT) return;
    if (m_loop_mode == FocLoopMode::POS) {
        m_pid_speed.setTarget(m_speed_loop_cmd);
    }

    q24_t current_loop_cmd_raw = m_pid_speed.getOutput(m_mech_speed) - m_disturbance;
    // q24_t current_loop_cmd_raw = m_pid_speed.getOutput(m_mech_speed);
    m_pid_speed.clamp(&current_loop_cmd_raw);
    this->m_current_loop_cmd = current_loop_cmd_raw;
}

void FocDriver::updatePosLoop() {
    if (m_loop_mode != FocLoopMode::POS) return;


    q24_t err = q24_sub_w(m_pos_ref, m_mech_ang_cont);
    // if      (err >  Q24_PI) err = q24_sub_w(err, Q24_TWOPI);
    // else if (err < -Q24_PI) err = q24_add_w(err, Q24_TWOPI);
    err *= POS_LOOP_ERR_SCALE;

    q24_t out = m_pid_pos.getOutputErr(err); 

    //kp_pos * POS_UNIT * (1/1024) / SPD_UNIT * err / POS_UNIT * 1024 = kp_pos * err / SPD_UNIT

    this->m_speed_loop_cmd = out;
}

void FocDriver::outputVoltage(q24_t u, q24_t v, q24_t w) {
    constexpr int FOC_DUTY_MIN  =     0x0CCD;  // ~5%
    constexpr int FOC_DUTY_MAX  =     0xF333;  // ~95%
    int32_t du = ((int32_t)((u * m_inv_vbus).val >> 8) + 0x8000);
    int32_t dv = ((int32_t)((v * m_inv_vbus).val >> 8) + 0x8000);
    int32_t dw = ((int32_t)((w * m_inv_vbus).val >> 8) + 0x8000);
    du = (du < FOC_DUTY_MIN) ? FOC_DUTY_MIN : (du > FOC_DUTY_MAX) ? FOC_DUTY_MAX : du;
    dv = (dv < FOC_DUTY_MIN) ? FOC_DUTY_MIN : (dv > FOC_DUTY_MAX) ? FOC_DUTY_MAX : dv;
    dw = (dw < FOC_DUTY_MIN) ? FOC_DUTY_MIN : (dw > FOC_DUTY_MAX) ? FOC_DUTY_MAX : dw;
    if (m_en) inv_set_duty((uint16_t)du, (uint16_t)dv, (uint16_t)dw);
    else     inv_set_duty(0x8000, 0x8000, 0x8000);
}

/**
 * @brief Update the mechanical angle difference, the disturbance estimate and the mechanical speed.
 */
void FocDriver::updateMechSpeed() {
    if (!m_mech_speed_init) { 
        m_mech_speed_init = true;
        m_mech_ang_last  = m_mech_ang;
        m_eso_states.mech_speed_hat = m_mech_speed;
        m_theta_hat = m_mech_ang;
        return;
    }

    q24_t mech_ang_diff_raw = q24_sub_w(m_mech_ang, m_mech_ang_last);
    m_mech_ang_last = m_mech_ang;
    if      (mech_ang_diff_raw >  Q24_PI) mech_ang_diff_raw = q24_sub_w(mech_ang_diff_raw, Q24_TWOPI);
    else if (mech_ang_diff_raw < -Q24_PI) mech_ang_diff_raw = q24_add_w(mech_ang_diff_raw, Q24_TWOPI);
    this->m_mech_ang_diff = q24_add_w(this->m_mech_ang_diff,
                            q24_mul_w(q24_sub_w(mech_ang_diff_raw, this->m_mech_ang_diff), m_speed_filt_coeff));

    q24_t mech_speed_ff = m_mech_ang_diff / q24(FOC_DT * SPD_UNIT);  // rad per tick -> scaled speed
    observeDisturb(mech_speed_ff);

    observeMechSpeed();
}

void FocDriver::observeMechSpeed() {
    q24_t err = q24_sub_w(m_mech_ang, m_theta_hat);
    if      (err >  Q24_PI) err = q24_sub_w(err, Q24_TWOPI);
    else if (err < -Q24_PI) err = q24_add_w(err, Q24_TWOPI);
    q24_t err_scaled = q24_t::from_raw(err.val * 32);

    q24_t d_theta_hat = q24_add_w(q24_mul_w(m_mech_speed, q24(OBS_DT * SPD_UNIT)), q24_mul_w(m_abfilt_coeffs.a, err));  // scaled speed -> rad per tick
    q24_t d_omega_hat = q24_mul_w(m_abfilt_coeffs.b, err_scaled);
    m_theta_hat = q24_add_w(m_theta_hat, d_theta_hat);
    if      (m_theta_hat > Q24_TWOPI) m_theta_hat = q24_sub_w(m_theta_hat, Q24_TWOPI);
    else if (m_theta_hat < Q24_ZERO) m_theta_hat = q24_add_w(m_theta_hat, Q24_TWOPI);
    m_mech_speed = q24_add_w(m_mech_speed, d_omega_hat);
}

/**
 * x_1_hat = w, x_2_hat = d_hat
 * J/Kt * dw/dt = -B/Kt * w + iq + d, let J' = J/Kt, B' = B/Kt, then
 * dw / dt = -B'/J' * w + 1/J' * iq - d/J' = Kt/J * (iq + d) + (-B/J) * w
 * assume dd/dt = 0.
 * dt is scaled to the right side so that q24_t can fit in
*/
void FocDriver::observeDisturb(q24_t mech_speed_meas) {
    q24_t err = q24_sub_w(mech_speed_meas, m_eso_states.mech_speed_hat);
    q24_t d_mech_speed = q24_add_w(
            q24_sub_w(q24_mul_w(K_Kt_div_J, q24_add_w(m_i_q_fb_filt, m_eso_states.disturb_hat)),
                    q24_mul_w(K_B_div_J, m_eso_states.mech_speed_hat)),
            q24_mul_w(m_eso_coeffs.l1, err));
    q24_t d_distur = q24_mul_w(m_eso_coeffs.l2, err);

    m_eso_states.mech_speed_hat = q24_add_w(m_eso_states.mech_speed_hat, d_mech_speed);
    m_eso_states.disturb_hat = q24_add_w(m_eso_states.disturb_hat, d_distur);
    
    m_disturbance = q24_add_w(m_disturbance, 
        q24_mul_w(m_disturb_filt_coeff, q24_sub_w(m_eso_states.disturb_hat, m_disturbance))
    );
}