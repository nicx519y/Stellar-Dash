#pragma once
#include <string>
#include <cstddef>

// 前向声明
struct cJSON;

// 检查字符串是否为有效UTF-8编码
bool is_valid_utf8(const char* str);

// 修复UTF-8字符串，移除无效字符
std::string fix_utf8_string(const std::string& input);

// 安全地复制字符串到固定大小的缓冲区，确保UTF-8编码有效
void safe_strncpy(char* dest, const char* src, size_t dest_size);

// 安全地添加字符串到JSON对象，确保UTF-8编码有效
void safe_add_string_to_object(cJSON* obj, const char* key, const char* value);

// 生成唯一ID，使用name、随机数和哈希
std::string generate_unique_id(const char* name); 