/**
 * @file system_logger.h
 * @brief STM32 HBox简化版系统日志模块 - 固定长度数组设计
 * @version 3.0.0
 * @date 2024-12-20
 * 
 * 设计原理：
 * - 扇区头部64字节：存储下次写入地址、队列开始地址、当前日志条数
 * - 每条日志128字节：以\n结尾的字符串
 * - 数组操作：读取整个数组→修改→写回Flash，循环覆盖
 */

#ifndef SYSTEM_LOGGER_H
#define SYSTEM_LOGGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ============================================================================
 * 配置常量定义
 * ========================================================================== */

// HBox Flash内存布局 (8MB总容量):
// 0x00780000-0x007FFFFF   0x90780000-0x907FFFFF   512KB     【日志存储区域】

// Flash存储配置
#define LOG_FLASH_BASE_ADDR         0x90580000      // 虚拟地址
#define LOG_FLASH_PHYSICAL_ADDR     0x00580000      // 物理地址
#define LOG_FLASH_TOTAL_SIZE        (512 * 1024)    // 512KB日志存储空间
#define LOG_FLASH_SECTOR_SIZE       4096            // 4KB扇区大小
#define LOG_FLASH_SECTOR_COUNT      (LOG_FLASH_TOTAL_SIZE / LOG_FLASH_SECTOR_SIZE) // 128个扇区

// 日志配置
#define LOG_HEADER_SIZE             64              // 头部固定64字节
#define LOG_ENTRY_SIZE              128             // 每条日志固定128字节
#define LOG_ENTRIES_PER_SECTOR      ((LOG_FLASH_SECTOR_SIZE - LOG_HEADER_SIZE) / LOG_ENTRY_SIZE) // 每扇区31条日志
#define LOG_MAX_MESSAGE_LENGTH      (LOG_ENTRY_SIZE - 1) // 消息最大长度（预留\n）

// 内存缓冲配置
#define LOG_MEMORY_BUFFER_SIZE      (32 * LOG_ENTRY_SIZE)  // 内存缓冲32条日志 (2KB)
#define LOG_AUTO_FLUSH_INTERVAL_MS  5000                   // 5秒自动刷新间隔

// 混合方案配置
#define LOG_GLOBAL_STATE_SECTOR     (LOG_FLASH_SECTOR_COUNT - 1)  // 使用最后一个扇区存储全局状态
#define LOG_GLOBAL_STATE_MAGIC      0x484C4753              // "HLGS" (HBox Logger Global State)

/* ============================================================================
 * 数据结构定义  
 * ========================================================================== */

/**
 * @brief 全局日志状态结构 (混合方案的快速启动索引)
 */
typedef struct {
    uint32_t magic1;                   // 魔术数字 0x484C4753 ("HLGS")
    uint32_t last_active_sector;       // 最后已知的活跃扇区索引
    uint32_t global_sequence;          // 全局序列计数器
    uint32_t boot_counter;             // 启动计数器
    uint32_t last_update_timestamp;    // 最后更新时间戳
    uint32_t magic2;                   // 魔术数字备份 (双重验证)
    uint32_t last_active_sector_backup; // 活跃扇区索引备份
    uint32_t checksum;                 // 校验和 (前面所有字段的异或值)
    uint8_t  reserved[32];             // 预留空间 (总共64字节)
} __attribute__((packed)) LogGlobalState;

/**
 * @brief 扇区头部结构 (64字节)
 */
typedef struct {
    uint32_t magic;                    // 魔术数字 0x484C4F47 ("HLOG")
    uint32_t next_write_index;         // 下一个写入位置索引 (0-30)
    uint32_t queue_start_index;        // 队列开始位置索引 (0-30)
    uint32_t current_count;            // 当前日志条数 (0-31)
    uint32_t total_written;            // 历史写入总数
    uint32_t sector_index;             // 扇区索引
    uint32_t timestamp_first;          // 第一条日志时间戳
    uint32_t timestamp_last;           // 最后一条日志时间戳
    uint32_t boot_counter;             // 启动计数器
    uint32_t sequence_counter;         // 全局序列计数器
    uint8_t  is_active;                // 扇区是否活跃
    uint8_t  reserved[23];             // 预留空间 (总共64字节)
} __attribute__((packed)) LogSectorHeader;

/**
 * @brief 日志条目 (128字节字符串)
 * 格式: "[HH:MM:SS.mmm] [LEVEL] COMPONENT: MESSAGE\n"
 */
typedef char LogEntry[LOG_ENTRY_SIZE];

/**
 * @brief 扇区数据结构 (4KB)
 */
typedef struct {
    LogSectorHeader header;                           // 64字节头部
    LogEntry entries[LOG_ENTRIES_PER_SECTOR];         // 31条日志，每条128字节
} __attribute__((packed)) LogSector;

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

/* ============================================================================
 * 核心API函数
 * ========================================================================== */

/**
 * @brief 初始化日志模块
 * @param is_bootloader 是否运行在bootloader模式
 * @param min_level 最小记录级别
 * @return 操作结果
 */
LogResult Logger_Init(bool is_bootloader, LogLevel min_level);

/**
 * @brief 反初始化日志模块
 * @return 操作结果
 */
LogResult Logger_Deinit(void);

/**
 * @brief 记录日志
 * @param level 日志级别
 * @param component 组件名称
 * @param format 格式化字符串 (printf格式)
 * @param ... 可变参数
 * @return 操作结果
 */
LogResult Logger_Log(LogLevel level, const char* component, const char* format, ...);

/**
 * @brief 记录日志（延迟写盘版本）
 * @param level 日志级别
 * @param component 组件名称
 * @param format 格式化字符串 (printf格式)
 * @param ... 可变参数
 * @return 操作结果
 */
LogResult Logger_LogDelay(LogLevel level, const char* component, const char* format, ...);

/**
 * @brief 强制刷新缓冲区到Flash
 * @return 操作结果
 */
LogResult Logger_Flush(void);

/**
 * @brief 检查是否需要自动刷新 (在主循环中定期调用)
 * @return 操作结果
 */
LogResult Logger_AutoFlushCheck(void);

/**
 * @brief 清空Flash中的所有日志
 * @return 操作结果
 */
LogResult Logger_ClearFlash(void);

/**
 * @brief 获取当前写入指针状态 (调试用)
 * @param sector_index 返回当前写入扇区索引
 * @param write_index 返回扇区内写入索引
 * @param queue_start 返回队列开始索引
 * @param count 返回当前日志条数
 * @return 操作结果
 */
LogResult Logger_GetStatus(uint32_t* sector_index, uint32_t* write_index, 
                           uint32_t* queue_start, uint32_t* count);

/**
 * @brief 打印所有Flash中的日志到指定输出函数
 * @param print_func 输出函数指针 (例如printf)
 * @return 操作结果
 */
LogResult Logger_PrintAllLogs(int (*print_func)(const char* format, ...));

/**
 * @brief 显示扇区头部信息 (调试用)
 * @param print_func 输出函数指针 (例如printf)
 * @return 操作结果
 */
LogResult Logger_ShowSectorInfo(int (*print_func)(const char* format, ...));

/**
 * @brief 显示全局状态信息 (混合方案调试用)
 * @param print_func 输出函数指针 (例如printf)
 * @return 操作结果
 */
LogResult Logger_ShowGlobalState(int (*print_func)(const char* format, ...));

/**
 * @brief 获取有内容的日志扇区总数并按时间顺序排序
 * @param sector_array 返回排序后的扇区编号数组 (调用者提供的数组，至少128个元素)
 * @param actual_count 返回实际有效扇区数量
 * @return 操作结果
 * @note 扇区按启动计数器和序列计数器排序，确保时间顺序正确
 */
LogResult Logger_GetSortedSectors(uint32_t* sector_array, uint32_t* actual_count);

/**
 * @brief 获取指定扇区内的所有日志文本
 * @param sector_index 扇区编号 (0 - LOG_FLASH_SECTOR_COUNT-2)
 * @param log_array 返回日志文本数组 (调用者提供的数组，至少31个元素)
 * @param actual_count 返回实际日志条数
 * @return 操作结果
 * @note 日志按写入顺序正序排列，每条日志是以null结尾的字符串
 */
LogResult Logger_GetSectorLogs(uint32_t sector_index, LogEntry* log_array, uint32_t* actual_count);

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