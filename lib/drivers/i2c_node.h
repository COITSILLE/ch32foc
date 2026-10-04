#pragma once

#include "rtos_handler.h"
#include "systick_time.h"
#include "delay.h"
#include <assert.h>

class i2c_node;

/**
 * @brief Callback argument passed to all I2C event handlers.
 */
struct i2c_callback_args_t {
    i2c_node* node;      /**< @brief I2C node that triggered the event */
    uint8_t*  data;      /**< @brief Pointer to the RX/TX buffer */
    uint32_t  size;      /**< @brief Number of bytes */
    uint32_t  err_flags; /**< @brief Error flags (WCH I2C_FLAG_*), valid only for err_handler */
    void*     user_ctx;  /**< @brief Opaque user pointer set via setCallback() */
};

/**
 * @brief Callback function table for I2C events.
 *
 * Set any unused callback to nullptr.
 */
struct i2c_callback_funcs_t {
    void (*tx_cplt)(i2c_callback_args_t*);     /**< @brief Called when TX completes */
    void (*rx_cplt)(i2c_callback_args_t*);     /**< @brief Called when RX completes */
    void (*err_handler)(i2c_callback_args_t*); /**< @brief Called on NACK / arbitration lost / bus error */
};

/**
 * @brief CH32M030 master-mode I2C driver built on the WCH standard peripheral
 *        library.
 *
 * Interrupt-driven: the async APIs return immediately and the transfer is
 * advanced inside irqHandler() (state machine). The sync APIs are thin
 * wrappers that poll the completion flags with a timeout.
 *
 * @note Hardware init (clocks, GPIO, I2C_Init, NVIC) is done by the caller.
 *       The ISR handlers (EV/ER) must call irqHandler().
 */
class i2c_node {
public:
    /* ---- Construction & callback registration ---- */

    /**
     * @brief Construct an I2C node.
     * @param i2c  I2C peripheral register base (e.g. I2C1)
     * @param cbs  Callback table (nullptr to disable all callbacks)
     */
    i2c_node(I2C_TypeDef* i2c, const i2c_callback_funcs_t* cbs = nullptr);

    /**
     * @brief Replace the callback table at runtime.
     *
     * Lets a peripheral register/unregister its ISR handlers after
     * construction. Sync transfers never invoke callbacks, so they are
     * unaffected.
     *
     * @param cbs      New callback table (nullptr disables all callbacks)
     * @param user_ctx Opaque pointer delivered in every callback's user_ctx
     */
    void setCallback(const i2c_callback_funcs_t* cbs, void* user_ctx = nullptr);

    /* ---- Init & bus recovery ---- */

    /**
     * @brief Verify hardware preconditions and snapshot the clock config.
     *
     * Asserts the I2C peripheral is enabled (PE) and the required interrupt
     * masks are set, then captures CTLR2/CKCFGR so resetBus() can restore the
     * bus configuration after a software reset. Call after the hardware init
     * (clocks, GPIO, I2C_Init, NVIC, I2C_ITConfig).
     */
    void init(void);

    /**
     * @brief Force-recover the bus after a stuck transfer.
     *
     * Pulses SWRST, restores the configuration captured by init(), re-enables
     * the interrupts and resets the state machine. Does not touch GPIO/RCC.
     */
    void resetBus();

    /* ---- Raw transfer (sync / async) ---- */

    /**
     * @brief Transmit data synchronously with timeout.
     * @param dev_addr    7-bit I2C target address
     * @param p_data      TX buffer
     * @param len         Number of bytes to send
     * @param do_restart  Issue a repeated START instead of STOP (default false)
     * @param timeout_ms  Timeout in milliseconds
     * @return true on success, false on timeout or error
     */
    bool transmit_sync(uint8_t dev_addr, const uint8_t* p_data, uint32_t len,
                       bool do_restart = false, uint32_t timeout_ms = 1000);

    /**
     * @brief Start an asynchronous transmit and return immediately.
     * @param dev_addr    7-bit I2C target address
     * @param p_data      TX buffer (must stay valid until the tx_cplt callback)
     * @param len         Number of bytes to send
     * @param do_restart  Issue a repeated START instead of STOP (default false)
     * @return true if the transfer was started
     * @note Completion is signaled via cbs_.tx_cplt / tx_done_.
     */
    bool transmit_async(uint8_t dev_addr, const uint8_t* p_data, uint32_t len,
                        bool do_restart = false);

    /**
     * @brief Receive data synchronously with timeout.
     * @param dev_addr    7-bit I2C target address
     * @param p_dest      RX buffer
     * @param len         Number of bytes to receive
     * @param timeout_ms  Timeout in milliseconds
     * @return true on success, false on timeout or error
     */
    bool receive_sync(uint8_t dev_addr, uint8_t* p_dest, uint32_t len,
                      uint32_t timeout_ms = 1000);

    /**
     * @brief Start an asynchronous receive and return immediately.
     * @param dev_addr  7-bit I2C target address
     * @param p_dest    RX buffer (must stay valid until the rx_cplt callback)
     * @param len       Number of bytes to receive
     * @return true if the transfer was started
     * @note Completion is signaled via cbs_.rx_cplt / rx_done_.
     */
    bool receive_async(uint8_t dev_addr, uint8_t* p_dest, uint32_t len);

    /* ---- Register access (sync / async) ---- */

    /**
     * @brief Write a device register synchronously.
     * @param dev_addr    7-bit I2C target address
     * @param reg         Register address
     * @param data        Value to write
     * @param timeout_ms  Timeout in milliseconds
     * @return true on success, false on timeout or error
     */
    bool writeReg_sync(uint8_t dev_addr, uint8_t reg, uint8_t data,
                       uint32_t timeout_ms = 1000);

    /**
     * @brief Start an asynchronous register write (reg + data, then STOP).
     * @param dev_addr  7-bit I2C target address
     * @param reg       Register address
     * @param data      Value to write
     * @return true if the transfer was started
     * @note Completion is signaled via cbs_.tx_cplt / tx_done_.
     */
    bool writeReg_async(uint8_t dev_addr, uint8_t reg, uint8_t data);

    /**
     * @brief Read one or more device registers synchronously
     *        (write addr + repeated START + read).
     * @param dev_addr    7-bit I2C target address
     * @param reg         First register address
     * @param p_data      Destination for the read data
     * @param len         Number of bytes to read (>=1; must fit in p_data)
     * @param timeout_ms  Total timeout for the write+read phases
     * @return true on success, false on timeout or error
     */
    bool readReg_sync(uint8_t dev_addr, uint8_t reg, uint8_t* p_data,
                      uint32_t len, uint32_t timeout_ms = 1000);

    /**
     * @brief Start an asynchronous register read
     *        (write reg + repeated START + read, chained internally).
     * @param dev_addr  7-bit I2C target address
     * @param reg       Register address
     * @param p_dest    RX buffer (must stay valid until the rx_cplt callback)
     * @param len       Number of bytes to read
     * @return true if the transfer was started
     * @note The TX phase and the repeated START are chained to the RX phase
     *       inside the driver, so tx_cplt is NOT fired for this transfer;
     *       completion is signaled via cbs_.rx_cplt / rx_done_.
     */
    bool readReg_async(uint8_t dev_addr, uint8_t reg, uint8_t* p_dest, uint32_t len);

    /* ---- Interrupt dispatch ---- */

    /**
     * @brief I2C interrupt dispatcher. Call from the EV and ER ISRs.
     */
    void irqHandler();

    /* ---- Status & bus lock ---- */

    volatile bool tx_done_; /**< @brief TX completion flag for polling */
    volatile bool rx_done_; /**< @brief RX completion flag for polling */

#if defined(USING_FREERTOS)
    SemaphoreHandle_t bus_lock_; /**< @brief Recursive mutex guarding the shared bus */

    inline void lockBus()   { if (bus_lock_) xSemaphoreTakeRecursive(bus_lock_, portMAX_DELAY); }
    inline void unlockBus() { if (bus_lock_) xSemaphoreGiveRecursive(bus_lock_); }
#else
    inline void lockBus()   {}
    inline void unlockBus() {}
#endif
private:
    /* ---- Types ---- */

    /** @brief Current transfer direction */
    enum class transfer_mode : uint8_t {
        NONE,     /**< @brief Idle */
        TX,       /**< @brief Transmitting */
        RX,       /**< @brief Receiving */
        RX_READY  /**< @brief Repeated START issued; waiting for receive_async to supply the RX buffer */
    };

    /** @brief Progress within the current transfer */
    enum class i2c_phase : uint8_t {
        IDLE, /**< @brief No transfer in progress */
        ADDR, /**< @brief START generated: wait for SB / send address / wait for ADDR */
        DATA  /**< @brief Address acknowledged: moving data */
    };

    /* ---- State machine helpers ---- */

    /** @brief Reset all driver state to defaults */
    void resetState();
    /** @brief Handle an I2C error flag */
    void setError(uint32_t flag);
    /** @brief Finish TX: disable IRQs, set tx_done_, fire tx_cplt */
    void completeTx();
    /** @brief Finish RX: disable IRQs, set rx_done_, fire rx_cplt */
    void completeRx();
    /** @brief Abort a transfer on timeout: STOP + disable IRQs */
    void abortTransfer();
    /** @brief TX state machine, called from irqHandler */
    void txIrq();
    /** @brief RX state machine, called from irqHandler */
    void rxIrq();

    /* ---- Hardware & callbacks ---- */

    I2C_TypeDef* i2c_;                 /**< @brief I2C register base */
    i2c_callback_funcs_t cbs_;         /**< @brief Local copy of the callback table */
    i2c_callback_args_t cb_args_;      /**< @brief Reusable callback argument */
    void* user_ctx_ = nullptr;         /**< @brief User context delivered to callbacks */

    /* ---- Transfer state ---- */

    volatile bool err_ = false;        /**< @brief Error occurred during the transfer */
    transfer_mode mode_ = transfer_mode::NONE; /**< @brief Current direction */
    i2c_phase phase_ = i2c_phase::IDLE;        /**< @brief Current phase */

    const uint8_t* tx_buf_ = nullptr;  /**< @brief TX source buffer */
    uint32_t tx_len_ = 0;              /**< @brief Total TX byte count */
    uint32_t tx_pos_ = 0;              /**< @brief Bytes already sent */

    uint8_t* rx_buf_ = nullptr;        /**< @brief RX destination buffer */
    uint32_t rx_len_ = 0;              /**< @brief Expected RX byte count */
    uint32_t rx_pos_ = 0;              /**< @brief Bytes already received */

    uint8_t dev_addr_ = 0;             /**< @brief 7-bit target address */
    bool do_restart_ = false;          /**< @brief End TX with a repeated START instead of STOP */

    /* ---- Async register helpers ----
     * reg_tx_buf_ holds the TX bytes (must persist across the async TX);
     * readReg_async reuses the shared rx_buf_/rx_len_/dev_addr_ for its RX
     * stage, so no extra members are needed. */

    uint8_t reg_tx_buf_[2] = {0, 0}; /**< @brief Persisted [reg, data] for async register ops */
    bool    reg_rx_pending_ = false; /**< @brief readReg_async: RX stage pending after TX */

    /* ---- Configuration snapshot (captured in init(), restored by resetBus()) ---- */

    uint16_t ctlr2_cfg_  = 0;   /**< @brief CTLR2 (clock freq + IT enables) snapshot */
    uint16_t ckcfgr_cfg_ = 0;   /**< @brief CKCFGR (mode + rate) snapshot */
};
