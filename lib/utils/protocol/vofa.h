#pragma once

/// VOFA JustFloat 协议 —— 将多个 float 打包为帧
/// 帧格式: [float1(4B)][float2(4B)]...[floatN(4B)][0x00,0x00,0x80,0x7F(4B)]
/// 返回写入 buffer 的总字节数（含尾部）

#ifdef __cplusplus
#include <cstdint>
#include <cstring>
#include <cstddef>

/// 将 count 个 float 打包为 JustFloat 帧（自动追加 +Inf 尾部）
/// 用法:
///   float vals[8];
///   for (...) { vals[i] = ...; }
///   vofa_justfloat_pack(buf, vals, 8);
inline size_t vofa_justfloat_pack(char* buffer, const float* data, size_t count) {
    size_t offset = count * sizeof(float);
    memcpy(buffer, data, offset);
    const uint32_t tail = 0x7F800000u;
    memcpy(buffer + offset, &tail, sizeof(tail));
    return offset + sizeof(tail);
}
#endif 