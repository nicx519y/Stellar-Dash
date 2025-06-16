#ifndef INTEL_HEX_PARSER_H
#define INTEL_HEX_PARSER_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Intel HEX 记录类型
typedef enum {
    HEX_RECORD_DATA = 0x00,              // 数据记录
    HEX_RECORD_END_OF_FILE = 0x01,       // 文件结束记录
    HEX_RECORD_EXT_SEGMENT = 0x02,       // 扩展段地址记录
    HEX_RECORD_START_SEGMENT = 0x03,     // 开始段地址记录
    HEX_RECORD_EXT_LINEAR = 0x04,        // 扩展线性地址记录
    HEX_RECORD_START_LINEAR = 0x05       // 开始线性地址记录
} hex_record_type_t;

// Intel HEX 解析结果
typedef struct {
    uint8_t* binary_data;     // 解析后的二进制数据
    uint32_t binary_size;     // 二进制数据大小
    uint32_t start_address;   // 起始地址
    uint32_t end_address;     // 结束地址
} hex_parse_result_t;

/**
 * @brief 解析Intel HEX格式数据为二进制数据
 * @param hex_data Intel HEX格式的文本数据
 * @param hex_size HEX数据的大小
 * @param result 解析结果输出
 * @return 0 = 成功, -1 = 失败
 */
int parse_intel_hex(const char* hex_data, size_t hex_size, hex_parse_result_t* result);

/**
 * @brief 释放解析结果中分配的内存
 * @param result 解析结果
 */
void free_hex_parse_result(hex_parse_result_t* result);

#ifdef __cplusplus
}
#endif

#endif // INTEL_HEX_PARSER_H 