/* ============================================================
 *  app_main.cpp  —  PC link application layer
 * ============================================================ */

/* ========= Includes ========= */
/*Include C headers*/
extern "C"
{
#include "app_main.h"
#include "cmsis_os.h"
}
/*Include Cxx headers*/
#include <cstring>
#include "motor2204.h"
#include "pc_link.h"

/* ========= Types ========= */
enum class PcCmd : uint8_t {
    NONE = 0x00,
    PING,
    ADDR,
    MODE,
    ENABLE,
    DISABLE,
    IQ,
    SPD,
    POS,
    BW,
    ZETA,
    FB,
    VBUS,
    SYSREG,
    CLEAR,

    INFO,
    CALIB,
    DADDR,
    BAUD,
    DEFAULTS,
    SAVE,
    OVC,
    ZOFF,

    INVALID,
};

/* ========= Function Prototypes ========= */
static void pc_cmd_execute(char* line);
static void cmd_set_value(PcCmd cmd, char* value_token);
static void cmd_set_bw(char* cursor);
static void cmd_set_zeta(char* cursor);

static PcCmd parse_cmd(const char* s);
static bool parse_mode(const char* arg, FocLoopMode* mode);
static bool is_digit(char c);
static char* next_token(char** cursor);
static void to_upper(char* s);
static bool parse_uint8(const char* s, uint8_t* out);
static bool parse_uint32(const char* s, uint32_t* out);
static bool parse_float(const char* s, float* out);

static char* append_text(char* p, const char* text);
static char* append_uint(char* p, uint32_t v);
static char* append_hex(char* p, uint32_t v);
static char* append_float(char* p, float v, uint8_t decimals);
static void reply_value(const char* prefix, float v, uint8_t decimals);
static void reply_error(void);
static void reply_debug_error(void);

/* ========= Global Variables ========= */
static constexpr uint8_t REPLY_DECIMALS = 3;
static constexpr uint8_t POS_DECIMALS = 4;
static constexpr uint8_t TX_CAPACITY = 64;

/* huart1 / addr 0x00 / 20ms frame / 2ms byte gap / 2 retries */
static Motor2204::Config g_motor_cfg;
static Motor2204 g_motor(g_motor_cfg);

/* huart2 / 20ms tx timeout */
static PcLink::Config g_pc_link_cfg;
static PcLink g_pc_link(g_pc_link_cfg, pc_cmd_execute);

/* INFO 成功才置位：说明下位机确实在 DEBUG 模式，其余 debug 命令才允许下发 */
static bool g_debug_confirmed = false;

/* 零点校准最低母线电压，低于此值下位机会把 zero_ang_calib 直接清 0 */
static constexpr float CALIB_VBUS_MIN_V = 5.0f;

/* ========= Main ========= */
void app_main(void)
{
    g_pc_link.init();

/* Begin loop */
    while (1)
    {
        g_pc_link.poll();
        osDelay(1);
    }
}

/* ======== Function Implementation ======== */

void HAL_UART_RxCpltCallback(UART_HandleTypeDef* huart)
{
    if (huart->Instance == USART2)
    {
        g_pc_link.onRxComplete();
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef* huart)
{
    if (huart->Instance == USART2)
    {
        g_pc_link.onError();
    }
}

static void pc_cmd_execute(char* line)
{
    char* cursor = line;
    char* cmd = next_token(&cursor);
    if (cmd == nullptr)
    {
        return;
    }
    to_upper(cmd);

    const PcCmd cmd_id = parse_cmd(cmd);
    switch (cmd_id)
    {
        case PcCmd::PING:
            g_pc_link.sendText("PONG");
            break;

        case PcCmd::ADDR:
        {
            uint8_t addr = 0;
            /* 地址与协议 tag 共用一个字节的高 3 位，范围 0..7 */
            if (!parse_uint8(next_token(&cursor), &addr) || addr > 7u)
            {
                g_pc_link.sendText("ERR ARG");
                break;
            }
            g_motor.setAddr(addr);
            char buf[TX_CAPACITY];
            char* p = append_text(buf, "OK ADDR ");
            p = append_uint(p, addr);
            g_pc_link.send(buf, p);
            break;
        }

        case PcCmd::MODE:
        {
            char* arg = next_token(&cursor);
            FocLoopMode mode = FocLoopMode::Current;
            if (arg == nullptr)
            {
                g_pc_link.sendText("ERR ARG");
                break;
            }
            to_upper(arg);
            if (!parse_mode(arg, &mode))
            {
                g_pc_link.sendText("ERR ARG");
                break;
            }
            if (!g_motor.setLoopMode(mode))
            {
                reply_error();
                break;
            }
            char buf[TX_CAPACITY];
            char* p = append_text(buf, "OK MODE ");
            p = append_uint(p, (uint32_t)mode);
            g_pc_link.send(buf, p);
            break;
        }

        case PcCmd::ENABLE:
            if (!g_motor.enable())
            {
                reply_error();
                break;
            }
            g_pc_link.sendText("OK ENABLE");
            break;

        case PcCmd::DISABLE:
            if (!g_motor.disable())
            {
                reply_error();
                break;
            }
            g_pc_link.sendText("OK DISABLE");
            break;

        case PcCmd::IQ:
        case PcCmd::SPD:
        case PcCmd::POS:
            cmd_set_value(cmd_id, next_token(&cursor));
            break;

        case PcCmd::BW:
            cmd_set_bw(cursor);
            break;

        case PcCmd::ZETA:
            cmd_set_zeta(cursor);
            break;

        case PcCmd::FB:
        {
            if (!g_motor.requestFeedback())
            {
                reply_error();
                break;
            }
            const Motor2204::Feedback& fb = g_motor.getFeedback();
            char buf[TX_CAPACITY];
            char* p = append_text(buf, "FB ");
            p = append_float(p, fb.current, REPLY_DECIMALS);
            *p++ = ' ';
            p = append_float(p, fb.speed, REPLY_DECIMALS);
            *p++ = ' ';
            p = append_float(p, fb.position, POS_DECIMALS);
            g_pc_link.send(buf, p);
            break;
        }

        case PcCmd::VBUS:
            if (!g_motor.requestVbus())
            {
                reply_error();
                break;
            }
            reply_value("VBUS ", g_motor.getFeedback().vbus, REPLY_DECIMALS);
            break;

        case PcCmd::SYSREG:
        {
            SysReg reg;
            if (!g_motor.getSysReg(&reg))
            {
                reply_error();
                break;
            }
            char buf[TX_CAPACITY];
            char* p = append_text(buf, "SYSREG ");
            p = append_hex(p, reg.raw);
            *p++ = ' ';
            p = append_uint(p, (uint32_t)reg.state());
            *p++ = ' ';
            p = append_hex(p, reg.warning());
            *p++ = ' ';
            p = append_hex(p, reg.fault());
            g_pc_link.send(buf, p);
            break;
        }

        case PcCmd::CLEAR:
            if (!g_motor.clearFault())
            {
                reply_error();
                break;
            }
            g_pc_link.sendText("OK CLEAR");
            break;

        case PcCmd::INFO:
        {
            Motor2204::DeviceInfo info;
            if (!g_motor.getDeviceInfo(&info))
            {
                /* 非 DEBUG 模式时下位机回 NOT_SUPPORTED，转成明确的 DEBUG 提示 */
                if (g_motor.getLastError() == Motor2204::Error::NOT_SUPPORTED)
                {
                    g_debug_confirmed = false;
                    g_pc_link.sendText("ERR DEBUG");
                }
                else
                {
                    reply_error();
                }
                break;
            }
            g_debug_confirmed = true;
            char buf[TX_CAPACITY];
            char* p = append_text(buf, "INFO ");
            p = append_hex(p, info.firmware_version);
            *p++ = ' ';
            p = append_hex(p, info.chip_id);
            *p++ = ' ';
            p = append_hex(p, info.device_id);
            g_pc_link.send(buf, p);
            break;
        }

        case PcCmd::CALIB:
        {
            if (!g_debug_confirmed)
            {
                reply_debug_error();
                break;
            }
            /* vbus 为 0 时下位机会把 zero_ang_calib 直接清成 0，必须先确认母线电压 */
            if (!g_motor.requestVbus() || g_motor.getFeedback().vbus < CALIB_VBUS_MIN_V)
            {
                g_pc_link.sendText("ERR VBUS");
                break;
            }
            g_pc_link.sendText("START CALIB");
            if (!g_motor.calibZeroPos())
            {
                reply_error();
                break;
            }
            g_pc_link.sendText("OK CALIB");
            break;
        }

        case PcCmd::DADDR:
        {
            /* 地址只能读：下位机上电后转动电机（encoder UI）即可改地址，再 SAVE 落盘 */
            if (next_token(&cursor) != nullptr)
            {
                g_pc_link.sendText("ERR DADDR");
                break;
            }
            uint8_t dev_addr = 0;
            if (!g_motor.getDeviceAddr(&dev_addr))
            {
                reply_error();
                break;
            }
            char buf[TX_CAPACITY];
            char* p = append_text(buf, "DADDR ");
            p = append_uint(p, dev_addr);
            g_pc_link.send(buf, p);
            break;
        }

        case PcCmd::BAUD:
        {
            if (!g_debug_confirmed)
            {
                reply_debug_error();
                break;
            }
            uint32_t baud = 0;
            if (!parse_uint32(next_token(&cursor), &baud) || baud < 1200u)
            {
                g_pc_link.sendText("ERR ARG");
                break;
            }
            if (!g_motor.setDeviceBaudrate(baud))
            {
                reply_error();
                break;
            }
            char buf[TX_CAPACITY];
            char* p = append_text(buf, "OK BAUD ");
            p = append_uint(p, baud);
            g_pc_link.send(buf, p);
            break;
        }

        case PcCmd::DEFAULTS:
            if (!g_debug_confirmed)
            {
                reply_debug_error();
                break;
            }
            if (!g_motor.loadDefaultConfig())
            {
                reply_error();
                break;
            }
            g_pc_link.sendText("OK DEFAULTS");
            break;

        case PcCmd::SAVE:
            if (!g_debug_confirmed)
            {
                reply_debug_error();
                break;
            }
            if (!g_motor.saveConfig())
            {
                reply_error();
                break;
            }
            g_pc_link.sendText("OK SAVE");
            break;

        case PcCmd::OVC:
        {
            if (!g_debug_confirmed)
            {
                reply_debug_error();
                break;
            }
            float amp = 0.0f;
            uint32_t timeout_ms = 0;
            if (!parse_float(next_token(&cursor), &amp) ||
                !parse_uint32(next_token(&cursor), &timeout_ms) || timeout_ms == 0u)
            {
                g_pc_link.sendText("ERR ARG");
                break;
            }
            if (!g_motor.setOvcThreshold(amp) || !g_motor.setOvcTimeout(timeout_ms))
            {
                reply_error();
                break;
            }
            char buf[TX_CAPACITY];
            char* p = append_text(buf, "OK OVC ");
            p = append_float(p, amp, REPLY_DECIMALS);
            *p++ = ' ';
            p = append_uint(p, timeout_ms);
            g_pc_link.send(buf, p);
            break;
        }

        case PcCmd::ZOFF:
        {
            if (!g_debug_confirmed)
            {
                reply_debug_error();
                break;
            }
            /* 以当前编码器位置为新的连续角零点；下位机不回偏移值 */
            if (!g_motor.setContAngZeroOffset())
            {
                reply_error();
                break;
            }
            g_pc_link.sendText("OK ZOFF");
            break;
        }

        case PcCmd::NONE:
        case PcCmd::INVALID:
        default:
            g_pc_link.sendText("ERR CMD");
            break;
    }
}

static void cmd_set_value(PcCmd cmd, char* value_token)
{
    float value = 0.0f;
    if (!parse_float(value_token, &value))
    {
        g_pc_link.sendText("ERR ARG");
        return;
    }

    bool ok;
    const char* name;
    switch (cmd)
    {
        case PcCmd::IQ:
            ok = g_motor.setIqRef(value);
            name = "IQ";
            break;
        case PcCmd::SPD:
            ok = g_motor.setSpeedRef(value);
            name = "SPD";
            break;
        default:
            ok = g_motor.setPosRef(value);
            name = "POS";
            break;
    }
    if (!ok)
    {
        reply_error();
        return;
    }

    char buf[TX_CAPACITY];
    char* p = append_text(buf, "OK ");
    p = append_text(p, name);
    *p++ = ' ';
    p = append_float(p, value, REPLY_DECIMALS);
    g_pc_link.send(buf, p);
}

static void cmd_set_bw(char* cursor)
{
    char* which = next_token(&cursor);
    float value = 0.0f;
    if (which == nullptr || !parse_float(next_token(&cursor), &value))
    {
        g_pc_link.sendText("ERR ARG");
        return;
    }
    to_upper(which);

    bool ok;
    if (strcmp(which, "CUR") == 0)
    {
        ok = g_motor.setCurrentLoopBandWidth(value);
    }
    else if (strcmp(which, "SPD") == 0)
    {
        ok = g_motor.setSpeedLoopBandWidth(value);
    }
    else if (strcmp(which, "POS") == 0)
    {
        ok = g_motor.setPosLoopBandWidth(value);
    }
    else if (strcmp(which, "ESO") == 0)
    {
        ok = g_motor.setEsoBandWidth(value);
    }
    else if (strcmp(which, "AB") == 0)
    {
        ok = g_motor.setSpeedABFiltBandWidth(value);
    }
    else
    {
        g_pc_link.sendText("ERR ARG");
        return;
    }
    if (!ok)
    {
        reply_error();
        return;
    }

    char buf[TX_CAPACITY];
    char* p = append_text(buf, "OK BW ");
    p = append_text(p, which);
    *p++ = ' ';
    p = append_float(p, value, REPLY_DECIMALS);
    g_pc_link.send(buf, p);
}

static void cmd_set_zeta(char* cursor)
{
    char* which = next_token(&cursor);
    float value = 0.0f;
    if (which == nullptr || !parse_float(next_token(&cursor), &value))
    {
        g_pc_link.sendText("ERR ARG");
        return;
    }
    to_upper(which);

    /* 与 BW 一样原样转发，不做限幅：阻尼比的合理区间由上位机的 SOFT_RANGES 提示 */
    bool ok;
    if (strcmp(which, "SPD") == 0)
    {
        ok = g_motor.setSpeedLoopDamping(value);
    }
    else if (strcmp(which, "AB") == 0)
    {
        ok = g_motor.setSpeedABFiltDamping(value);
    }
    else
    {
        g_pc_link.sendText("ERR ARG");
        return;
    }
    if (!ok)
    {
        reply_error();
        return;
    }

    char buf[TX_CAPACITY];
    char* p = append_text(buf, "OK ZETA ");
    p = append_text(p, which);
    *p++ = ' ';
    p = append_float(p, value, REPLY_DECIMALS);
    g_pc_link.send(buf, p);
}

static void reply_error(void)
{
    switch (g_motor.getLastError())
    {
        case Motor2204::Error::TIMEOUT:
            g_pc_link.sendText("ERR TIMEOUT");
            break;
        case Motor2204::Error::FAULT:
            g_pc_link.sendText("ERR FAULT");
            break;
        case Motor2204::Error::NOT_SUPPORTED:
            g_pc_link.sendText("ERR UNSUPPORTED");
            break;
        default:
            g_pc_link.sendText("ERR FRAME");
            break;
    }
}

static void reply_debug_error(void)
{
    g_pc_link.sendText("ERR DEBUG");
}

static void reply_value(const char* prefix, float v, uint8_t decimals)
{
    char buf[TX_CAPACITY];
    char* p = append_text(buf, prefix);
    p = append_float(p, v, decimals);
    g_pc_link.send(buf, p);
}

static PcCmd parse_cmd(const char* s)
{
    if (strcmp(s, "PING") == 0) return PcCmd::PING;
    if (strcmp(s, "ADDR") == 0) return PcCmd::ADDR;
    if (strcmp(s, "MODE") == 0) return PcCmd::MODE;
    if (strcmp(s, "ENABLE") == 0) return PcCmd::ENABLE;
    if (strcmp(s, "DISABLE") == 0) return PcCmd::DISABLE;
    if (strcmp(s, "IQ") == 0) return PcCmd::IQ;
    if (strcmp(s, "SPD") == 0) return PcCmd::SPD;
    if (strcmp(s, "POS") == 0) return PcCmd::POS;
    if (strcmp(s, "BW") == 0) return PcCmd::BW;
    if (strcmp(s, "ZETA") == 0) return PcCmd::ZETA;
    if (strcmp(s, "FB") == 0) return PcCmd::FB;
    if (strcmp(s, "VBUS") == 0) return PcCmd::VBUS;
    if (strcmp(s, "SYSREG") == 0) return PcCmd::SYSREG;
    if (strcmp(s, "CLEAR") == 0) return PcCmd::CLEAR;
    if (strcmp(s, "INFO") == 0) return PcCmd::INFO;
    if (strcmp(s, "CALIB") == 0) return PcCmd::CALIB;
    if (strcmp(s, "DADDR") == 0) return PcCmd::DADDR;
    if (strcmp(s, "BAUD") == 0) return PcCmd::BAUD;
    if (strcmp(s, "DEFAULTS") == 0) return PcCmd::DEFAULTS;
    if (strcmp(s, "SAVE") == 0) return PcCmd::SAVE;
    if (strcmp(s, "OVC") == 0) return PcCmd::OVC;
    if (strcmp(s, "ZOFF") == 0) return PcCmd::ZOFF;
    return PcCmd::INVALID;
}

static bool parse_mode(const char* arg, FocLoopMode* mode)
{
    if (strcmp(arg, "CUR") == 0 || strcmp(arg, "CURRENT") == 0)
    {
        *mode = FocLoopMode::Current;
        return true;
    }
    if (strcmp(arg, "SPD") == 0 || strcmp(arg, "SPEED") == 0)
    {
        *mode = FocLoopMode::Speed;
        return true;
    }
    if (strcmp(arg, "POS") == 0 || strcmp(arg, "POSITION") == 0)
    {
        *mode = FocLoopMode::Pos;
        return true;
    }
    uint8_t value = 0;
    if (parse_uint8(arg, &value) && value <= 2u)
    {
        *mode = static_cast<FocLoopMode>(value);
        return true;
    }
    return false;
}

static bool is_digit(char c)
{
    return c >= '0' && c <= '9';
}

static char* next_token(char** cursor)
{
    char* p = *cursor;
    while (*p == ' ')
    {
        p++;
    }
    if (*p == '\0')
    {
        *cursor = p;
        return nullptr;
    }
    char* start = p;
    while (*p != ' ' && *p != '\0')
    {
        p++;
    }
    if (*p == ' ')
    {
        *p = '\0';
        p++;
    }
    *cursor = p;
    return start;
}

static void to_upper(char* s)
{
    for (; *s != '\0'; s++)
    {
        if (*s >= 'a' && *s <= 'z')
        {
            *s = (char)(*s - 'a' + 'A');
        }
    }
}

static bool parse_uint8(const char* s, uint8_t* out)
{
    if (s == nullptr || !is_digit(*s))
    {
        return false;
    }
    uint32_t v = 0;
    while (is_digit(*s))
    {
        v = v * 10u + (uint32_t)(*s - '0');
        if (v > 0xFFu)
        {
            return false;
        }
        s++;
    }
    if (*s != '\0')
    {
        return false;
    }
    *out = (uint8_t)v;
    return true;
}

static bool parse_uint32(const char* s, uint32_t* out)
{
    if (s == nullptr || !is_digit(*s))
    {
        return false;
    }
    uint32_t v = 0;
    while (is_digit(*s))
    {
        const uint32_t digit = (uint32_t)(*s - '0');
        if (v > (0xFFFFFFFFu - digit) / 10u)
        {
            return false;
        }
        v = v * 10u + digit;
        s++;
    }
    if (*s != '\0')
    {
        return false;
    }
    *out = v;
    return true;
}

static bool parse_float(const char* s, float* out)
{
    if (s == nullptr)
    {
        return false;
    }
    bool neg = false;
    if (*s == '+' || *s == '-')
    {
        neg = (*s == '-');
        s++;
    }
    if (!is_digit(*s))
    {
        return false;
    }
    float v = 0.0f;
    while (is_digit(*s))
    {
        v = v * 10.0f + (float)(*s - '0');
        s++;
    }
    if (*s == '.')
    {
        s++;
        float f = 0.1f;
        while (is_digit(*s))
        {
            v += (float)(*s - '0') * f;
            f *= 0.1f;
            s++;
        }
    }
    if (*s != '\0')
    {
        return false;
    }
    *out = neg ? -v : v;
    return true;
}

static char* append_text(char* p, const char* text)
{
    while (*text != '\0')
    {
        *p++ = *text++;
    }
    return p;
}

static char* append_uint(char* p, uint32_t v)
{
    char tmp[10];
    uint8_t n = 0;
    do
    {
        tmp[n++] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v != 0u);
    while (n > 0u)
    {
        *p++ = tmp[--n];
    }
    return p;
}

static char* append_hex(char* p, uint32_t v)
{
    static const char HEX_DIGITS[] = "0123456789ABCDEF";
    p = append_text(p, "0x");
    for (int8_t shift = 28; shift >= 0; shift -= 4)
    {
        *p++ = HEX_DIGITS[(v >> shift) & 0x0Fu];
    }
    return p;
}

static char* append_float(char* p, float v, uint8_t decimals)
{
    if (v < 0.0f)
    {
        *p++ = '-';
        v = -v;
    }
    uint32_t scale = 1;
    for (uint8_t i = 0; i < decimals; i++)
    {
        scale *= 10u;
    }
    const uint32_t scaled = (uint32_t)(v * (float)scale + 0.5f);
    p = append_uint(p, scaled / scale);
    if (decimals > 0u)
    {
        *p++ = '.';
        uint32_t frac = scaled % scale;
        for (uint32_t d = scale / 10u; d > 0u; d /= 10u)
        {
            *p++ = (char)('0' + (frac / d) % 10u);
        }
    }
    return p;
}
