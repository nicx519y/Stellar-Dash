#ifndef SHA256_SIMPLE_H
#define SHA256_SIMPLE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t state[8];
    uint64_t count;
    uint8_t buffer[64];
} sha256_simple_ctx_t;

void sha256_simple_init(sha256_simple_ctx_t* ctx);
void sha256_simple_update(sha256_simple_ctx_t* ctx,
                          const uint8_t* data,
                          size_t len);
void sha256_simple_final(sha256_simple_ctx_t* ctx, uint8_t hash[32]);
int sha256_calculate_raw(const uint8_t* data, size_t len, uint8_t hash[32]);

// 便捷函数：一次性计算SHA256并转换为十六进制字符串
int sha256_calculate(const uint8_t* data, size_t len, char* hex_output);

#ifdef __cplusplus
}
#endif

#endif // SHA256_SIMPLE_H
