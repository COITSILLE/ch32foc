#include "i2c_node.h"


// ============================================================================
// Constructor / initialization
// ============================================================================

/**
 * @brief Construct an I2C node.
 * @param i2c  I2C peripheral register base
 * @param cbs  Callback table (nullptr to disable all callbacks)
 */
i2c_node::i2c_node(I2C_TypeDef* i2c, const i2c_callback_funcs_t* cbs)
    : i2c_(i2c)
    , cbs_(cbs ? *cbs : i2c_callback_funcs_t{nullptr, nullptr, nullptr})
{
    resetState();

#if defined(USING_FREERTOS)
    /* Recursive mutex, safe to create before the scheduler starts */
    bus_lock_ = xSemaphoreCreateRecursiveMutex();
#endif

    cb_args_.node = this;
    cb_args_.data = nullptr;
    cb_args_.size = 0;
    cb_args_.err_flags = 0;
    cb_args_.user_ctx = nullptr;
}

/**
 * @brief Replace the callback table at runtime.
 */
void i2c_node::setCallback(const i2c_callback_funcs_t* cbs, void* user_ctx)
{
    cbs_ = cbs ? *cbs : i2c_callback_funcs_t{nullptr, nullptr, nullptr};
    user_ctx_ = user_ctx;
}

/**
 * @brief Reset all driver state to defaults.
 */
void i2c_node::resetState()
{
    tx_done_ = false;
    rx_done_ = false;
    err_ = false;
    mode_  = transfer_mode::NONE;
    phase_ = i2c_phase::IDLE;

    tx_buf_ = nullptr;
    tx_len_ = 0;
    tx_pos_ = 0;

    rx_buf_ = nullptr;
    rx_len_ = 0;
    rx_pos_ = 0;

    dev_addr_ = 0;
    do_restart_ = false;

    reg_tx_buf_[0] = 0;
    reg_tx_buf_[1] = 0;
    reg_rx_pending_ = false;
}

/**
 * @brief Handle an I2C error flag.
 *
 * Disables the I2C interrupts, generates a STOP, clears the error flag and
 * fires the err_handler callback.
 *
 * @param flag  The I2C_FLAG_* that caused the error
 */
void i2c_node::setError(uint32_t flag)
{
    err_ = true;
    tx_done_ = false;
    rx_done_ = false;
    mode_  = transfer_mode::NONE;
    phase_ = i2c_phase::IDLE;
    reg_rx_pending_ = false;

    I2C_ITConfig(i2c_, I2C_IT_BUF | I2C_IT_EVT | I2C_IT_ERR, DISABLE);
    I2C_GenerateSTOP(i2c_, ENABLE);
    I2C_ClearFlag(i2c_, flag);

    cb_args_.err_flags = flag;
    cb_args_.data = (tx_buf_ != nullptr) ? const_cast<uint8_t*>(tx_buf_) : rx_buf_;
    cb_args_.size = (tx_len_ > rx_len_) ? tx_len_ : rx_len_;
    cb_args_.user_ctx = user_ctx_;
    if (cbs_.err_handler) {
        cbs_.err_handler(&cb_args_);
    }
}

/**
 * @brief Finish a TX transfer.
 *
 * Disables the I2C interrupts, sets tx_done_ and fires the tx_cplt callback.
 */
void i2c_node::completeTx()
{
    mode_  = transfer_mode::NONE;
    phase_ = i2c_phase::IDLE;
    tx_done_ = true;
    rx_done_ = false;
    err_ = false;

    I2C_ITConfig(i2c_, I2C_IT_BUF | I2C_IT_EVT | I2C_IT_ERR, DISABLE);

    cb_args_.data = const_cast<uint8_t*>(tx_buf_);
    cb_args_.size = tx_len_;
    cb_args_.err_flags = 0;
    cb_args_.user_ctx = user_ctx_;
    if (cbs_.tx_cplt) {
        cbs_.tx_cplt(&cb_args_);
    }
}

/**
 * @brief Finish an RX transfer.
 *
 * Disables the I2C interrupts, sets rx_done_ and fires the rx_cplt callback.
 */
void i2c_node::completeRx()
{
    mode_  = transfer_mode::NONE;
    phase_ = i2c_phase::IDLE;
    tx_done_ = false;
    rx_done_ = true;
    err_ = false;

    I2C_ITConfig(i2c_, I2C_IT_BUF | I2C_IT_EVT | I2C_IT_ERR, DISABLE);

    cb_args_.data = rx_buf_;
    cb_args_.size = rx_pos_;
    cb_args_.err_flags = 0;
    cb_args_.user_ctx = user_ctx_;
    if (cbs_.rx_cplt) {
        cbs_.rx_cplt(&cb_args_);
    }
}

/**
 * @brief Abort a transfer (used on timeout).
 *
 * Disables interrupts, generates a STOP and marks the transfer as errored.
 */
void i2c_node::abortTransfer()
{
    I2C_ITConfig(i2c_, I2C_IT_BUF | I2C_IT_EVT | I2C_IT_ERR, DISABLE);
    I2C_GenerateSTOP(i2c_, ENABLE);
    mode_  = transfer_mode::NONE;
    phase_ = i2c_phase::IDLE;
    err_   = true;
    reg_rx_pending_ = false;
}

/**
 * @brief Verify the hardware preconditions the driver relies on.
 *
 * Asserts that the I2C peripheral is enabled (PE) and that the required
 * interrupt masks (ERR / EVT / BUF) are set. The caller must perform the
 * hardware init (clocks, GPIO, I2C_Init, NVIC, I2C_ITConfig) before
 * calling this.
 */
void i2c_node::init(void)
{
    /* The peripheral base must be valid */
    // assert(i2c_ != nullptr);

    /* I2C controller must be enabled (PE bit) */
    assert((i2c_->CTLR1 & I2C_CTLR1_PE) != 0);

    /* Required interrupt masks must be set (ERR + EVT + BUF) */
    constexpr uint16_t required_irq =
        I2C_CTLR2_ITERREN | I2C_CTLR2_ITEVTEN | I2C_CTLR2_ITBUFEN;
    assert((i2c_->CTLR2 & required_irq) == required_irq);

    /* Snapshot the clock/mode configuration so resetBus() can restore it
     * after a software reset, without coupling to the caller's peripheral.c. */
    ctlr2_cfg_  = i2c_->CTLR2;
    ckcfgr_cfg_ = i2c_->CKCFGR;
}

void i2c_node::resetBus()
{
    /* Software-reset the I2C peripheral (clears a stuck BUSY / pending STOP). */
    i2c_->CTLR1 |= I2C_CTLR1_SWRST;
    i2c_->CTLR1 &= (uint16_t)~I2C_CTLR1_SWRST;

    /* Restore the configuration captured in init(). */
    i2c_->CTLR2  = ctlr2_cfg_;
    i2c_->CKCFGR = ckcfgr_cfg_;
    i2c_->CTLR1 |= (I2C_CTLR1_PE | I2C_CTLR1_ACK);

    /* Re-enable the I2C interrupts (belt-and-suspenders; CTLR2 already holds
     * the IT enables from the snapshot). */
    I2C_ITConfig(i2c_, I2C_IT_BUF | I2C_IT_EVT | I2C_IT_ERR, ENABLE);

    /* Reset the driver state machine. */
    mode_  = transfer_mode::NONE;
    phase_ = i2c_phase::IDLE;
    tx_done_ = false;
    rx_done_ = false;
    err_     = false;
    reg_rx_pending_ = false;
}

// ============================================================================
// Interrupt state machine
// ============================================================================

/**
 * @brief I2C interrupt dispatcher. Call from the EV and ER ISRs.
 */
void i2c_node::irqHandler()
{
    /* Handle error events first */
    if (I2C_GetFlagStatus(i2c_, I2C_FLAG_AF) == SET) {
        setError(I2C_FLAG_AF);
        return;
    }
    if (I2C_GetFlagStatus(i2c_, I2C_FLAG_ARLO) == SET) {
        setError(I2C_FLAG_ARLO);
        return;
    }
    if (I2C_GetFlagStatus(i2c_, I2C_FLAG_BERR) == SET) {
        setError(I2C_FLAG_BERR);
        return;
    }

    switch (mode_) {
    case transfer_mode::TX:
        txIrq();
        break;
    case transfer_mode::RX:
    case transfer_mode::RX_READY:
        rxIrq();
        break;
    default:
        break;
    }
}

/**
 * @brief TX state machine, called from irqHandler().
 */
void i2c_node::txIrq()
{
    if (phase_ == i2c_phase::ADDR) {
        /* EVT5: START issued -> send the slave address (writing DATAR clears SB).
         * NOTE: WCH's I2C_Send7bitAddress() does NOT shift internally; DATAR
         * holds the raw bus byte (7-bit addr << 1 | R/W). Shift dev_addr_ here
         * so the driver API stays in plain 7-bit addressing. */
        if (I2C_GetFlagStatus(i2c_, I2C_FLAG_SB) == SET) {
            I2C_Send7bitAddress(i2c_, (uint8_t)(dev_addr_ << 1), I2C_Direction_Transmitter);
            return;
        }
        /* EVT6: address acknowledged -> read STAR1 then STAR2 to clear ADDR */
        if (I2C_GetFlagStatus(i2c_, I2C_FLAG_ADDR) == SET) {
            (void)I2C_GetFlagStatus(i2c_, I2C_FLAG_ADDR);
            (void)i2c_->STAR2;
            phase_ = i2c_phase::DATA;
        }
        return;
    }

    if (phase_ == i2c_phase::DATA) {
        /* TXE: data register empty -> send the next byte */
        if (I2C_GetFlagStatus(i2c_, I2C_FLAG_TXE) == SET && tx_pos_ < tx_len_) {
            I2C_SendData(i2c_, tx_buf_[tx_pos_++]);
        }
        /* BTF: last byte shifted out -> STOP (or repeated START for readReg) */
        if (I2C_GetFlagStatus(i2c_, I2C_FLAG_BTF) == SET && tx_pos_ >= tx_len_) {
            if (do_restart_) {
                /* No STOP; issue a repeated START and wait for receive_async */
                I2C_GenerateSTART(i2c_, ENABLE);
                mode_  = transfer_mode::RX_READY;
                phase_ = i2c_phase::ADDR;
                I2C_ITConfig(i2c_, I2C_IT_BUF | I2C_IT_EVT | I2C_IT_ERR, DISABLE);

                tx_done_ = true;
                err_     = false;
                cb_args_.data      = const_cast<uint8_t*>(tx_buf_);
                cb_args_.size      = tx_len_;
                cb_args_.err_flags = 0;
                cb_args_.user_ctx  = user_ctx_;
                if (reg_rx_pending_) {
                    /* readReg_async: chain straight into the RX stage; the
                     * caller is only notified via rx_cplt. */
                    reg_rx_pending_ = false;
                    receive_async(dev_addr_, rx_buf_, rx_len_);
                } else if (cbs_.tx_cplt) {
                    cbs_.tx_cplt(&cb_args_);
                }
            } else {
                I2C_GenerateSTOP(i2c_, ENABLE);
                completeTx();
            }
        }
    }
}

/**
 * @brief RX state machine, called from irqHandler().
 */
void i2c_node::rxIrq()
{
    if (phase_ == i2c_phase::ADDR) {
        if (I2C_GetFlagStatus(i2c_, I2C_FLAG_SB) == SET) {
            /* Same note as txIrq(): shift to bus byte; API uses 7-bit addr */
            I2C_Send7bitAddress(i2c_, (uint8_t)(dev_addr_ << 1), I2C_Direction_Receiver);
            return;
        }
        if (I2C_GetFlagStatus(i2c_, I2C_FLAG_ADDR) == SET) {
            (void)I2C_GetFlagStatus(i2c_, I2C_FLAG_ADDR);
            (void)i2c_->STAR2;
            phase_ = i2c_phase::DATA;
            if (rx_len_ == 1) {
                /* Single-byte read: disable ACK now so the byte ends with NACK */
                I2C_AcknowledgeConfig(i2c_, DISABLE);
            }
        }
        return;
    }

    if (phase_ == i2c_phase::DATA) {
        if (I2C_GetFlagStatus(i2c_, I2C_FLAG_RXNE) == SET && rx_pos_ < rx_len_) {
            rx_buf_[rx_pos_++] = I2C_ReceiveData(i2c_);

            if (rx_pos_ >= rx_len_) {
                /* Last byte received -> STOP */
                I2C_GenerateSTOP(i2c_, ENABLE);
                completeRx();
            } else if (rx_pos_ == (rx_len_ - 1)) {
                /* Just received the second-to-last byte -> NACK the last one */
                I2C_AcknowledgeConfig(i2c_, DISABLE);
            }
        }
    }
}

// ============================================================================
// TX — sync / async
// ============================================================================

/**
 * @brief Transmit data synchronously with timeout.
 * @param dev_addr    7-bit I2C target address
 * @param p_data      TX buffer
 * @param len         Number of bytes to send
 * @param do_restart  Issue a repeated START instead of STOP (default false)
 * @param timeout_ms  Timeout in milliseconds
 * @return true on success, false on timeout or error
 */
bool i2c_node::transmit_sync(uint8_t dev_addr, const uint8_t* p_data,
    uint32_t len, bool do_restart, uint32_t timeout_ms)
{
    if (!p_data || !len) {
        return false;
    }

    lockBus();

    bool ok = transmit_async(dev_addr, p_data, len, do_restart);
    if (ok) {
        uint32_t start_ms = Systick_getMsTick();
        bool timed_out = false;
#if defined(USING_FREERTOS)
        while (!tx_done_ && !err_ && !timed_out) {
            vTaskDelay(1);
            timed_out = ((Systick_getMsTick() - start_ms) >= timeout_ms);
        }
#else
        while (!tx_done_ && !err_ && !timed_out) {
            delay_us_nortos(50);
            timed_out = ((Systick_getMsTick() - start_ms) >= timeout_ms);
        }
#endif
        if (timed_out) {
            abortTransfer();
            ok = false;
        } else {
            tx_done_ = false;
            ok = !err_;
        }
    }

    unlockBus();
    return ok;
}

/**
 * @brief Start an asynchronous transmit and return immediately.
 * @param dev_addr    7-bit I2C target address
 * @param p_data      TX buffer (must stay valid until the tx_cplt callback)
 * @param len         Number of bytes to send
 * @param do_restart  Issue a repeated START instead of STOP (default false)
 * @return true if the transfer was started
 * @note The transfer is advanced by irqHandler(); completion is signaled via
 *       cbs_.tx_cplt / tx_done_.
 */
bool i2c_node::transmit_async(uint8_t dev_addr, const uint8_t* p_data,
    uint32_t len, bool do_restart)
{
    if (!p_data || !len) {
        return false;
    }

    tx_buf_     = p_data;
    tx_len_     = len;
    tx_pos_     = 0;
    tx_done_    = false;
    rx_done_    = false;
    err_        = false;
    mode_       = transfer_mode::TX;
    phase_      = i2c_phase::ADDR;
    dev_addr_   = dev_addr;
    do_restart_ = do_restart;

    /* Wait for any pending STOP from the previous transfer to finish */
    volatile uint32_t guard = 100000;
    while ((i2c_->CTLR1 & I2C_CTLR1_STOP) && guard--) {
    }
    I2C_GenerateSTOP(i2c_, DISABLE);
    I2C_AcknowledgeConfig(i2c_, ENABLE);
    I2C_ITConfig(i2c_, I2C_IT_BUF | I2C_IT_EVT | I2C_IT_ERR, ENABLE);
    I2C_GenerateSTART(i2c_, ENABLE);

    /* Return immediately; the transfer is driven by the ISR state machine */
    return true;
}

// ============================================================================
// RX — sync / async
// ============================================================================

/**
 * @brief Receive data synchronously with timeout.
 * @param dev_addr    7-bit I2C target address
 * @param p_dest      RX buffer
 * @param len         Number of bytes to receive
 * @param timeout_ms  Timeout in milliseconds
 * @return true on success, false on timeout or error
 */
bool i2c_node::receive_sync(uint8_t dev_addr, uint8_t* p_dest, uint32_t len,
    uint32_t timeout_ms)
{
    if (!p_dest || !len) {
        return false;
    }

    lockBus();

    bool ok = receive_async(dev_addr, p_dest, len);
    if (ok) {
        uint32_t start_ms = Systick_getMsTick();
        bool timed_out = false;
#if defined(USING_FREERTOS)
        while (!rx_done_ && !err_ && !timed_out) {
            vTaskDelay(1);
            timed_out = ((Systick_getMsTick() - start_ms) >= timeout_ms);
        }
#else
        while (!rx_done_ && !err_ && !timed_out) {
            delay_us_nortos(50);
            timed_out = ((Systick_getMsTick() - start_ms) >= timeout_ms);
        }
#endif
        if (timed_out) {
            abortTransfer();
            ok = false;
        } else {
            rx_done_ = false;
            ok = !err_;
        }
    }

    unlockBus();
    return ok;
}

/**
 * @brief Start an asynchronous receive and return immediately.
 * @param dev_addr  7-bit I2C target address
 * @param p_dest    RX buffer (must stay valid until the rx_cplt callback)
 * @param len       Number of bytes to receive
 * @return true if the transfer was started
 * @note The transfer is advanced by irqHandler(); completion is signaled via
 *       cbs_.rx_cplt / rx_done_.
 */
bool i2c_node::receive_async(uint8_t dev_addr, uint8_t* p_dest, uint32_t len)
{
    if (!p_dest || !len) {
        return false;
    }

    rx_buf_   = p_dest;
    rx_len_   = len;
    rx_pos_   = 0;
    tx_done_  = false;
    rx_done_  = false;
    err_      = false;
    dev_addr_ = dev_addr;
    do_restart_ = false;

    /* readReg_sync path: the TX phase already issued a repeated START
     * (RX_READY), so do not issue another START — take over the pending SB. */
    bool restart_pending = (mode_ == transfer_mode::RX_READY);

    mode_  = transfer_mode::RX;
    phase_ = i2c_phase::ADDR;

    if (!restart_pending) {
        /* Wait for any pending STOP from the previous transfer to finish */
        volatile uint32_t guard = 100000;
        while ((i2c_->CTLR1 & I2C_CTLR1_STOP) && guard--) {
        }
        I2C_GenerateSTOP(i2c_, DISABLE);
        I2C_AcknowledgeConfig(i2c_, ENABLE);
        I2C_GenerateSTART(i2c_, ENABLE);
    }

    I2C_ITConfig(i2c_, I2C_IT_BUF | I2C_IT_EVT | I2C_IT_ERR, ENABLE);

    /* Return immediately; the transfer is driven by the ISR state machine */
    return true;
}

// ============================================================================
// Register helpers
// ============================================================================

/**
 * @brief Write a device register synchronously.
 * @param dev_addr    7-bit I2C target address
 * @param reg         Register address
 * @param data        Value to write
 * @param timeout_ms  Timeout in milliseconds
 * @return true on success, false on timeout or error
 */
bool i2c_node::writeReg_sync(uint8_t dev_addr, uint8_t reg, uint8_t data,
    uint32_t timeout_ms)
{
    uint8_t buf[2] = {reg, data};
    return transmit_sync(dev_addr, buf, sizeof(buf), false, timeout_ms);
}

/**
 * @brief Start an asynchronous register write.
 *
 * The [reg, data] bytes are persisted internally (the TX runs asynchronously,
 * so a stack local would dangle); completion is signaled via tx_cplt.
 */
bool i2c_node::writeReg_async(uint8_t dev_addr, uint8_t reg, uint8_t data)
{
    reg_tx_buf_[0] = reg;
    reg_tx_buf_[1] = data;
    return transmit_async(dev_addr, reg_tx_buf_, 2, false);
}

/**
 * @brief Read a device register synchronously
 *        (write addr + repeated START + read).
 * @param dev_addr    7-bit I2C target address
 * @param reg         Register address
 * @param p_data      Destination for the read value
 * @param timeout_ms  Total timeout for the write+read phases
 * @return true on success, false on timeout or error
 */
bool i2c_node::readReg_sync(uint8_t dev_addr, uint8_t reg, uint8_t* p_data,
    uint32_t len, uint32_t timeout_ms)
{
    if (!p_data || !len) {
        return false;
    }

    lockBus();

    uint32_t start = Systick_getMsTick();

    bool ok = transmit_sync(dev_addr, &reg, 1, true, timeout_ms);
    uint32_t elapsed = Systick_getMsTick() - start;
    if (ok && elapsed < timeout_ms) {
        ok = receive_sync(dev_addr, p_data, len, timeout_ms - elapsed);
    } else {
        ok = false;
    }

    unlockBus();
    return ok;
}

/**
 * @brief Start an asynchronous register read.
 *
 * The register byte is persisted internally (the TX runs asynchronously, so a
 * stack local would dangle); once TX + repeated START complete, the driver
 * chains straight into the RX stage and the caller is only notified via
 * rx_cplt / rx_done_.
 */
bool i2c_node::readReg_async(uint8_t dev_addr, uint8_t reg, uint8_t* p_dest, uint32_t len)
{
    if (!p_dest || !len) {
        return false;
    }

    /* Persist the register byte for the async TX, and stage the RX info in
     * the shared rx_buf_/rx_len_ (dev_addr_ is set by transmit_async). The RX
     * stage is taken over automatically when the TX + restart completes. */
    reg_tx_buf_[0]  = reg;
    reg_rx_pending_ = true;
    rx_buf_         = p_dest;
    rx_len_         = len;

    if (!transmit_async(dev_addr, reg_tx_buf_, 1, true)) {
        reg_rx_pending_ = false;
        return false;
    }
    return true;
}
