#include "motor2204.h"
#include <cstring>

namespace {

/* 反馈与给定都是 IEEE-754 float32 的位型（下位机 bit_cast(float) 收发）。
 * 下位机发出前已把内部定标还原成物理单位：电流 A、速度 rad/s、位置 rad、母线 V，
 * 所以这里直接按 float 解码，不能再乘 SPD_UNIT / POS_UNIT。 */
constexpr uint8_t REPLY_SHORT_LEN = 4;
constexpr uint8_t REPLY_LONG_LEN = 12;
constexpr uint8_t CMD_DATA_LEN = 4;

/* 帧开销：SYNC + HDR + LEN + CRC，不含 data */
constexpr uint8_t FRAME_OVERHEAD = 4;

bool waitByte(UART_HandleTypeDef* huart, uint8_t* byte, uint32_t timeout_ms){
    return HAL_UART_Receive(huart, byte, 1, timeout_ms) == HAL_OK;
}

const uint8_t crc_table[256] = {
    0x00, 0x5e, 0xbc, 0xe2, 0x61, 0x3f, 0xdd, 0x83, 
    0xc2, 0x9c, 0x7e, 0x20, 0xa3, 0xfd, 0x1f, 0x41, 
    0x9d, 0xc3, 0x21, 0x7f, 0xfc, 0xa2, 0x40, 0x1e, 
    0x5f, 0x01, 0xe3, 0xbd, 0x3e, 0x60, 0x82, 0xdc, 
    0x23, 0x7d, 0x9f, 0xc1, 0x42, 0x1c, 0xfe, 0xa0, 
    0xe1, 0xbf, 0x5d, 0x03, 0x80, 0xde, 0x3c, 0x62, 
    0xbe, 0xe0, 0x02, 0x5c, 0xdf, 0x81, 0x63, 0x3d, 
    0x7c, 0x22, 0xc0, 0x9e, 0x1d, 0x43, 0xa1, 0xff, 
    0x46, 0x18, 0xfa, 0xa4, 0x27, 0x79, 0x9b, 0xc5, 
    0x84, 0xda, 0x38, 0x66, 0xe5, 0xbb, 0x59, 0x07, 
    0xdb, 0x85, 0x67, 0x39, 0xba, 0xe4, 0x06, 0x58, 
    0x19, 0x47, 0xa5, 0xfb, 0x78, 0x26, 0xc4, 0x9a, 
    0x65, 0x3b, 0xd9, 0x87, 0x04, 0x5a, 0xb8, 0xe6, 
    0xa7, 0xf9, 0x1b, 0x45, 0xc6, 0x98, 0x7a, 0x24, 
    0xf8, 0xa6, 0x44, 0x1a, 0x99, 0xc7, 0x25, 0x7b, 
    0x3a, 0x64, 0x86, 0xd8, 0x5b, 0x05, 0xe7, 0xb9, 
    0x8c, 0xd2, 0x30, 0x6e, 0xed, 0xb3, 0x51, 0x0f, 
    0x4e, 0x10, 0xf2, 0xac, 0x2f, 0x71, 0x93, 0xcd, 
    0x11, 0x4f, 0xad, 0xf3, 0x70, 0x2e, 0xcc, 0x92, 
    0xd3, 0x8d, 0x6f, 0x31, 0xb2, 0xec, 0x0e, 0x50, 
    0xaf, 0xf1, 0x13, 0x4d, 0xce, 0x90, 0x72, 0x2c, 
    0x6d, 0x33, 0xd1, 0x8f, 0x0c, 0x52, 0xb0, 0xee, 
    0x32, 0x6c, 0x8e, 0xd0, 0x53, 0x0d, 0xef, 0xb1, 
    0xf0, 0xae, 0x4c, 0x12, 0x91, 0xcf, 0x2d, 0x73, 
    0xca, 0x94, 0x76, 0x28, 0xab, 0xf5, 0x17, 0x49, 
    0x08, 0x56, 0xb4, 0xea, 0x69, 0x37, 0xd5, 0x8b, 
    0x57, 0x09, 0xeb, 0xb5, 0x36, 0x68, 0x8a, 0xd4, 
    0x95, 0xcb, 0x29, 0x77, 0xf4, 0xaa, 0x48, 0x16, 
    0xe9, 0xb7, 0x55, 0x0b, 0x88, 0xd6, 0x34, 0x6a, 
    0x2b, 0x75, 0x97, 0xc9, 0x4a, 0x14, 0xf6, 0xa8, 
    0x74, 0x2a, 0xc8, 0x96, 0x15, 0x4b, 0xa9, 0xf7, 
    0xb6, 0xe8, 0x0a, 0x54, 0xd7, 0x89, 0x6b, 0x35,
};

void do_crc(uint8_t* crc, uint8_t new_data){
    *crc = crc_table[(*crc ^ new_data) & 0xff];
}

uint32_t floatToBits(float value){
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

float bitsToFloat(uint32_t bits){
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

} // namespace

Motor2204::Motor2204(const Config& cfg) : cfg_(cfg) {
    /* 地址只有 3 bit（与回复 tag 共用），构造时就夹紧，保证 getAddr() 与实际帧一致 */
    setAddr(cfg_.addr);
}

uint8_t Motor2204::buildFrame(uint8_t* buf, uint8_t cmd, const uint8_t* data, uint8_t data_len){
    /* 帧格式：SYNC | HDR(addr<<5 | cmd) | LEN(数据字节数) | data[LEN] | CRC
     * CRC 覆盖 HDR+LEN+data，不含 SYNC；下位机同时把地址当作回复 tag。
     * LEN 由调用方给定（0 或 4），不再由命令编号推导。 */
    buf[0] = FRAME_SYNC;
    buf[1] = static_cast<uint8_t>(((cfg_.addr & 0x07u) << 5) | (cmd & 0x1fu));
    buf[2] = data_len;
    if (data_len > 0u){
        memcpy(&buf[3], data, data_len);
    }

    uint8_t crc = 0;
    for (uint8_t i = 1; i < 3u + data_len; i++){
        do_crc(&crc, buf[i]);
    }
    buf[3u + data_len] = crc;
    return static_cast<uint8_t>(FRAME_OVERHEAD + data_len);
}

bool Motor2204::transmit(const uint8_t* buf, uint8_t len){
    tx_count_++;
    last_error_ = Error::TIMEOUT;

    /* 单线半双工：TX 与 RX 内部相连，发送期间若接收器开着就会把自己发出去的帧收回来
     * （从第 2 字节起 ORE 溢出丢弃，最终每次只读到自己的 addr，表现为 ERR FRAME）。
     * 与下位机 uart_halfduplex.cpp 的做法一致：发送只开 TE，接收只开 RE。 */
    UART_HandleTypeDef* huart = cfg_.huart;
    CLEAR_BIT(huart->Instance->CR1, USART_CR1_RE);
    __HAL_UART_CLEAR_OREFLAG(huart);
    __HAL_UART_CLEAR_FLAG(huart, UART_FLAG_TC);
    SET_BIT(huart->Instance->CR1, USART_CR1_TE);

    const HAL_StatusTypeDef status = HAL_UART_Transmit(huart, buf, len, cfg_.timeout_ms);

    /* 回到接收态：松开发送器，清掉残留，再打开接收 */
    CLEAR_BIT(huart->Instance->CR1, USART_CR1_TE);
    __HAL_UART_CLEAR_OREFLAG(huart);
    SET_BIT(huart->Instance->CR1, USART_CR1_RE);

    return status == HAL_OK;
}

void Motor2204::drainRx(){
    uint8_t byte;
    while (HAL_UART_Receive(cfg_.huart, &byte, 1, cfg_.byte_timeout_ms) == HAL_OK){
    }
}

Motor2204::RxResult Motor2204::readFrame(uint8_t* type, uint8_t* data, uint8_t* len,
                                          uint32_t timeout_ms){
    /* 帧格式：SYNC | HDR(tag<<5 | type) | LEN | data[LEN] | CRC
     * 任何一步校验失败都丢掉当前候选帧、从下一个 SYNC 重新扫描（不再"丢 1 字节再试"），
     * 这样数据里恰好出现 0xA5 也不会把后续帧带偏。 */
    uint8_t payload[REPLY_LONG_LEN] = {0};
    const uint32_t deadline = HAL_GetTick() + timeout_ms;
    last_error_ = Error::TIMEOUT;

    for (;;){
        const int32_t remaining = static_cast<int32_t>(deadline - HAL_GetTick());
        if (remaining <= 0){
            return RxResult::FAIL;
        }

        uint8_t sync = 0;
        if (!waitByte(cfg_.huart, &sync, static_cast<uint32_t>(remaining))){
            last_error_ = Error::TIMEOUT;
            return RxResult::FAIL;
        }
        if (sync != FRAME_SYNC){
            continue;
        }

        uint8_t hdr = 0;
        uint8_t data_len = 0;
        if (!waitByte(cfg_.huart, &hdr, cfg_.byte_timeout_ms) ||
            !waitByte(cfg_.huart, &data_len, cfg_.byte_timeout_ms)){
            last_error_ = Error::BAD_FRAME;
            continue;
        }

        /* LEN 只可能是 1 个字或 3 个字的数据；其它值说明这个 SYNC 是数据里的巧合 */
        if (data_len != REPLY_SHORT_LEN && data_len != REPLY_LONG_LEN){
            last_error_ = Error::BAD_FRAME;
            continue;
        }

        const uint8_t frame_type = hdr & REPLY_TYPE_MASK;
        /* 下位机把请求里的地址原样填回复的 tag 位 */
        if ((hdr >> 5) != (cfg_.addr & 0x07u) || frame_type > (uint8_t)DataFrameType::NOT_SUPPORTED){
            last_error_ = Error::BAD_FRAME;
            continue;
        }

        uint8_t crc = 0;
        do_crc(&crc, hdr);
        do_crc(&crc, data_len);

        uint8_t index = 0;
        for (; index < data_len; index++){
            if (!waitByte(cfg_.huart, &payload[index], cfg_.byte_timeout_ms)){
                break;
            }
            do_crc(&crc, payload[index]);
        }
        if (index < data_len){
            last_error_ = Error::BAD_FRAME;
            continue;
        }

        uint8_t checksum = 0;
        if (!waitByte(cfg_.huart, &checksum, cfg_.byte_timeout_ms) || checksum != crc){
            last_error_ = Error::BAD_FRAME;
            continue;
        }

        *type = hdr & 0x1fu;
        *len = data_len;
        memcpy(data, payload, data_len);
        if (frame_type == (uint8_t)DataFrameType::REQUIRE_RESEND){
            return RxResult::RESEND;
        }
        return RxResult::OK;
    }
}

bool Motor2204::transfer(uint8_t cmd, const uint8_t* data, DataFrameType expect, uint32_t* rx, uint8_t* rx_len,
                         uint32_t timeout_ms, uint8_t retry){
    const uint8_t data_len = (data != nullptr) ? CMD_DATA_LEN : 0u;
    uint8_t frame[FRAME_OVERHEAD + CMD_DATA_LEN];
    const uint8_t frame_len = buildFrame(frame, cmd, data, data_len);

    drainRx();
    for (uint8_t attempt = 0; attempt <= retry; attempt++){
        const bool sent = transmit(frame, frame_len);

        uint8_t type = 0;
        uint8_t len = 0;
        uint8_t buf[REPLY_LONG_LEN] = {0};
        const RxResult res = sent ? readFrame(&type, buf, &len, timeout_ms) : RxResult::FAIL;

        if (res == RxResult::OK){
            const DataFrameType frame_type = static_cast<DataFrameType>(type & REPLY_TYPE_MASK);
            warning_ = (type & REPLY_WARNING_MASK) != 0;
            if (len == REPLY_LONG_LEN){
                uint32_t words[3] = {0};
                memcpy(words, buf, REPLY_LONG_LEN);
                applyFeedback(words, len / CMD_DATA_LEN);
            }

            if (frame_type == expect){
                memcpy(rx, buf, len);
                *rx_len = len / CMD_DATA_LEN;
                last_error_ = Error::NONE;
                return true;
            }
            /* FAULT / NOT_SUPPORTED 是下位机的明确答复，重发没有意义 */
            if (frame_type == DataFrameType::FAULT){
                last_error_ = Error::FAULT;
                return false;
            }
            if (frame_type == DataFrameType::NOT_SUPPORTED){
                last_error_ = Error::NOT_SUPPORTED;
                return false;
            }
            last_error_ = Error::BAD_FRAME;
        }
        else if (res == RxResult::RESEND){
            /* 下位机 CRC 校验失败，要求重发：记为帧错误，别报成超时 */
            last_error_ = Error::BAD_FRAME;
        }
        err_count_++;
        drainRx();
    }
    return false;
}

bool Motor2204::execSetValue(Cmd cmd, uint32_t value){
    uint32_t rx[3] = {0};
    uint8_t count = 0;
    return transfer(static_cast<uint8_t>(cmd), reinterpret_cast<const uint8_t*>(&value), DataFrameType::LONG,
                    rx, &count, cfg_.timeout_ms, cfg_.retry);
}

bool Motor2204::execRequest(Cmd cmd){
    uint32_t rx[3] = {0};
    uint8_t count = 0;
    const DataFrameType expect = (cmd == Cmd::GET_FEEDBACK) ? DataFrameType::LONG : DataFrameType::SHORT;
    if (!transfer(static_cast<uint8_t>(cmd), nullptr, expect, rx, &count, cfg_.timeout_ms, cfg_.retry)){
        return false;
    }
    if (expect == DataFrameType::SHORT && count > 0){
        applySingle(cmd, rx[0]);
    }
    return true;
}

bool Motor2204::execSimple(Cmd cmd){
    uint32_t rx[3] = {0};
    uint8_t count = 0;
    return transfer(static_cast<uint8_t>(cmd), nullptr, DataFrameType::SHORT, rx, &count,
                    cfg_.timeout_ms, cfg_.retry);
}

bool Motor2204::execDebug(CmdDebug cmd, uint32_t value, DataFrameType expect, uint32_t* rx, uint8_t* rx_len,
                          uint32_t timeout_ms, uint8_t retry){
    /* debug 命令的编号都 >= SET_MODE，下位机会按长帧解析，所以必须带 4 字节 data。 */
    return transfer(static_cast<uint8_t>(cmd), reinterpret_cast<const uint8_t*>(&value), expect,
                    rx, rx_len, timeout_ms, retry);
}

void Motor2204::applyFeedback(const uint32_t* raw, uint8_t count){
    if (count < 3){
        return;
    }
    feedback_.current  = bitsToFloat(raw[0]);
    feedback_.speed    = bitsToFloat(raw[1]);
    feedback_.position = bitsToFloat(raw[2]);
}

void Motor2204::applySingle(Cmd cmd, uint32_t raw){
    switch (cmd){
        case Cmd::GET_CURRENT:
            feedback_.current = bitsToFloat(raw);
            break;
        case Cmd::GET_SPEED:
            feedback_.speed = bitsToFloat(raw);
            break;
        case Cmd::GET_POSITION:
            feedback_.position = bitsToFloat(raw);
            break;
        case Cmd::GET_VBUS:
            feedback_.vbus = bitsToFloat(raw);
            break;
        default:
            break;
    }
}

bool Motor2204::enable(){
    return execSimple(Cmd::ENABLE);
}

bool Motor2204::disable(){
    return execSimple(Cmd::DISABLE);
}

bool Motor2204::setLoopMode(FocLoopMode mode){
    return execSetValue(Cmd::SET_MODE, static_cast<uint32_t>(mode));
}

bool Motor2204::setIqRef(float amp){
    return execSetValue(Cmd::SET_CURRENT, floatToBits(amp));
}

bool Motor2204::setSpeedRef(float rad_s){
    return execSetValue(Cmd::SET_SPEED, floatToBits(rad_s));
}

bool Motor2204::setPosRef(float rad){
    return execSetValue(Cmd::SET_POSITION, floatToBits(rad));
}

bool Motor2204::setCurrentLoopBandWidth(float hz){
    return execSetValue(Cmd::SET_CURRENT_BW, floatToBits(hz));
}

bool Motor2204::setSpeedLoopBandWidth(float hz){
    return execSetValue(Cmd::SET_SPEED_BW, floatToBits(hz));
}

bool Motor2204::setSpeedLoopDamping(float zeta){
    return execSetValue(Cmd::SET_SPEED_ZETA, floatToBits(zeta));
}

bool Motor2204::setPosLoopBandWidth(float hz){
    return execSetValue(Cmd::SET_POS_BW, floatToBits(hz));
}

bool Motor2204::setEsoBandWidth(float hz){
    return execSetValue(Cmd::SET_ESO_BW, floatToBits(hz));
}

bool Motor2204::setSpeedABFiltBandWidth(float hz){
    return execSetValue(Cmd::SET_ABFILT_BW, floatToBits(hz));
}

bool Motor2204::setSpeedABFiltDamping(float zeta){
    return execSetValue(Cmd::SET_ABFILT_ZETA, floatToBits(zeta));
}

bool Motor2204::requestFeedback(){
    return execRequest(Cmd::GET_FEEDBACK);
}

bool Motor2204::requestCurrent(){
    return execRequest(Cmd::GET_CURRENT);
}

bool Motor2204::requestSpeed(){
    return execRequest(Cmd::GET_SPEED);
}

bool Motor2204::requestPosition(){
    return execRequest(Cmd::GET_POSITION);
}

bool Motor2204::requestVbus(){
    return execRequest(Cmd::GET_VBUS);
}

bool Motor2204::getSysReg(SysReg* reg){
    uint32_t rx[3] = {0};
    uint8_t count = 0;
    if (!transfer(static_cast<uint8_t>(Cmd::GET_SYSREG), nullptr, DataFrameType::SHORT,
                  rx, &count, cfg_.timeout_ms, cfg_.retry)){
        return false;
    }
    if (count < 1){
        return false;
    }
    reg->raw = rx[0];
    return true;
}

bool Motor2204::clearFault(){
    /* CLEAR_FAULT 编号 >= SET_MODE，下位机会按带 data 的帧解析（必须带 4 字节）；
     * 但它成功时只回 1 个字（SHORT），不能用 execSetValue 的 LONG 期望。 */
    uint32_t value = 0;
    uint32_t rx[3] = {0};
    uint8_t count = 0;
    return transfer(static_cast<uint8_t>(Cmd::CLEAR_FAULT), reinterpret_cast<const uint8_t*>(&value),
                    DataFrameType::SHORT, rx, &count, cfg_.timeout_ms, cfg_.retry);
}

bool Motor2204::getDeviceInfo(DeviceInfo* info){
    uint32_t rx[3] = {0};
    uint8_t count = 0;
    /* 正常模式（非 DEBUG）时下位机对这些编号回 NOT_SUPPORTED，
     * 只有真正执行到 DEBUG 命令表才会给 LONG。 */
    if (!execDebug(CmdDebug::GET_DVC_INFO, 0, DataFrameType::LONG, rx, &count,
                   cfg_.timeout_ms, cfg_.retry)){
        return false;
    }
    if (count < 3){
        return false;
    }
    info->firmware_version = rx[0];
    info->chip_id = rx[1];
    info->device_id = rx[2];
    return true;
}

bool Motor2204::calibZeroPos(){
    uint32_t rx[3] = {0};
    uint8_t count = 0;
    /* 下位机内部有 ~1.1 s 的对齐等待；重发会导致重复对齐，所以 retry 固定为 0。 */
    return execDebug(CmdDebug::CALIB_ZERO_POS, 0, DataFrameType::SHORT, rx, &count,
                     cfg_.calib_timeout_ms, 0);
}

bool Motor2204::getDeviceAddr(uint8_t* addr){
    uint32_t rx[3] = {0};
    uint8_t count = 0;
    if (!execDebug(CmdDebug::GET_ADDR, 0, DataFrameType::SHORT, rx, &count,
                   cfg_.timeout_ms, cfg_.retry)){
        return false;
    }
    *addr = (uint8_t)rx[0];
    return true;
}

bool Motor2204::setDeviceBaudrate(uint32_t baud){
    uint32_t rx[3] = {0};
    uint8_t count = 0;
    return execDebug(CmdDebug::SET_BAUDRATE, baud, DataFrameType::SHORT, rx, &count,
                     cfg_.timeout_ms, cfg_.retry);
}

bool Motor2204::loadDefaultConfig(){
    uint32_t rx[3] = {0};
    uint8_t count = 0;
    return execDebug(CmdDebug::SET_DEFAULT_CONFIG, 0, DataFrameType::SHORT, rx, &count,
                     cfg_.timeout_ms, cfg_.retry);
}

bool Motor2204::saveConfig(){
    uint32_t rx[3] = {0};
    uint8_t count = 0;
    /* flash 页擦写较慢，留足超时；不重发（重发会重复擦写） */
    return execDebug(CmdDebug::SAVE_CONFIG, 0, DataFrameType::SHORT, rx, &count,
                     cfg_.calib_timeout_ms, 0);
}

bool Motor2204::setOvcThreshold(float amp){
    uint32_t rx[3] = {0};
    uint8_t count = 0;
    return execDebug(CmdDebug::SET_OVC_THRESHOLD, floatToBits(amp), DataFrameType::SHORT, rx, &count,
                     cfg_.timeout_ms, cfg_.retry);
}

bool Motor2204::setOvcTimeout(uint32_t ms){
    uint32_t rx[3] = {0};
    uint8_t count = 0;
    return execDebug(CmdDebug::SET_OVC_TIMEOUT, ms, DataFrameType::SHORT, rx, &count,
                     cfg_.timeout_ms, cfg_.retry);
}

bool Motor2204::setContAngZeroOffset(){
    uint32_t rx[3] = {0};
    uint8_t count = 0;
    return execDebug(CmdDebug::SET_CONT_ANG_ZERO_OFFSET, 0, DataFrameType::SHORT, rx, &count,
                     cfg_.timeout_ms, cfg_.retry);
}
