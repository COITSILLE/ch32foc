#pragma once

#include <cstdint>
#include <cstring>
#include "systick_time.h"

/** @brief Sync byte that starts every frame, in both directions. */
#define FRAME_SYNC 0xa5
/** @brief Maximum payload size of a command frame (one 32-bit word). */
#define CMD_DATA_SIZE 4
/** @brief Maximum payload size of any frame */
#define FRAME_MAX_DATA 12

/**
 * @brief Command frame: sync byte, address + command byte, length byte, data bytes and a CRC byte.
 *
 * The response frame uses the same layout with the type byte in place of the command byte:
 * sync byte, tag + type, length, data words and a CRC byte.
 */
struct LongCmdFrame {
    uint8_t addr_n_cmd;
    uint8_t len;
    uint8_t data[CMD_DATA_SIZE];
    uint8_t checksum;
};

enum class DataFrameType : uint8_t {
    REQUIRE_RESEND = 0,
    SHORT = 0x01,
    LONG = 0x02,
    SHORT_WITH_WARNING = 0x11,
    LONG_WITH_WARNING = 0x12,
    FAULT = 0x03,
    NOT_SUPPORTED = 0x04,
};

/** @brief Response frame payload: type byte, data words and a CRC byte. */
struct DataFrame {
    uint8_t type;
    uint8_t* data;
    uint8_t checksum;
};

enum class Cmd : uint8_t {
    NOP = 0x00,
    GET_FEEDBACK,
    GET_CURRENT,
    GET_SPEED,
    GET_POSITION,
    GET_VBUS,
    GET_SYSREG,

    ENABLE,
    DISABLE,

    SET_MODE,
    SET_CURRENT,
    SET_SPEED,
    SET_POSITION,

    SET_CURRENT_BW,
    SET_SPEED_BW,
    SET_SPEED_ZETA,
    SET_POS_BW,
    SET_ESO_BW,
    SET_ABFILT_BW,
    SET_ABFILT_ZETA,
    CLEAR_FAULT,
};

enum class CmdDebug : uint8_t {
    CALIB_ZERO_POS = (int)Cmd::CLEAR_FAULT + 1,
    
    GET_ADDR,
    GET_DVC_INFO,

    SET_BAUDRATE,
    SET_DEFAULT_CONFIG,

    SAVE_CONFIG,

    SET_OVC_THRESHOLD,
    SET_OVC_TIMEOUT,

    SET_CONT_ANG_ZERO_OFFSET,
};

#define ADDR_MASK 0xe0
#define CMD_MASK 0x1f

/** @brief Advance the CRC byte with one received byte (table driven). */
void do_crc(uint8_t* crc, uint8_t new_data);

using cmd_rcv_callback_t = void(*)(bool is_valid, uint8_t tag, uint8_t cmd, uint32_t data);

/** @brief Byte-wise command frame parser for the half-duplex protocol. */
class DeviceProtocolRx {
public:
    /** @brief Store the callback invoked once a frame has been received. */
    DeviceProtocolRx(cmd_rcv_callback_t callback) : m_cmd_callback(callback) {}

    /** @brief Set the device address matched against incoming frames (clamped to 7). */
    void setAddr(uint8_t addr) { m_device_addr = (addr > 7 ? 7 : addr); }

    /** @brief Feed one received byte into the frame state machine. */
    void processCmdFrame(uint8_t rx_byte);

    /** @brief Abort a partially received frame if it stalled. */
    void updateTimeoutState();

    /** @brief Leave the awaiting-send state once the response has been handed off. */
    void cpltSend() { if (m_state == AWAITS_SEND) m_state = WAIT_SYNC; }
private:
    enum {
        AWAITS_SEND = 0,
        WAIT_SYNC,
        HEADER,
        LENGTH,
        SKIP,
        DATA,
        CHECKSUM,
    } m_state = WAIT_SYNC;

    uint8_t m_device_addr = 0x00;

    cmd_rcv_callback_t m_cmd_callback = nullptr;

    uint8_t m_data_index = 0;
    uint8_t m_skip_cnt = 0;
    LongCmdFrame m_last_cmd_frame = {0};

    uint32_t m_timeout_stamp = 0;
};

/** @brief Builder for a response frame (header, data words, CRC). */
class DeviceProtocolTx {
public:
    /** @brief Set the tag bits of the response header. */
    void setTag(uint8_t tag);
    /** @brief Set the frame type and start a new frame. */
    void setType(DataFrameType type);
    /** @brief Append the data words of the frame. */
    void setData(const uint8_t* data, int len);
    /**
     * @brief Append the CRC and copy the finished frame out.
     * @return Frame length in bytes.
     */
    int getTxFrame(uint8_t* buffer);
private:
    uint8_t m_tx_buffer[20] = {0};
    int m_tx_len = 0;
};