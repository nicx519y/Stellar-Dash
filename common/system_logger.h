/**
 * @file system_logger.h
 * @brief 日志模块已停用：保留兼容 API，全部为空实现。
 */

#ifndef SYSTEM_LOGGER_H
#define SYSTEM_LOGGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LOG_FLASH_TOTAL_SIZE    (64u * 1024u)
#define LOG_FLASH_SECTOR_SIZE   4096u
#define LOG_FLASH_SECTOR_COUNT  (LOG_FLASH_TOTAL_SIZE / LOG_FLASH_SECTOR_SIZE)
#define LOG_HEADER_SIZE         64u
#define LOG_ENTRY_SIZE          128u
#define LOG_ENTRIES_PER_SECTOR  ((LOG_FLASH_SECTOR_SIZE - LOG_HEADER_SIZE) / LOG_ENTRY_SIZE)

/* ============================================================================
 * 数据结构定义  
 * ========================================================================== */

typedef char LogEntry[LOG_ENTRY_SIZE];

/* ============================================================================
 * 枚举类型定义
 * ========================================================================== */

/**
 * @brief 日志级别枚举
 */
typedef enum {
    LOG_LEVEL_DEBUG = 0,    // 调试信息
    LOG_LEVEL_INFO  = 1,    // 一般信息
    LOG_LEVEL_WARN  = 2,    // 警告信息
    LOG_LEVEL_ERROR = 3,    // 错误信息
    LOG_LEVEL_FATAL = 4,    // 致命错误
    LOG_LEVEL_SYSTEM = 5,   // 系统级消息
    LOG_LEVEL_COUNT
} LogLevel;

/**
 * @brief 日志操作结果枚举
 */
typedef enum {
    LOG_RESULT_SUCCESS = 0,          // 操作成功
    LOG_RESULT_ERROR_INIT,           // 初始化失败
    LOG_RESULT_ERROR_BUFFER_FULL,    // 缓冲区满
    LOG_RESULT_ERROR_FLASH_WRITE,    // Flash写入失败
    LOG_RESULT_ERROR_INVALID_PARAM,  // 无效参数
    LOG_RESULT_ERROR_NOT_INITIALIZED // 未初始化
} LogResult;

static inline LogResult Logger_Init(bool is_bootloader, LogLevel min_level) {
    (void)is_bootloader;
    (void)min_level;
    return LOG_RESULT_SUCCESS;
}

static inline LogResult Logger_Deinit(void) { return LOG_RESULT_SUCCESS; }

static inline LogResult Logger_Log(LogLevel level, const char* component, const char* format, ...) {
    (void)level;
    (void)component;
    (void)format;
    return LOG_RESULT_SUCCESS;
}

static inline LogResult Logger_LogDelay(LogLevel level, const char* component, const char* format, ...) {
    (void)level;
    (void)component;
    (void)format;
    return LOG_RESULT_SUCCESS;
}

static inline LogResult Logger_Flush(void) { return LOG_RESULT_SUCCESS; }
static inline LogResult Logger_AutoFlushCheck(void) { return LOG_RESULT_SUCCESS; }
static inline LogResult Logger_ClearFlash(void) { return LOG_RESULT_SUCCESS; }

static inline LogResult Logger_GetStatus(uint32_t* sector_index, uint32_t* write_index,
                                         uint32_t* queue_start, uint32_t* count) {
    if (sector_index) *sector_index = 0;
    if (write_index) *write_index = 0;
    if (queue_start) *queue_start = 0;
    if (count) *count = 0;
    return LOG_RESULT_SUCCESS;
}

static inline LogResult Logger_PrintAllLogs(int (*print_func)(const char* format, ...)) {
    (void)print_func;
    return LOG_RESULT_SUCCESS;
}

static inline LogResult Logger_ShowSectorInfo(int (*print_func)(const char* format, ...)) {
    (void)print_func;
    return LOG_RESULT_SUCCESS;
}

static inline LogResult Logger_ShowGlobalState(int (*print_func)(const char* format, ...)) {
    (void)print_func;
    return LOG_RESULT_SUCCESS;
}

static inline LogResult Logger_GetSortedSectors(uint32_t* sector_array, uint32_t* actual_count) {
    (void)sector_array;
    if (actual_count) *actual_count = 0;
    return LOG_RESULT_SUCCESS;
}

static inline LogResult Logger_GetSectorLogs(uint32_t sector_index, LogEntry* log_array, uint32_t* actual_count) {
    (void)sector_index;
    (void)log_array;
    if (actual_count) *actual_count = 0;
    return LOG_RESULT_SUCCESS;
}

/* ============================================================================
 * 便捷宏定义
 * ========================================================================== */

#define LOG_DEBUG(component, format, ...)   Logger_Log(LOG_LEVEL_DEBUG, component, format, ##__VA_ARGS__)
#define LOG_INFO(component, format, ...)    Logger_Log(LOG_LEVEL_INFO, component, format, ##__VA_ARGS__)
#define LOG_WARN(component, format, ...)    Logger_Log(LOG_LEVEL_WARN, component, format, ##__VA_ARGS__)
#define LOG_ERROR(component, format, ...)   Logger_Log(LOG_LEVEL_ERROR, component, format, ##__VA_ARGS__)
#define LOG_FATAL(component, format, ...)   Logger_Log(LOG_LEVEL_FATAL, component, format, ##__VA_ARGS__)

// 延迟写盘版本的宏定义
#define LOG_DEBUG_DELAY(component, format, ...)   Logger_LogDelay(LOG_LEVEL_DEBUG, component, format, ##__VA_ARGS__)
#define LOG_INFO_DELAY(component, format, ...)    Logger_LogDelay(LOG_LEVEL_INFO, component, format, ##__VA_ARGS__)
#define LOG_WARN_DELAY(component, format, ...)    Logger_LogDelay(LOG_LEVEL_WARN, component, format, ##__VA_ARGS__)
#define LOG_ERROR_DELAY(component, format, ...)   Logger_LogDelay(LOG_LEVEL_ERROR, component, format, ##__VA_ARGS__)
#define LOG_FATAL_DELAY(component, format, ...)   Logger_LogDelay(LOG_LEVEL_FATAL, component, format, ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif // SYSTEM_LOGGER_H
