#pragma once

#include "smath.h"

class FirstOrderLowPassFilter
{
public:
    FirstOrderLowPassFilter(float cutoff_freq, float initial_value = 0.0f){
        setCutoffFreq(cutoff_freq);
        prev_out_ = initial_value;
    }

    void setCutoffFreq(float cutoff_freq){
        float cutoff_freq_ = cutoff_freq;
        if(cutoff_freq <= 0.0f || cutoff_freq > 100.0f) {
            cutoff_freq_ = 0.0f; // Disable filtering
        } else {
            cutoff_freq_ = cutoff_freq;
        }
        tau = 1.0f / (2.0f * M_PI * cutoff_freq_);
    }
    void reset(float value = 0.0f){
        prev_out_ = value;
    }
    float update(float in, float dt){
        float alpha = dt / (dt + this->tau);
        prev_out_ += alpha * (in - prev_out_);
        return prev_out_;
    }
private:
    float tau;
    float prev_out_;
};