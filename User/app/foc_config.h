#pragma once

/* ===== 通信链路选择（编译期宏，二选一）=====
 * 两套链路共用 USART1 + DMA1_Channel6，只能选其一：
 *   FOC_LINK_TM       = 0：JustFloat 2kHz DMA 遥测（只发不收，配网页波形）
 *   FOC_LINK_PROTOCOL = 1：protocol 命令/应答协议（DeviceProtocolRx/Tx，半双工）
 * 也可以在编译选项里 -DFOC_LINK_MODE=1 覆盖。 */
#define FOC_LINK_TM       0
#define FOC_LINK_PROTOCOL 1

#ifndef FOC_LINK_MODE
#define FOC_LINK_MODE     FOC_LINK_PROTOCOL
#endif

#if (FOC_LINK_MODE != FOC_LINK_TM) && (FOC_LINK_MODE != FOC_LINK_PROTOCOL)
#error "FOC_LINK_MODE must be FOC_LINK_TM(0) or FOC_LINK_PROTOCOL(1)"
#endif
