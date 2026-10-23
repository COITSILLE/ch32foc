#pragma once

#include "simu_ssi.h"
#include "iqmath.h"
#include "smath.h"
#include "systick_time.h"
#include <stdint.h>

#define MT6701_RAW_MAX        16384  ///< 2^14 LSB per revolution

class MT6701_SSI
{
public:
    /* ---- Construction & init ---- */

    explicit MT6701_SSI(SimuSSI<24>& ssi);

    /**
     * @brief Carry out a update_sync() to get the first sample.
     * @note  This function must be called after the initialization of SimuSSI and peripherals.
    */
    bool init();

    /* ---- Updaters: pick one; both refresh the cached raw_/rad_ ---- */

    /** @brief Synchronously refresh the cached angle (blocking).
     *  @return true on success; the cache is only updated on success
     */
    bool update_sync(uint32_t timeout_ms = 1000);

    /** @brief Advance the async state machine 
     *  @return true if this call committed a fresh sample
     */
    bool update_async();

    /* ---- Readers: pure cache access, never touch I2C, O(1) ---- */

    /** @brief Latest raw 14-bit angle count (0..16383). */
    uint16_t readRaw() const { return raw_; }

    /** @brief Latest angle in radians (wrapped to [0, 2π)).
     *         Zero-float main path for FOC control loops. */
    q24_t readAngleRad() const { return rad_; }

    /** @brief Latest angle in degrees (wrapped to [0, 360)).
     *         One multiply; for low-rate debug use only. */
    q24_t readAngleDeg() const { return rad_ * q24(RAD_TO_DEG_C); }

    int32_t readAngleTurns() const { return turn_cnt_; }

private:
    SimuSSI<24>& m_ssi;         /**< @brief SSI bus instance */

    bool first_sample_ = true;
    bool first_async = true;   /**< @brief first async read: fire a read */

    /* ---- Helpers ---- */
    /** @brief raw -> radians, wrapped to [0, 2π). */
    q24_t rawToRad(uint32_t raw);

    /* ---- Cached angle (written by updaters, read by readers) ---- */
    volatile uint16_t raw_ = 0;     /**< @brief latest raw 14-bit count */
    volatile uint16_t prev_raw_ = 0; /**< @brief previous raw count (for turn counting) */
    q24_t    rad_ = q24(0.0f);  /**< @brief latest angle in radians (offset+wrap) */
    volatile int32_t  turn_cnt_ = 0; /**< @brief turn count (increments on wrap-around) */
};