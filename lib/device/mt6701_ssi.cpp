#include "mt6701_ssi.h"
#include "iqmath.h"

// ==================== Construction ====================
MT6701_SSI::MT6701_SSI(SimuSSI<24>& ssi)
    : m_ssi(ssi)
{
}

bool MT6701_SSI::init(){
    return update_sync();
}

// ==================== raw -> radians ====================
q24_t MT6701_SSI::rawToRad(uint32_t raw)
{
    q24_t angle = q24_t::from_raw(raw << 10) * Q24_TWOPI;
    if (angle < q24(0.0f)) {
        angle += q24(2.0f * M_PI);
    } else if (angle >= q24(2.0f * M_PI)) {
        angle -= q24(2.0f * M_PI);
    }
    return angle;
}

// ==================== Synchronous Updater ====================
bool MT6701_SSI::update_sync(uint32_t timeout_ms)
{
    const uint32_t start_ms = Systick_getMsTick();
    if(!m_ssi.ssi_busy){
        m_ssi.start();
    }
    while(m_ssi.ssi_busy && (Systick_getMsTick() - start_ms) < timeout_ms){}
    if(m_ssi.ssi_busy){
        m_ssi.abort();
        return false;
    }
    this->raw_ = (uint16_t)(m_ssi.ssi_raw_data >> 10) & 0x3FFFu;
    const uint16_t raw_ang = (uint16_t)(raw_);
    this->rad_ = rawToRad(raw_ang);

    if ((int32_t)(raw_ang - prev_raw_) > (int32_t)(MT6701_RAW_MAX / 2)) {
        turn_cnt_--;
    } else if ((int32_t)(raw_ang - prev_raw_) < -(int32_t)(MT6701_RAW_MAX / 2)) {
        turn_cnt_++;
    }
    prev_raw_ = raw_ang;

    return true;
}

// ==================== Asynchronous Updater ====================
bool MT6701_SSI::update_async()
{
    if(first_async){
        first_async = false;
        m_ssi.start();
    }
    if(!m_ssi.ssi_busy){
        this->raw_ = (uint16_t)(m_ssi.ssi_raw_data >> 10) & 0x3FFFu;
        const uint16_t raw_ang = (uint16_t)(this->raw_);
        this->rad_ = rawToRad(raw_ang);

        if ((int32_t)(raw_ang - prev_raw_) > (int32_t)(MT6701_RAW_MAX / 2)) {
            turn_cnt_--;
        } else if ((int32_t)(raw_ang - prev_raw_) < -(int32_t)(MT6701_RAW_MAX / 2)) {
            turn_cnt_++;
        }
        prev_raw_ = raw_ang;

        m_ssi.start();
        
        return true;
    }
    return false;
}
