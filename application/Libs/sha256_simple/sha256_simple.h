#ifndef SHA256_SIMPLE_H
#define SHA256_SIMPLE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// 便捷函数：一次性计算SHA256并转换为十六进制字符串
int sha256_calculate(const uint8_t* data, size_t len, char* hex_output);

#ifdef __cplusplus
}
#endif

#endif // SHA256_SIMPLE_H 