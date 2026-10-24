#pragma once

/*================ SYSTEM INTERFACE =================*/
#define SYSTEM_INTERFACE "main.h"
/*===================================================*/
#ifdef __cplusplus
extern "C" {
#endif

#include SYSTEM_INTERFACE
void app_main(void);

/*
 * PC link text protocol (USART2, 115200 8N1, one command per line, reply \r\n):
 *
 *   PING                  -> PONG
 *   ADDR <0..7>           -> OK ADDR <n>      (本机与下位机通信用的地址/tag，不是写设备地址)
 *   MODE <cur|spd|pos>    -> OK MODE <0|1|2>
 *   ENABLE                -> OK ENABLE
 *   DISABLE               -> OK DISABLE
 *   IQ <float A>          -> OK IQ <f>
 *   SPD <float rad/s>     -> OK SPD <f>
 *   POS <float rad>       -> OK POS <f>
 *   BW <CUR|SPD|POS|ESO|AB> <float hz> -> OK BW <name> <f>
 *   FB                    -> FB <current> <speed> <position>
 *   VBUS                  -> VBUS <f>
 *   SYSREG                -> SYSREG <0xraw> <state> <0xwarning> <0xfault>
 *                            state: 0 初始化 / 1 运行 / 2 故障 / 3 校准中
 *                            warning: 0x10 flash 读失败, 0x20 编码器初始化失败,
 *                                     0x40 母线跌落, 0x80 母线回充
 *                            fault:   0x20 过流, 0x40 过压, 0x80 欠压
 *   CLEAR                 -> OK CLEAR        (清故障，仅故障态有效)
 *
 *   debug commands (设备必须上电时拉低 PB0，且先发 INFO 确认):
 *   INFO                  -> INFO <0xVer> <0xChipId> <0xDevId>   (同时确认 DEBUG 模式)
 *   CALIB                 -> START CALIB ... OK CALIB  (约 1.5s，电机须能自由转动)
 *   DADDR                 -> DADDR <n>        (读设备里存的地址；地址只能在下位机上转电机修改)
 *   BAUD <1200..>         -> OK BAUD <n>      (写波特率，重启后生效，需 SAVE 落盘)
 *   DEFAULTS              -> OK DEFAULTS      (恢复出厂配置，需 SAVE 落盘)
 *   SAVE                  -> OK SAVE          (写 flash)
 *   OVC <float A> <ms>    -> OK OVC <f> <n>   (过流阈值/持续时间，需 SAVE 落盘)
 *
 *   errors: ERR CMD / ERR ARG / ERR LINE / ERR TIMEOUT / ERR FRAME / ERR FAULT /
 *           ERR UNSUPPORTED / ERR DADDR /
 *           ERR DEBUG (下位机不在 DEBUG 模式或未先发 INFO) / ERR VBUS (母线电压过低，禁止校准)
 */

#ifdef __cplusplus
}
#endif
