#include "cpp_utils.hpp"
#include <string>
#include <cstddef>
#include <cstring>
#include "system_logger.h"
#include "cJSON.h"
#include "stm32h7xx_hal.h"

bool is_valid_utf8(const char* str) {
    if (!str) return false;
    while (*str) {
        unsigned char c = (unsigned char)*str;
        if (c < 0x80) {
            str++;
        } else if ((c & 0xE0) == 0xC0) {
            if ((str[1] & 0xC0) != 0x80) return false;
            str += 2;
        } else if ((c & 0xF0) == 0xE0) {
            if ((str[1] & 0xC0) != 0x80 || (str[2] & 0xC0) != 0x80) return false;
            str += 3;
        } else if ((c & 0xF8) == 0xF0) {
            if ((str[1] & 0xC0) != 0x80 || (str[2] & 0xC0) != 0x80 || (str[3] & 0xC0) != 0x80) return false;
            str += 4;
        } else {
            return false;
        }
    }
    return true;
}

std::string fix_utf8_string(const std::string& input) {
    std::string result;
    result.reserve(input.length());
    const char* str = input.c_str();
    while (*str) {
        unsigned char c = (unsigned char)*str;
        if (c < 0x80) {
            result += *str;
            str++;
        } else if ((c & 0xE0) == 0xC0) {
            if ((str[1] & 0xC0) == 0x80) {
                result += str[0];
                result += str[1];
                str += 2;
            } else {
                str++;
            }
        } else if ((c & 0xF0) == 0xE0) {
            if ((str[1] & 0xC0) == 0x80 && (str[2] & 0xC0) == 0x80) {
                result += str[0];
                result += str[1];
                result += str[2];
                str += 3;
            } else {
                str++;
            }
        } else if ((c & 0xF8) == 0xF0) {
            if ((str[1] & 0xC0) == 0x80 && (str[2] & 0xC0) == 0x80 && (str[3] & 0xC0) == 0x80) {
                result += str[0];
                result += str[1];
                result += str[2];
                result += str[3];
                str += 4;
            } else {
                str++;
            }
        } else {
            str++;
        }
    }
    return result;
}

void safe_strncpy(char* dest, const char* src, size_t dest_size) {
    if (!dest || !src || dest_size == 0) return;
    std::string str_value(src);
    if (!is_valid_utf8(str_value.c_str())) {
        LOG_WARN("UTILS", "Invalid UTF-8 detected in string, attempting to fix");
        str_value = fix_utf8_string(str_value);
        if (!is_valid_utf8(str_value.c_str())) {
            LOG_ERROR("UTILS", "Failed to fix UTF-8 encoding, using fallback");
            std::string fallback;
            for (char c : str_value) {
                if (c >= 32 && c <= 126) {
                    fallback += c;
                } else {
                    fallback += '?';
                }
            }
            str_value = fallback;
        }
    }
    if (str_value.length() >= dest_size) {
        str_value = str_value.substr(0, dest_size - 1);
    }
    strncpy(dest, str_value.c_str(), dest_size - 1);
    dest[dest_size - 1] = '\0';
}

void safe_add_string_to_object(cJSON* obj, const char* key, const char* value) {
    if (!obj || !key || !value) return;
    
    std::string str_value(value);
    if (!is_valid_utf8(str_value.c_str())) {
        LOG_WARN("UTILS", "Invalid UTF-8 detected in string '%s', attempting to fix", key);
        str_value = fix_utf8_string(str_value);
        if (!is_valid_utf8(str_value.c_str())) {
            LOG_ERROR("UTILS", "Failed to fix UTF-8 encoding for key '%s', using fallback", key);
            // 如果修复失败，使用ASCII字符替换
            std::string fallback;
            for (char c : str_value) {
                if (c >= 32 && c <= 126) {
                    fallback += c;
                } else {
                    fallback += '?';
                }
            }
            str_value = fallback;
        }
    }
    
    cJSON_AddStringToObject(obj, key, str_value.c_str());
}

// 简单的哈希函数
static uint32_t simple_hash(const char* str) {
    uint32_t hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c; // hash * 33 + c
    }
    return hash;
}

// 手动将32位整数转换为16进制字符串
static void uint32_to_hex(uint32_t value, char* hex_str, size_t size) {
    if (size < 9) return; // 需要至少9个字符（8个hex + null）
    
    const char hex_chars[] = "0123456789abcdef";
    for (int i = 7; i >= 0; i--) {
        hex_str[i] = hex_chars[value & 0xF];
        value >>= 4;
    }
    hex_str[8] = '\0';
}

std::string generate_unique_id(const char* name) {
    if (!name) {
        return "unknown-0";
    }
    
    // 获取当前时间戳（毫秒）
    uint32_t timestamp = HAL_GetTick();
    
    // 使用递增计数器确保同一毫秒内的唯一性
    static uint32_t counter = 0;
    counter++;
    
    // 使用简单的伪随机数生成
    static uint32_t seed = 0;
    seed = seed * 1103515245 + 12345;
    uint32_t random_num = (seed >> 16) & 0x7FFF; // 15位随机数
    
    // 计算name的哈希值
    uint32_t name_hash = simple_hash(name);
    
    // 组合所有元素生成最终哈希，确保不为0
    // 使用更复杂的组合方式：timestamp + counter + random_num + name_hash
    uint32_t combined_hash = timestamp + (counter << 16) + random_num + name_hash;
    if (combined_hash == 0) {
        combined_hash = timestamp ^ counter ^ random_num ^ name_hash; // 备用组合方式
    }
    
    // 手动转换为16进制字符串
    char hex_str[9];
    uint32_to_hex(combined_hash, hex_str, sizeof(hex_str));
    
    // 只返回16进制后缀作为ID
    return std::string(hex_str);
} 