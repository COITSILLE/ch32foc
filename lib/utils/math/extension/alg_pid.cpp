#include "alg_pid.h"
#include "iqmath.h"
#include <stdint.h>

#ifdef __cplusplus

template<typename T>
static inline T assert_num(T x){ return (x == x) ? x : T(0); }

template<typename T>
PID<T>::PID() : 
    last_time(0),
    last_err(T(0.0f)),
    last_err_i(T(0.0f)),
    last_sp(T(0.0f)),
    last_output_(T(0.0f)),
    kp(T(0.0f)),
    ki(T(0.0f)),
    kd(T(0.0f)),
    max_output(T(0.0f)),
    min_output(T(0.0f)),
    ki_limit(T(0.0f)),
    sp(T(0.0f)),
    kv_ff_(T(0.0f)),
    ka_ff_(T(0.0f))
{}

template<typename T>
T PID<T>::clampIntegral(T err_i){
    if (err_i > this->ki_limit || err_i < -this->ki_limit){
        err_i = this->ki_limit * (err_i > 0 ? 1 : -1);
    }
    return err_i;
}

template<typename T>
T PID<T>::clampOutput(T res){
    if (res > this->max_output){
        return this->max_output;
    }
    else if (res < this->min_output){
        return this->min_output;
    }
    return res;
}

template<typename T>
void PID<T>::setTarget(T sp){
    T delta = ABS(sp - this->sp);
    this->last_sp = this->sp;
    this->sp = assert_num(sp);
    if (integral_sep_threshold > T(0.0f) && delta > integral_sep_threshold) {
        this->last_err_i = T(0.0f);
    }
}

template<typename T>
void PID<T>::setFactors(T kp, T ki, T kd, T kfv, T kfa){
    this->kp = assert_num(kp);
    this->ki = assert_num(ki);
    this->kd = assert_num(kd);
    this->kv_ff_ = assert_num(kfv);
    this->ka_ff_ = assert_num(kfa);
}

template<typename T>
void PID<T>::setLimit(T output_limit, T ki_limit){
    this->max_output = assert_num(output_limit);
    this->min_output = -assert_num(output_limit);
    this->ki_limit = assert_num(ki_limit);
}
template<typename T>
void PID<T>::setLimit(T max_output, T min_output, T ki_limit){
    this->max_output = assert_num(max_output);
    this->min_output = assert_num(min_output);
    this->ki_limit = assert_num(ki_limit);
}

template<typename T>
void PID<T>::setDt(T dt){
    this->dt_ = assert_num(dt);
}

template<typename T>
void PID<T>::setDiffMode(DiffMode mode){
    this->mode = mode;
}

template<typename T>
void PID<T>::setDiffTau(T tau){
    this->diff_tau = assert_num(tau);
}

template<typename T>
void PID<T>::setIntegralSepThreshold(T threshold){
    this->integral_sep_threshold = assert_num(threshold);
}

template<typename T>
T PID<T>::getOutput(T fb){
    T dt;
    if(this->dt_ == T(0.0f)){
        uint64_t time_now = Systick_getUsTick();
        dt = T((float)(time_now - last_time) * 1.0e-6f);
        if (dt <= T(0.0f)){
            dt = T(1.0e-6f);
        }
        this->last_time = time_now;
    }
    else{
        dt = this->dt_;
    }
    T err = this->sp - fb;

    T err_i = this->last_err_i;
    if (integral_sep_threshold <= T(0.0f) || ABS(err) <= integral_sep_threshold) {
        err_i += (err + this->last_err) * dt * T(0.5f);
    }
    err_i = this->clampIntegral(err_i);

    T deriv = T(0.0f);
    switch (this->mode) {
    case DiffMode::Standard:
        deriv = this->kd * (err - this->last_err) / dt;
        break;

    case DiffMode::IncompDiff: {
        T d_err = err - this->last_err;
        T alpha = dt / (this->diff_tau + dt);
        this->last_err_d = alpha * (d_err / dt - this->last_err_d) + this->last_err_d;
        deriv = this->kd * this->last_err_d;
        break;
    }

    case DiffMode::DiffOnFb: {
        T last_fb = this->last_sp - this->last_err;
        T fb_d = (fb - last_fb) / dt;
        deriv = -this->kd * fb_d;
        break;
    }

    case DiffMode::DisableDiff:
        deriv = T(0.0f);
        break;
    }

    // ── 更新状态 ──
    this->last_err = err;
    this->last_err_i = err_i;

    T ff = T(0.0f);
    if (this->kv_ff_ != T(0.0f)) ff += this->kv_ff_ * this->sp;
    if (this->ka_ff_ != T(0.0f)) ff += this->ka_ff_ * (this->sp - this->last_sp) / dt;
    last_output_ = this->clampOutput(this->kp * err + deriv + this->ki * err_i) + ff;
    return last_output_;
}

template<typename T>
T PID<T>::getOutput(T fb, T external_fb_d){
    uint64_t time_now = Systick_getUsTick();
    T dt = T((float)(time_now - last_time) * 1.0e-6f);
    if (dt <= T(0.0f)){
        dt = T(1.0e-6f);
    }
    T err = this->sp - fb;

    T err_i = this->last_err_i;
    if (integral_sep_threshold <= T(0.0f) || ABS(err) <= integral_sep_threshold) {
        err_i += (err + this->last_err) * dt * T(0.5f);
    }
    err_i = this->clampIntegral(err_i);

    T deriv = -this->kd * external_fb_d;

    this->last_err = err;
    this->last_err_i = err_i;
    this->last_time = time_now;

    T ff = T(0.0f);
    if (this->kv_ff_ != T(0.0f)) ff += this->kv_ff_ * this->sp;
    if (this->ka_ff_ != T(0.0f)) ff += this->ka_ff_ * (this->sp - this->last_sp) / dt;
    last_output_ = this->clampOutput(this->kp * err + deriv + this->ki * err_i) + ff;
    return last_output_;
}

template<typename T>
void PID<T>::setFeedforward(T kv, T ka){
    this->kv_ff_ = assert_num(kv);
    this->ka_ff_ = assert_num(ka);
}

template<typename T>
void PID<T>::reset(){
    this->last_time = Systick_getUsTick();
    this->last_err = T(0.0f);
    this->last_err_i = T(0.0f);
    this->last_err_d = T(0.0f);
    this->last_sp = T(0.0f);
}

template class PID<float>;
template class PID<q24_t>;

#endif