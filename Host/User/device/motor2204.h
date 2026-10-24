#pragma once

#include "sys_interface.h"
#include "usart.h"

#ifdef __cplusplus

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

/* 与下位机 protocol.h 的 Cmd 严格一一对应；顺序错一项，后面所有命令都会错位一格。
 * 这里把关键编号钉死，改枚举顺序会直接编译失败。 */
static_assert((uint8_t)Cmd::GET_FEEDBACK == 1u, "Cmd out of sync with firmware protocol.h");
static_assert((uint8_t)Cmd::GET_SYSREG == 6u, "Cmd out of sync with firmware protocol.h");
static_assert((uint8_t)Cmd::ENABLE == 7u, "Cmd out of sync with firmware protocol.h");
static_assert((uint8_t)Cmd::DISABLE == 8u, "Cmd out of sync with firmware protocol.h");
static_assert((uint8_t)Cmd::SET_MODE == 9u, "Cmd out of sync with firmware protocol.h");
static_assert((uint8_t)Cmd::SET_CURRENT == 10u, "Cmd out of sync with firmware protocol.h");
static_assert((uint8_t)Cmd::SET_ABFILT_ZETA == 19u, "Cmd out of sync with firmware protocol.h");
static_assert((uint8_t)Cmd::CLEAR_FAULT == 20u, "Cmd out of sync with firmware protocol.h");

/* 下位机 protocol.h 的 CmdDebug，编号接在 Cmd 之后。
 * 只有上电时 PB0 拉低（BOOT_MODE == DEBUG_）才会执行，否则回 NOT_SUPPORTED。
 * 设备地址不再由命令写入：下位机上电后转电机（encoder UI）即可改地址。 */
enum class CmdDebug : uint8_t {
    CALIB_ZERO_POS = 21,
    GET_ADDR,
    GET_DVC_INFO,
    SET_BAUDRATE,
    SET_DEFAULT_CONFIG,
    SAVE_CONFIG,
    SET_OVC_THRESHOLD,
    SET_OVC_TIMEOUT,
    SET_CONT_ANG_ZERO_OFFSET,
};

static_assert((uint8_t)CmdDebug::CALIB_ZERO_POS == (uint8_t)Cmd::CLEAR_FAULT + 1u,
              "CmdDebug out of sync with firmware protocol.h");
static_assert((uint8_t)CmdDebug::SAVE_CONFIG == 26u, "CmdDebug out of sync with firmware protocol.h");
static_assert((uint8_t)CmdDebug::SET_OVC_TIMEOUT == 28u, "CmdDebug out of sync with firmware protocol.h");
static_assert((uint8_t)CmdDebug::SET_CONT_ANG_ZERO_OFFSET == 29u,
              "CmdDebug out of sync with firmware protocol.h");

/* 每帧都以 SYNC 开头；收帧时靠它重新同步 */
constexpr uint8_t FRAME_SYNC = 0xa5;

/* bit4 是告警标志，与 SHORT/LONG 组合；低 4 位才是帧类型 */
constexpr uint8_t REPLY_TYPE_MASK = 0x0f;
constexpr uint8_t REPLY_WARNING_MASK = 0x10;

enum class DataFrameType : uint8_t {
    REQUIRE_RESEND = 0x00,
    SHORT = 0x01,
    LONG = 0x02,
    FAULT = 0x03,
    NOT_SUPPORTED = 0x04,

    SHORT_WITH_WARNING = static_cast<uint8_t>(SHORT) | REPLY_WARNING_MASK,
    LONG_WITH_WARNING = static_cast<uint8_t>(LONG) | REPLY_WARNING_MASK,
};

enum class FocLoopMode : uint8_t {
    Current = 0,
    Speed,
    Pos,
};

enum class SysState : uint8_t {
    INIT = 0,
    RUN,
    FAULT,
    CALIB_POS,
};

/* GET_SYSREG 的 32 位系统寄存器 */
struct SysReg{
    uint32_t raw = 0;

    uint8_t  warning() const { return static_cast<uint8_t>(raw & 0xFFu); }
    uint8_t  fault() const { return static_cast<uint8_t>((raw >> 8) & 0xFFu); }
    SysState state() const { return static_cast<SysState>((raw >> 16) & 0xFFu); }
    uint8_t  mcuFlags() const { return static_cast<uint8_t>((raw >> 24) & 0xFFu); }
};

/* warning 字节（SYS_REG bit[7:0]） */
enum WarningBit : uint8_t {
    WARN_FAILED_READ_FLASH  = 1u << 4,
    WARN_FAILED_INIT_ENCODER = 1u << 5,
    WARN_VOLTAGE_FELL       = 1u << 6,
    WARN_VOLTAGE_BACK_CHARGED = 1u << 7,
};

/* fault 字节（SYS_REG bit[15:8]） */
enum FaultBit : uint8_t {
    FAULT_OVER_CURRENT = 1u << 5,
    FAULT_OVER_VOLTAGE = 1u << 6,
    FAULT_UNDER_VOLTAGE = 1u << 7,
};

/**
 * @brief Host-side driver for the 2204 FOC driver board.
 *
 * Wire format（SYNC 起头、LEN 显式给长度、CRC 不覆盖 SYNC）
 *  - command: 0xA5 | (addr << 5 | cmd) | len | data[len] | crc
 *    地址占 bit[7:5]（0..7），命令占 bit[4:0]；len 只能是 0（GET_* / ENABLE /
 *    DISABLE / CLEAR_FAULT）或 4（带一个 32 位参数的命令）。
 *    CRC 覆盖 HDR + len + data。帧长 4（短）/ 8（长）。
 *    下位机把地址同时当作回复的 tag 用，所以设备地址必须 <= 7。
 *  - reply  : 0xA5 | (tag << 5 | type) | len | data[len] | crc
 *    len 只能是 4（1 个字）或 12（3 个字）；CRC 同样覆盖 HDR + len + data。
 *    帧长 8（短）/ 16（长）。
 *    type: 0x01 SHORT / 0x02 LONG（bit4 置位表示有 warning），
 *          0x03 FAULT（命令被拒绝），0x04 NOT_SUPPORTED（命令不支持），
 *          0x00 REQUIRE_RESEND（CRC 错，重发）。后三种都带 12 字节反馈。
 *  - 收帧时先扫 SYNC，再按 len 取满整帧；任何校验失败都丢掉当前候选帧、
 *    从下一个 SYNC 重新扫描。
 *  - set commands carry a float32 bit pattern, GET replies carry float32 bit patterns
 *    in physical units (A / rad/s / rad / V), already unscaled by the device.
 */
class Motor2204{
public:
    struct Config{
        UART_HandleTypeDef* huart = &huart1;
        uint8_t  addr = 0x00;            /**< 0..7，与回复 tag 共用 */
        uint32_t timeout_ms = 20;        /**< frame header / tx timeout */
        uint32_t byte_timeout_ms = 2;    /**< intra-frame byte gap timeout */
        uint32_t calib_timeout_ms = 3000;/**< 零点校准要等约 1.1 s 对齐 */
        uint8_t  retry = 2;              /**< resend limit on timeout / bad frame */
    };

    struct Feedback{
        float current = 0.0f;       /**< A */
        float speed = 0.0f;         /**< rad/s */
        float position = 0.0f;      /**< rad */
        float vbus = 0.0f;          /**< V */
    };

    enum class Error : uint8_t {
        NONE = 0,
        TIMEOUT,
        BAD_FRAME,
        FAULT,          /**< 下位机处于故障态，命令被拒绝 */
        NOT_SUPPORTED,  /**< 命令不存在，或 debug 命令但下位机不在 DEBUG 模式 */
    };

    explicit Motor2204(const Config& cfg);

    void setAddr(uint8_t addr) { cfg_.addr = (addr > 0x07u) ? 0x07u : addr; }
    uint8_t getAddr() const { return cfg_.addr; }

    bool enable();
    bool disable();

    bool setLoopMode(FocLoopMode mode);
    bool setIqRef(float amp);
    bool setSpeedRef(float rad_s);
    bool setPosRef(float rad);

    bool setCurrentLoopBandWidth(float hz);
    bool setSpeedLoopBandWidth(float hz);
    bool setSpeedLoopDamping(float zeta);
    bool setPosLoopBandWidth(float hz);
    bool setEsoBandWidth(float hz);
    bool setSpeedABFiltBandWidth(float hz);
    bool setSpeedABFiltDamping(float zeta);

    bool requestFeedback();
    bool requestCurrent();
    bool requestSpeed();
    bool requestPosition();
    bool requestVbus();

    /** 读系统寄存器（状态 / 故障 / 告警） */
    bool getSysReg(SysReg* reg);

    /** 清故障：仅在下位机 FAULT 态有效，成功后回到 RUN（成功回 SHORT） */
    bool clearFault();

    /* ---- debug commands, 需要下位机上电时拉低 PB0（BOOT_MODE == DEBUG_）---- */

    struct DeviceInfo{
        uint32_t firmware_version = 0;  /**< bit[23:16]=major, [15:8]=minor, [7:0]=batch */
        uint32_t chip_id = 0;
        uint32_t device_id = 0;
    };

    /* 正常模式会回 NOT_SUPPORTED（错误码 NOT_SUPPORTED），据此判断下位机是否在 DEBUG 模式。 */
    bool getDeviceInfo(DeviceInfo* info);

    /* 零点对齐：下位机会施加对齐电压约 1.1 s，电机必须能自由转动。
     * 内部不重发（重发会重复对齐），超时时间用 Config::calib_timeout_ms。 */
    bool calibZeroPos();

    bool getDeviceAddr(uint8_t* addr);      /* 只读；地址由下位机编码器 UI 设置 */
    bool setDeviceBaudrate(uint32_t baud);  /* 只写 RAM，重启后生效，需 saveConfig() */
    bool loadDefaultConfig();
    bool saveConfig();

    bool setOvcThreshold(float amp);        /* 过流阈值，只写 RAM，需 saveConfig() */
    bool setOvcTimeout(uint32_t ms);        /* 过流持续时间阈值，只写 RAM，需 saveConfig() */

    /* 连续角零点偏移：下位机以当前编码器位置为基准重算并写入 RAM，需 saveConfig() 才掉电保留。
     * 注意：下位机这条命令只回 SHORT 帧，不带偏移值，所以桥接也只回 `OK ZOFF`。 */
    bool setContAngZeroOffset();

    const Feedback& getFeedback() const { return feedback_; }
    Error getLastError() const { return last_error_; }
    /** 最近一次成功回复是否带 warning 位（SYS_REG.warning != 0） */
    bool hasWarning() const { return warning_; }
    uint32_t getTxCount() const { return tx_count_; }
    uint32_t getErrCount() const { return err_count_; }

private:
    enum class RxResult : uint8_t { OK, RESEND, FAIL };

    uint8_t buildFrame(uint8_t* buf, uint8_t cmd, const uint8_t* data, uint8_t data_len);
    RxResult readFrame(uint8_t* type, uint8_t* data, uint8_t* len, uint32_t timeout_ms);
    bool transmit(const uint8_t* buf, uint8_t len);
    bool transfer(uint8_t cmd, const uint8_t* data, DataFrameType expect, uint32_t* rx, uint8_t* rx_len,
                  uint32_t timeout_ms, uint8_t retry);
    bool execSetValue(Cmd cmd, uint32_t value);
    bool execRequest(Cmd cmd);
    bool execSimple(Cmd cmd);
    bool execDebug(CmdDebug cmd, uint32_t value, DataFrameType expect, uint32_t* rx, uint8_t* rx_len,
                   uint32_t timeout_ms, uint8_t retry);
    void drainRx();
    void applyFeedback(const uint32_t* raw, uint8_t count);
    void applySingle(Cmd cmd, uint32_t raw);

    Config cfg_;
    Feedback feedback_;
    Error last_error_ = Error::NONE;
    bool warning_ = false;
    uint32_t tx_count_ = 0;
    uint32_t err_count_ = 0;
};

#endif