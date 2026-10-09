#include "foc_driver.h"
#include "alg_pid.h"

FocDriver::FocDriver(MT6701_SSI& enc)
 : encoder(enc)
 , pid_id()
 , pid_iq()
 , pid_spd()
 , pid_pos()
{
    pid_pos.setDt((float)SPD_DT * 2.0f);
    pid_pos.setDiffMode(DiffMode::DisableDiff);
}

void FocDriver::outputVoltage(q24_t u, q24_t v, q24_t w){
    constexpr int FOC_DUTY_MIN  =     0x0CCD;   /* ~5%  */
    constexpr int FOC_DUTY_MAX  =     0xF333;  /* ~95% */
    int32_t du = ((int32_t)((u * inv_vbus).val >> 8) + 0x8000);
    int32_t dv = ((int32_t)((v * inv_vbus).val >> 8) + 0x8000);
    int32_t dw = ((int32_t)((w * inv_vbus).val >> 8) + 0x8000);
    du = (du < FOC_DUTY_MIN) ? FOC_DUTY_MIN : (du > FOC_DUTY_MAX) ? FOC_DUTY_MAX : du;
    dv = (dv < FOC_DUTY_MIN) ? FOC_DUTY_MIN : (dv > FOC_DUTY_MAX) ? FOC_DUTY_MAX : dv;
    dw = (dw < FOC_DUTY_MIN) ? FOC_DUTY_MIN : (dw > FOC_DUTY_MAX) ? FOC_DUTY_MAX : dw;
    inv_set_duty((uint16_t)du, (uint16_t)dv, (uint16_t)dw);
}

void FocDriver::updateCurrLoop(){
    if (calibrating) return; 
    uint16_t iu_adc = ADC_GetInjectedConversionValue(ADC1, ADC_InjectedChannel_1);
    uint16_t iv_adc = ADC_GetInjectedConversionValue(ADC1, ADC_InjectedChannel_2);
    this->current.u = -q24_mul_w(q24_sub_w(q24_t::from_raw(iu_adc << 12), current_calib.u), CURRENT_SCALE);   /* 减校准零偏 */
    this->current.v = -q24_mul_w(q24_sub_w(q24_t::from_raw(iv_adc << 12), current_calib.v), CURRENT_SCALE);
    /* i_w = -(i_u+i_v) is eliminated */

    q24_t i_alpha_fb, i_beta_fb;
    clarke(current.u, current.v, &i_alpha_fb, &i_beta_fb);
    
    readMechanAngFromEnc();

    // this->elec_ang = - q24_sub_w(mech_ang * POLE_PAIRS, this->ang_calib);
    this->elec_ang = q24_sub_w(q24_mul_w(mech_ang, q24(POLE_PAIRS)), this->ang_calib);

    updateMechSpeed();

    q24_t c, s;
    q_sincos(this->elec_ang, &s, &c);
    q24_t i_d_fb, i_q_fb;
    park(i_alpha_fb, i_beta_fb, c, s, &i_d_fb, &i_q_fb);

    this->i_d_fb_filt_ = q24_add_w(this->i_d_fb_filt_, q24_mul_w(q24_sub_w(i_d_fb, this->i_d_fb_filt_), current_filt_coeff_));
    this->i_q_fb_filt_ = q24_add_w(this->i_q_fb_filt_, q24_mul_w(q24_sub_w(i_q_fb, this->i_q_fb_filt_), current_filt_coeff_));

    q24_t v_d_ref = (q24_t)this->pid_id.getOutput(this->i_d_fb_filt_);

    if (this->loop_mode_ != FocLoopMode::Current) {
        this->pid_iq.setTarget(current_loop_cmd);
    }
    q24_t v_q_ref = (q24_t)this->pid_iq.getOutput(this->i_q_fb_filt_);

    /* forward */
    q24_t elec_diff = q24_mul_w(this->mech_ang_diff, q24(POLE_PAIRS));
    q24_t w_e_L = q24_mul_w(elec_diff, K_L);
    v_d_ref = q24_sub_w(v_d_ref, q24_mul_w(w_e_L, this->i_q_fb_filt_));
    v_q_ref = q24_add_w(v_q_ref, q24_mul_w(w_e_L, this->i_d_fb_filt_));

    /* clamp again the forward */
    pid_id.clamp(&v_d_ref);
    pid_iq.clamp(&v_q_ref);

    /* for debug */
    this->i_d_fb_  = this->i_d_fb_filt_;
    this->i_q_fb_  = this->i_q_fb_filt_;
    this->v_d_ref_ = v_d_ref;
    this->v_q_ref_ = v_q_ref;

    q24_t v_alpha_ref, v_beta_ref;
    inv_park(v_d_ref, v_q_ref, c, s, &v_alpha_ref, &v_beta_ref);

    q24_t v_u_ref, v_v_ref, v_w_ref;
    inv_clarke(v_alpha_ref, v_beta_ref, &v_u_ref, &v_v_ref, &v_w_ref);

    outputVoltage(v_u_ref, v_v_ref, v_w_ref);
}

/**
 * @brief this function updates mechanical angle difference and mechanical speed.
 */
void FocDriver::updateMechSpeed(){
    if (!mech_spd_init_) { 
        mech_spd_init_ = true;
        mech_ang_last_  = mech_ang;
        return;
    }

    q24_t mech_ang_diff_raw = q24_sub_w(mech_ang, mech_ang_last_);
    mech_ang_last_ = mech_ang;
    if      (mech_ang_diff_raw >  Q24_PI) mech_ang_diff_raw = q24_sub_w(mech_ang_diff_raw, Q24_TWOPI);
    else if (mech_ang_diff_raw < -Q24_PI) mech_ang_diff_raw = q24_add_w(mech_ang_diff_raw, Q24_TWOPI);
    this->mech_ang_diff = q24_add_w(this->mech_ang_diff,
                            q24_mul_w(q24_sub_w(mech_ang_diff_raw, this->mech_ang_diff), speed_filt_coeff_));

    // this->mech_speed = mech_ang_diff * FOC_DT_INV;
    eso(this->mech_ang_diff / q24(FOC_DT));
}
/**
 * x_1_hat = w, x_2_hat = d_hat
 * J/Kt * dw/dt = -B/Kt * w + iq + d, let J' = J/Kt, B' = B/Kt, then
 * dw / dt = -B'/J' * w + 1/J' * iq - d/J' = Kt/J * (iq + d) + (-B/J) * w
 * assume dd/dt = 0.
 * dt is scaled to the right side so that q24_t can fit in
*/
void FocDriver::eso(q24_t mech_speed_meas){
    q24_t d_mech_speed = K_Kt_div_J * (i_q_fb_filt_ + disturbance) - K_B_div_J * mech_speed + eso_coeffs.l1 * (mech_speed_meas - mech_speed);
    q24_t d_distur = eso_coeffs.l2 * (mech_speed_meas - mech_speed);

    mech_speed = q24_add_w(mech_speed, d_mech_speed);
    disturbance = q24_add_w(disturbance, d_distur);
}

void FocDriver::updateSpeedLoop(){
    if (calibrating) return;
    if (loop_mode_ == FocLoopMode::Current) return;
    if (loop_mode_ == FocLoopMode::Pos){
        pid_spd.setTarget(speed_loop_cmd);
    }
    q24_t current_loop_cmd_raw = pid_spd.getOutput(mech_speed) - disturbance;
    pid_spd.clamp(&current_loop_cmd_raw);
    this->current_loop_cmd = current_loop_cmd_raw;
}

void FocDriver::updatePosLoop(){
    if (calibrating) return;
    if (loop_mode_ != FocLoopMode::Pos) return;
    int32_t turns = this->encoder.readAngleTurns();
    float pos_fb = (float)turns * M_TWOPI + this->mech_ang.cvt2float();
    //for debug
    this->mech_ang_cont_ = q24(pos_fb);

    this->speed_loop_cmd = q24(pid_pos.getOutput(pos_fb));
}

void FocDriver::setLoopMode(FocLoopMode mode){
    this->loop_mode_ = mode;
    if (mode == FocLoopMode::Current) {
        this->pid_spd.reset();
        this->pid_pos.reset();
    }
    else if (mode == FocLoopMode::Speed) {
        this->pid_pos.reset();
    }
}

void FocDriver::calibCurrent(){
    const int samples = 32;
    uint16_t iu_adc, iv_adc;
    this->current_calib.u = q24(0.0f);
    this->current_calib.v = q24(0.0f);
    for(int i = 0; i < samples; i++){
        uint64_t t0 = Systick_getUsTick();
        while(Systick_getUsTick() - t0 < (uint64_t)120u) {}
        iu_adc = ADC_GetInjectedConversionValue(ADC1, ADC_InjectedChannel_1);
        iv_adc = ADC_GetInjectedConversionValue(ADC1, ADC_InjectedChannel_2);

        this->current_calib.u += q24_t::from_raw(iu_adc << 12);
        this->current_calib.v += q24_t::from_raw(iv_adc << 12);
    }
    this->current_calib.u /= samples;
    this->current_calib.v /= samples;
}

void FocDriver::setVbus(q24_t vbus){
    if (!initialized) initialized = true;
    this->vbus = vbus;
    this->inv_vbus = Q24_ONE / vbus;
}

q24_t FocDriver::calibZeroPos(){
    if (vbus == 0) return Q24_ZERO;
    outputVoltage(q24(0.0f), q24(0.0f), q24(0.0f));
    delay_ms_nortos(100);
    q24_t v_align = 0.15f * vbus;
    outputVoltage(v_align, -v_align / 2, -v_align / 2);
    delay_ms_nortos(1000);
    readMechanAngFromEnc();
    this->ang_calib = this->mech_ang * POLE_PAIRS;
    return this->ang_calib;
};
