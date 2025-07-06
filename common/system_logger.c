/**
 * @file system_logger.c
 * @brief STM32 HBox简化版系统日志模块 - 固定长度数组设计
 * @version 3.0.0
 * @date 2024-12-20
 * 
 * 设计原理：
 * 1. 扇区头部64字节：存储下次写入地址、队列开始地址、当前日志条数
 * 2. 每条日志64字节：以\n结尾的字符串
 * 3. 数组操作：读取整个数组→修改→写回Flash，循环覆盖
 * 
 * 使用示例:
 * ```c
 * // 1. 初始化日志系统
 * LogResult result = Logger_Init(false, LOG_LEVEL_DEBUG);
 * 
 * // 2. 记录日志
 * Logger_Log(LOG_LEVEL_INFO, "MAIN", "System started, version %d.%d", 1, 0);
 * Logger_Log(LOG_LEVEL_ERROR, "ADC", "Sensor reading failed: %d", error_code);
 * 
 * // 3. 在主循环中定期检查自动刷新
 * while (1) {
 *     Logger_AutoFlushCheck();  // 每5秒自动刷新到Flash
 *     // 其他业务逻辑...
 *     HAL_Delay(100);
 * }
 * 
 * // 4. 程序结束前刷新并清理
 * Logger_Flush();
 * Logger_Deinit();
 * ```
 */

#include "system_logger.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stm32h7xx_hal.h>

/* ============================================================================
 * 外部依赖函数声明
 * ========================================================================== */

// QSPI Flash操作函数声明 - 来自qspi-w25q64.c
extern int8_t QSPI_W25Qxx_WriteBuffer_WithXIPOrNot(uint8_t* pBuffer, uint32_t WriteAddr, uint32_t NumByteToWrite);
extern int8_t QSPI_W25Qxx_ReadBuffer_WithXIPOrNot(uint8_t* pBuffer, uint32_t ReadAddr, uint32_t NumByteToRead);
extern int8_t QSPI_W25Qxx_SectorErase(uint32_t SectorAddress);
extern int8_t QSPI_W25Qxx_WritePage(uint8_t* pBuffer, uint32_t WriteAddr, uint16_t NumByteToWrite);

// QSPI驱动返回值定义
#define QSPI_W25Qxx_OK              0
#define W25Qxx_ERROR_TRANSMIT      -1
#define W25Qxx_ERROR_AUTOPOLLING   -2
#define W25Qxx_ERROR_WriteEnable   -3

/* ============================================================================
 * 内部常量定义
 * ========================================================================== */

#define LOG_MAGIC_NUMBER           0x484C4F47       // "HLOG" 魔术数字

/* ============================================================================
 * 内部数据结构和全局变量
 * ========================================================================== */

/**
 * @brief 日志管理器状态
 */
typedef struct {
    bool              is_initialized;        // 是否已初始化
    bool              is_bootloader_mode;    // 是否运行在bootloader模式
    LogLevel          minimum_level;         // 最小记录级别
    uint32_t          last_flush_time;       // 上次刷新时间
    uint32_t          current_sector;        // 当前写入扇区
    uint32_t          global_sequence;       // 全局序列计数器
    uint32_t          boot_counter;          // 启动计数器
    volatile bool     is_writing;            // 正在写入标志
    
    // 内存缓冲区
    LogEntry          memory_buffer[32];     // 内存缓冲32条日志
    uint32_t          buffer_count;          // 缓冲区中的日志条数
} LoggerState;

// 全局日志管理器状态
static LoggerState g_logger_state = {0};

// 简单锁定宏 (裸机环境下关闭中断)
#define LOGGER_LOCK()       do { __disable_irq(); g_logger_state.is_writing = true; } while(0)
#define LOGGER_UNLOCK()     do { g_logger_state.is_writing = false; __enable_irq(); } while(0)

/* ============================================================================
 * 私有函数声明
 * ========================================================================== */

static uint32_t get_current_timestamp_ms(void);
static LogResult write_to_flash(uint32_t address, const uint8_t* data, size_t size);
static LogResult read_from_flash(uint32_t address, uint8_t* data, size_t size);
static LogResult erase_flash_sector(uint32_t sector_index);
static LogResult format_log_entry(LogLevel level, const char* component, const char* message, LogEntry entry);
static LogResult add_entry_to_memory_buffer(const LogEntry entry);
static LogResult flush_memory_buffer_to_flash(void);
static LogResult switch_to_next_sector(void);
static const char* get_level_string(LogLevel level);
static LogResult logger_log_internal(LogLevel level, const char* component, bool immediate_flush, const char* format, va_list args);

// 混合方案：快速启动相关函数
static LogResult read_global_state(LogGlobalState* state);
static LogResult write_global_state(const LogGlobalState* state);
static LogResult quick_start_init(LogLevel min_level);
static LogResult full_scan_init(LogLevel min_level);
static uint32_t calculate_checksum(const LogGlobalState* state);
static bool verify_global_state(const LogGlobalState* state);

/* ============================================================================
 * 工具函数实现
 * ========================================================================== */

static uint32_t get_current_timestamp_ms(void) {
    return HAL_GetTick();
}

static const char* get_level_string(LogLevel level) {
    switch (level) {
        case LOG_LEVEL_DEBUG:  return "DEBUG";
        case LOG_LEVEL_INFO:   return "INFO";
        case LOG_LEVEL_WARN:   return "WARN";
        case LOG_LEVEL_ERROR:  return "ERROR";
        case LOG_LEVEL_FATAL:  return "FATAL";
        case LOG_LEVEL_SYSTEM: return "SYSTEM";
        default:               return "UNKNOWN";
    }
}

/* ============================================================================
 * Flash操作封装函数
 * ========================================================================== */

static LogResult write_to_flash(uint32_t address, const uint8_t* data, size_t size) {
    // 转换为物理地址
    uint32_t physical_addr = address - LOG_FLASH_BASE_ADDR + LOG_FLASH_PHYSICAL_ADDR;
    
    // 参数检查
    if (!data || size == 0) {
        return LOG_RESULT_ERROR_INVALID_PARAM;
    }
    
    // 关键修复：如果要写入的数据不是整个扇区，需要使用读-修改-写策略
    const uint32_t sector_size = 4096; // 4KB扇区大小
    uint32_t sector_start = physical_addr & ~(sector_size - 1);
    uint32_t offset_in_sector = physical_addr - sector_start;
    
    // 如果写入的数据跨越扇区边界或者不是从扇区开始写入，需要特殊处理
    if (offset_in_sector != 0 || size != sector_size) {
        // 读-修改-写策略
        static uint8_t sector_buffer[4096]; // 4KB缓冲区
        
        // 1. 读取整个扇区到缓冲区
        int8_t result = QSPI_W25Qxx_ReadBuffer_WithXIPOrNot(sector_buffer, sector_start, sector_size);
        if (result != QSPI_W25Qxx_OK) {
            return LOG_RESULT_ERROR_FLASH_WRITE;
        }
        
        // 2. 在缓冲区中修改指定位置的数据
        memcpy(sector_buffer + offset_in_sector, data, size);
        
        // 3. 擦除扇区
        result = QSPI_W25Qxx_SectorErase(sector_start);
        if (result != QSPI_W25Qxx_OK) {
            return LOG_RESULT_ERROR_FLASH_WRITE;
        }
        
        // 4. 写回整个扇区
        result = QSPI_W25Qxx_WritePage(sector_buffer, sector_start, sector_size < 256 ? sector_size : 256);
        if (result != QSPI_W25Qxx_OK) {
            return LOG_RESULT_ERROR_FLASH_WRITE;
        }
        
        // 如果扇区大于256字节，需要分页写入
        if (sector_size > 256) {
            for (uint32_t i = 256; i < sector_size; i += 256) {
                uint32_t write_size = (sector_size - i) > 256 ? 256 : (sector_size - i);
                result = QSPI_W25Qxx_WritePage(sector_buffer + i, sector_start + i, write_size);
                if (result != QSPI_W25Qxx_OK) {
                    return LOG_RESULT_ERROR_FLASH_WRITE;
                }
            }
        }
    } else {
        // 写入整个扇区，可以直接使用原有逻辑
        int8_t result = QSPI_W25Qxx_WriteBuffer_WithXIPOrNot((uint8_t*)data, physical_addr, size);
        if (result != QSPI_W25Qxx_OK) {
            return LOG_RESULT_ERROR_FLASH_WRITE;
        }
    }
    
    return LOG_RESULT_SUCCESS;
}

static LogResult read_from_flash(uint32_t address, uint8_t* data, size_t size) {
    // 转换为物理地址
    uint32_t physical_addr = address - LOG_FLASH_BASE_ADDR + LOG_FLASH_PHYSICAL_ADDR;
    
    // 参数检查
    if (!data || size == 0) {
        return LOG_RESULT_ERROR_INVALID_PARAM;
    }
    
    int8_t result = QSPI_W25Qxx_ReadBuffer_WithXIPOrNot(data, physical_addr, size);
    
    if (result != QSPI_W25Qxx_OK) {
        return LOG_RESULT_ERROR_FLASH_WRITE;
    }
    
    return LOG_RESULT_SUCCESS;
}

static LogResult erase_flash_sector(uint32_t sector_index) {
    if (sector_index >= LOG_FLASH_SECTOR_COUNT) {
        return LOG_RESULT_ERROR_INVALID_PARAM;
    }
    
    uint32_t sector_addr = LOG_FLASH_PHYSICAL_ADDR + sector_index * LOG_FLASH_SECTOR_SIZE;
    int8_t result = QSPI_W25Qxx_SectorErase(sector_addr);
    
    if (result != QSPI_W25Qxx_OK) {
        return LOG_RESULT_ERROR_FLASH_WRITE;
    }
    
    return LOG_RESULT_SUCCESS;
}

/* ============================================================================
 * 日志条目和缓冲区管理
 * ========================================================================== */

static LogResult format_log_entry(LogLevel level, const char* component, const char* message, LogEntry entry) {
    if (!component || !message) {
        return LOG_RESULT_ERROR_INVALID_PARAM;
    }

    // 清零日志条目
    memset(entry, 0, LOG_ENTRY_SIZE);

    // 获取时间戳（系统启动后的毫秒数）
    uint32_t timestamp = get_current_timestamp_ms();
    uint32_t sec = timestamp / 1000;
    uint32_t ms = timestamp % 1000;

    // 计算时分秒
    uint32_t hours = sec / 3600;
    uint32_t minutes = (sec % 3600) / 60;
    uint32_t seconds = sec % 60;

    // 格式化日志条目 - 只显示时分秒和毫秒 [HH:MM:SS.mmm]
    snprintf(entry, LOG_ENTRY_SIZE, 
             "[%02lu:%02lu:%02lu.%03lu] [%s] %s: %s\n",
             (unsigned long)hours, (unsigned long)minutes, (unsigned long)seconds, (unsigned long)ms,
             get_level_string(level), component, message);

    return LOG_RESULT_SUCCESS;
}

static LogResult add_entry_to_memory_buffer(const LogEntry entry) {
    if (g_logger_state.buffer_count >= 32) {
        // 缓冲区满，强制刷新
        LogResult result = flush_memory_buffer_to_flash();
        if (result != LOG_RESULT_SUCCESS) {
            return result;
        }
    }

    // 复制日志条目到缓冲区
    memcpy(g_logger_state.memory_buffer[g_logger_state.buffer_count], entry, LOG_ENTRY_SIZE);
    g_logger_state.buffer_count++;

    return LOG_RESULT_SUCCESS;
}

static LogResult switch_to_next_sector(void) {
    // 先将当前扇区标记为非活跃
    LogSectorHeader current_header;
    uint32_t current_sector_addr = LOG_FLASH_BASE_ADDR + g_logger_state.current_sector * LOG_FLASH_SECTOR_SIZE;
    
    if (read_from_flash(current_sector_addr, (uint8_t*)&current_header, sizeof(LogSectorHeader)) == LOG_RESULT_SUCCESS) {
        if (current_header.magic == LOG_MAGIC_NUMBER) {
            current_header.is_active = 0; // 标记为非活跃
            write_to_flash(current_sector_addr, (uint8_t*)&current_header, sizeof(LogSectorHeader));
        }
    }
    
    // 切换到下一个扇区
    uint32_t next_sector = (g_logger_state.current_sector + 1) % LOG_FLASH_SECTOR_COUNT;
    
    // 擦除下一个扇区
    LogResult result = erase_flash_sector(next_sector);
    if (result != LOG_RESULT_SUCCESS) {
        return result;
    }

    // 初始化新扇区头部
    LogSectorHeader new_header;
    memset(&new_header, 0, sizeof(LogSectorHeader));
    
    new_header.magic = LOG_MAGIC_NUMBER;
    new_header.next_write_index = 0;
    new_header.queue_start_index = 0;
    new_header.current_count = 0;
    new_header.total_written = 0;
    new_header.sector_index = next_sector;
    new_header.timestamp_first = 0;
    new_header.timestamp_last = 0;
    new_header.boot_counter = g_logger_state.boot_counter;
    new_header.sequence_counter = ++g_logger_state.global_sequence;  // 增加序列号
    new_header.is_active = 1;

    // 写入扇区头部（只写入64字节头部）
    uint32_t sector_addr = LOG_FLASH_BASE_ADDR + next_sector * LOG_FLASH_SECTOR_SIZE;
    result = write_to_flash(sector_addr, (uint8_t*)&new_header, sizeof(LogSectorHeader));
    if (result != LOG_RESULT_SUCCESS) {
        return result;
    }

    // 更新全局状态
    g_logger_state.current_sector = next_sector;
    
    // 更新全局状态索引（混合方案）
    LogGlobalState global_state;
    memset(&global_state, 0, sizeof(LogGlobalState));
    
    global_state.magic1 = LOG_GLOBAL_STATE_MAGIC;
    global_state.magic2 = LOG_GLOBAL_STATE_MAGIC;
    global_state.last_active_sector = next_sector;
    global_state.last_active_sector_backup = next_sector;
    global_state.global_sequence = g_logger_state.global_sequence;
    global_state.boot_counter = g_logger_state.boot_counter;
    global_state.last_update_timestamp = get_current_timestamp_ms();
    global_state.checksum = calculate_checksum(&global_state);
    
    // 写入全局状态（错误不影响主要功能）
    write_global_state(&global_state);
    
    return LOG_RESULT_SUCCESS;
}

static LogResult flush_memory_buffer_to_flash(void) {

    if (g_logger_state.buffer_count == 0) {
        return LOG_RESULT_SUCCESS; // 没有数据需要刷新
    }

    // 只读取当前扇区的头部
    LogSectorHeader header;
    uint32_t sector_addr = LOG_FLASH_BASE_ADDR + g_logger_state.current_sector * LOG_FLASH_SECTOR_SIZE;
    
    LogResult result = read_from_flash(sector_addr, (uint8_t*)&header, sizeof(LogSectorHeader));
    if (result != LOG_RESULT_SUCCESS) {
        return result;
    }

    // 检查扇区头部有效性
    if (header.magic != LOG_MAGIC_NUMBER) {
        memset(&header, 0, sizeof(LogSectorHeader));
        header.magic = LOG_MAGIC_NUMBER;
        header.next_write_index = 0;
        header.queue_start_index = 0;
        header.current_count = 0;
        header.sector_index = g_logger_state.current_sector;
        header.boot_counter = g_logger_state.boot_counter;
        header.sequence_counter = g_logger_state.global_sequence;
        header.is_active = 1;
    }

    // 检查是否需要切换扇区（在写入之前检查）
    if (header.current_count + g_logger_state.buffer_count > LOG_ENTRIES_PER_SECTOR) {
        // 当前扇区容量不足，切换到下一个扇区
        result = switch_to_next_sector();
        if (result != LOG_RESULT_SUCCESS) {
            return result;
        }
        
        // 重新读取新扇区的头部
        sector_addr = LOG_FLASH_BASE_ADDR + g_logger_state.current_sector * LOG_FLASH_SECTOR_SIZE;
        result = read_from_flash(sector_addr, (uint8_t*)&header, sizeof(LogSectorHeader));
        if (result != LOG_RESULT_SUCCESS) {
            return result;
        }
    }

    // 逐条写入日志条目
    for (uint32_t i = 0; i < g_logger_state.buffer_count; i++) {
        uint32_t write_index = header.next_write_index;
        
        // 检查是否需要循环覆盖
        if (header.current_count >= LOG_ENTRIES_PER_SECTOR) {
            // 扇区满了，循环覆盖最旧的日志
            write_index = header.queue_start_index;
            header.queue_start_index = (header.queue_start_index + 1) % LOG_ENTRIES_PER_SECTOR;
        } else {
            // 扇区未满，简单增加计数
            header.current_count++;
        }
        
        // 计算日志条目在Flash中的具体地址
        uint32_t entry_addr = sector_addr + LOG_HEADER_SIZE + write_index * LOG_ENTRY_SIZE;
        
        // 直接写入这条日志到Flash
        result = write_to_flash(entry_addr, (uint8_t*)g_logger_state.memory_buffer[i], LOG_ENTRY_SIZE);
        if (result != LOG_RESULT_SUCCESS) {
            return result;
        }
        
        // 更新写入位置
        header.next_write_index = (write_index + 1) % LOG_ENTRIES_PER_SECTOR;
        header.total_written++;
        
        // 更新时间戳
        uint32_t current_time = get_current_timestamp_ms();
        if (header.timestamp_first == 0) {
            header.timestamp_first = current_time;
        }
        header.timestamp_last = current_time;
        header.sequence_counter = ++g_logger_state.global_sequence;
    }

    // 写入更新后的扇区头部
    result = write_to_flash(sector_addr, (uint8_t*)&header, sizeof(LogSectorHeader));
    if (result != LOG_RESULT_SUCCESS) {
        return result;
    }

    // 清空缓冲区
    g_logger_state.buffer_count = 0;
    g_logger_state.last_flush_time = HAL_GetTick();

    return LOG_RESULT_SUCCESS;
}

/* ============================================================================
 * 核心API函数实现
 * ========================================================================== */

LogResult Logger_Init(bool is_bootloader, LogLevel min_level) {
    if (g_logger_state.is_initialized) {
        return LOG_RESULT_SUCCESS;
    }

    // 清零状态结构
    memset(&g_logger_state, 0, sizeof(LoggerState));

    // 设置基本参数
    g_logger_state.is_bootloader_mode = is_bootloader;
    g_logger_state.last_flush_time = HAL_GetTick();

    // 混合方案：先尝试快速启动
    LogResult result = quick_start_init(min_level);
    if (result == LOG_RESULT_SUCCESS) {
        // 快速启动成功
        g_logger_state.is_initialized = true;
        
        // 记录快速启动成功的日志
        Logger_Log(LOG_LEVEL_SYSTEM, "LOGGER", "Quick start successful - sector %lu, boot #%lu", 
                   (unsigned long)g_logger_state.current_sector,
                   (unsigned long)g_logger_state.boot_counter);
        
        return LOG_RESULT_SUCCESS;
    }
    
    // 快速启动失败，降级到完整扫描
    result = full_scan_init(min_level);
    if (result != LOG_RESULT_SUCCESS) {
        return result;
    }

    // 设置初始化完成标志
    g_logger_state.is_initialized = true;

    // 记录初始化日志（完整扫描）
    Logger_Log(LOG_LEVEL_SYSTEM, "LOGGER", "Full scan init - sector %lu, boot #%lu (quick start failed)", 
               (unsigned long)g_logger_state.current_sector,
               (unsigned long)g_logger_state.boot_counter);

    return LOG_RESULT_SUCCESS;
}

LogResult Logger_Deinit(void) {
    if (!g_logger_state.is_initialized) {
        return LOG_RESULT_ERROR_NOT_INITIALIZED;
    }
    
    // 刷新剩余数据到Flash
    LogResult result = flush_memory_buffer_to_flash();

    // 清理状态
    g_logger_state.is_initialized = false;

    return result;
}

LogResult Logger_Log(LogLevel level, const char* component, const char* format, ...) {
    va_list args;
    va_start(args, format);

    LogResult result;

    // 根据日志级别选择是否立即写入
    switch (level) {
        case LOG_LEVEL_DEBUG:
            result = logger_log_internal(level, component, false, format, args);
            break;
        case LOG_LEVEL_INFO:
            result = logger_log_internal(level, component, false, format, args);
            break;
        case LOG_LEVEL_WARN:
            result = logger_log_internal(level, component, false, format, args);
            break;
        case LOG_LEVEL_ERROR:
            result = logger_log_internal(level, component, true, format, args);
            break;
        case LOG_LEVEL_FATAL:
            result = logger_log_internal(level, component, true, format, args);
            break;
        case LOG_LEVEL_SYSTEM:
            result = logger_log_internal(level, component, false, format, args);
            break;
        default:
            result = logger_log_internal(level, component, true, format, args);
            break;
    }
    va_end(args);
    return result;
}

LogResult Logger_Flush(void) {
    if (!g_logger_state.is_initialized) {
        return LOG_RESULT_ERROR_NOT_INITIALIZED;
    }

    LogResult result = flush_memory_buffer_to_flash();

    return result;
}

LogResult Logger_AutoFlushCheck(void) {
    if (!g_logger_state.is_initialized) {
        return LOG_RESULT_SUCCESS;
    }

    uint32_t current_time = HAL_GetTick();
    if ((current_time - g_logger_state.last_flush_time) >= LOG_AUTO_FLUSH_INTERVAL_MS) {
        return Logger_Flush();
    }

    return LOG_RESULT_SUCCESS;
}

LogResult Logger_ClearFlash(void) {
    if (!g_logger_state.is_initialized) {
        return LOG_RESULT_ERROR_NOT_INITIALIZED;
    }

    LOGGER_LOCK();

    // 声明result变量
    LogResult result;

    // 擦除所有日志扇区
    for (uint32_t i = 0; i < LOG_FLASH_SECTOR_COUNT; i++) {
        result = erase_flash_sector(i);
        if (result != LOG_RESULT_SUCCESS) {
            LOGGER_UNLOCK();
            return result;
        }
    }

    // 重置状态
    g_logger_state.current_sector = 0;
    g_logger_state.global_sequence = 0;
    g_logger_state.buffer_count = 0;

    // 直接初始化扇区0
    LogSectorHeader header;
    memset(&header, 0, sizeof(LogSectorHeader));
    
    header.magic = LOG_MAGIC_NUMBER;
    header.next_write_index = 0;
    header.queue_start_index = 0;
    header.current_count = 0;
    header.total_written = 0;
    header.sector_index = 0;
    header.timestamp_first = 0;
    header.timestamp_last = 0;
    header.boot_counter = g_logger_state.boot_counter;
    header.sequence_counter = g_logger_state.global_sequence;
    header.is_active = 1;

    // 写入扇区0头部
    uint32_t sector_addr = LOG_FLASH_BASE_ADDR;
    result = write_to_flash(sector_addr, (uint8_t*)&header, sizeof(LogSectorHeader));

    LOGGER_UNLOCK();
    return result;
}

LogResult Logger_GetStatus(uint32_t* sector_index, uint32_t* write_index, 
                           uint32_t* queue_start, uint32_t* count) {
    if (!g_logger_state.is_initialized) {
        return LOG_RESULT_ERROR_NOT_INITIALIZED;
    }
    
    if (!sector_index || !write_index || !queue_start || !count) {
        return LOG_RESULT_ERROR_INVALID_PARAM;
    }
    
    // 读取当前扇区头部
    LogSectorHeader header;
    uint32_t sector_addr = LOG_FLASH_BASE_ADDR + g_logger_state.current_sector * LOG_FLASH_SECTOR_SIZE;
    LogResult result = read_from_flash(sector_addr, (uint8_t*)&header, sizeof(LogSectorHeader));
    
    if (result == LOG_RESULT_SUCCESS && header.magic == LOG_MAGIC_NUMBER) {
        *sector_index = g_logger_state.current_sector;
        *write_index = header.next_write_index;
        *queue_start = header.queue_start_index;
        *count = header.current_count;
    } else {
        *sector_index = g_logger_state.current_sector;
        *write_index = 0;
        *queue_start = 0;
        *count = 0;
    }
    
    return LOG_RESULT_SUCCESS;
}

LogResult Logger_PrintAllLogs(int (*print_func)(const char* format, ...)) {
    if (!print_func) {
        return LOG_RESULT_ERROR_INVALID_PARAM;
    }

    print_func("=== FLASH LOG DUMP ===\r\n");
    
    uint32_t total_entries = 0;
    
    // 遍历前几个扇区
    for (uint32_t sector = 0; sector < 8; sector++) {
        LogSectorHeader header;
        uint32_t sector_addr = LOG_FLASH_BASE_ADDR + sector * LOG_FLASH_SECTOR_SIZE;
        
        if (read_from_flash(sector_addr, (uint8_t*)&header, sizeof(LogSectorHeader)) != LOG_RESULT_SUCCESS) {
            continue;
        }
        
        if (header.magic != LOG_MAGIC_NUMBER || header.current_count == 0) {
            continue;
        }
        
        print_func("--- Sector %lu (count=%lu, start=%lu, next=%lu) ---\r\n", 
                   (unsigned long)sector, (unsigned long)header.current_count,
                   (unsigned long)header.queue_start_index, (unsigned long)header.next_write_index);
        
        // 读取日志条目
        uint32_t start_index = header.queue_start_index;
        for (uint32_t i = 0; i < header.current_count; i++) {
            uint32_t entry_index = (start_index + i) % LOG_ENTRIES_PER_SECTOR;
            LogEntry entry;
            
            uint32_t entry_addr = sector_addr + LOG_HEADER_SIZE + entry_index * LOG_ENTRY_SIZE;
            if (read_from_flash(entry_addr, (uint8_t*)entry, LOG_ENTRY_SIZE) == LOG_RESULT_SUCCESS) {
                // 确保字符串以null结尾
                entry[LOG_ENTRY_SIZE - 1] = '\0';
                
                // 移除末尾的换行符进行打印
                char* newline = strchr(entry, '\n');
                if (newline) *newline = '\0';
                
                print_func("%s\r\n", entry);
                total_entries++;
            }
        }
    }
    
    print_func("=== Total: %lu entries ===\r\n", (unsigned long)total_entries);
    return LOG_RESULT_SUCCESS;
}

LogResult Logger_ShowSectorInfo(int (*print_func)(const char* format, ...)) {
    if (!print_func) {
        return LOG_RESULT_ERROR_INVALID_PARAM;
    }

    print_func("=== SECTOR INFO ===\r\n");
    
    for (uint32_t sector = 0; sector < 8; sector++) {
        LogSectorHeader header;
        uint32_t sector_addr = LOG_FLASH_BASE_ADDR + sector * LOG_FLASH_SECTOR_SIZE;
        
        if (read_from_flash(sector_addr, (uint8_t*)&header, sizeof(LogSectorHeader)) != LOG_RESULT_SUCCESS) {
            print_func("Sector %lu: READ ERROR\r\n", (unsigned long)sector);
            continue;
        }
        
        if (header.magic == LOG_MAGIC_NUMBER) {
            print_func("Sector %lu: VALID - count=%lu, next=%lu, start=%lu, seq=%lu, boot=%lu\r\n",
                       (unsigned long)sector, (unsigned long)header.current_count,
                       (unsigned long)header.next_write_index, (unsigned long)header.queue_start_index,
                       (unsigned long)header.sequence_counter, (unsigned long)header.boot_counter);
        } else {
            print_func("Sector %lu: EMPTY (magic=0x%08lX)\r\n", 
                       (unsigned long)sector, (unsigned long)header.magic);
        }
    }
    
    return LOG_RESULT_SUCCESS;
}

LogResult Logger_ShowGlobalState(int (*print_func)(const char* format, ...)) {
    if (!print_func) {
        return LOG_RESULT_ERROR_INVALID_PARAM;
    }

    print_func("=== GLOBAL STATE INFO (Mixed Mode) ===\r\n");
    
    LogGlobalState global_state;
    LogResult result = read_global_state(&global_state);
    
    if (result != LOG_RESULT_SUCCESS) {
        print_func("Global State: READ ERROR\r\n");
        return result;
    }
    
    print_func("Magic1: 0x%08lX (expected: 0x%08lX)\r\n", 
               (unsigned long)global_state.magic1, (unsigned long)LOG_GLOBAL_STATE_MAGIC);
    print_func("Magic2: 0x%08lX (expected: 0x%08lX)\r\n", 
               (unsigned long)global_state.magic2, (unsigned long)LOG_GLOBAL_STATE_MAGIC);
    print_func("Last Active Sector: %lu\r\n", (unsigned long)global_state.last_active_sector);
    print_func("Last Active Sector Backup: %lu\r\n", (unsigned long)global_state.last_active_sector_backup);
    print_func("Global Sequence: %lu\r\n", (unsigned long)global_state.global_sequence);
    print_func("Boot Counter: %lu\r\n", (unsigned long)global_state.boot_counter);
    print_func("Last Update Timestamp: %lu ms\r\n", (unsigned long)global_state.last_update_timestamp);
    
    uint32_t calculated_checksum = calculate_checksum(&global_state);
    print_func("Checksum: 0x%08lX (calculated: 0x%08lX)\r\n", 
               (unsigned long)global_state.checksum, (unsigned long)calculated_checksum);
    
    bool is_valid = verify_global_state(&global_state);
    print_func("Global State: %s\r\n", is_valid ? "VALID" : "INVALID");
    
    if (is_valid) {
        print_func("Current Runtime State:\r\n");
        print_func("  Current Sector: %lu\r\n", (unsigned long)g_logger_state.current_sector);
        print_func("  Runtime Sequence: %lu\r\n", (unsigned long)g_logger_state.global_sequence);
        print_func("  Runtime Boot Counter: %lu\r\n", (unsigned long)g_logger_state.boot_counter);
    }
    
    return LOG_RESULT_SUCCESS;
}

LogResult Logger_GetSortedSectors(uint32_t* sector_array, uint32_t* actual_count) {
    if (!sector_array || !actual_count) {
        return LOG_RESULT_ERROR_INVALID_PARAM;
    }

    // 存储扇区信息的轻量级结构体
    typedef struct {
        uint32_t sector_index;
        uint32_t boot_counter;
        uint32_t sequence_counter;
        uint32_t timestamp_first;
    } SectorMeta;
    
    SectorMeta sectors[LOG_FLASH_SECTOR_COUNT - 1];  // 排除全局状态扇区
    uint32_t valid_count = 0;
    
    // 1. 扫描所有扇区头部，收集有效扇区信息
    for (uint32_t sector = 0; sector < LOG_FLASH_SECTOR_COUNT - 1; sector++) {
        LogSectorHeader header;
        uint32_t sector_addr = LOG_FLASH_BASE_ADDR + sector * LOG_FLASH_SECTOR_SIZE;
        
        // 读取扇区头部
        if (read_from_flash(sector_addr, (uint8_t*)&header, sizeof(LogSectorHeader)) == LOG_RESULT_SUCCESS) {
            // 检查扇区是否有效且有内容
            if (header.magic == LOG_MAGIC_NUMBER && header.current_count > 0) {
                sectors[valid_count].sector_index = sector;
                sectors[valid_count].boot_counter = header.boot_counter;
                sectors[valid_count].sequence_counter = header.sequence_counter;
                sectors[valid_count].timestamp_first = header.timestamp_first;
                valid_count++;
            }
        }
    }
    
    // 2. 对扇区按时间顺序排序（冒泡排序，适合小数据量）
    for (uint32_t i = 0; i < valid_count - 1; i++) {
        for (uint32_t j = 0; j < valid_count - 1 - i; j++) {
            bool should_swap = false;
            
            // 先按启动计数器排序
            if (sectors[j].boot_counter > sectors[j + 1].boot_counter) {
                should_swap = true;
            } else if (sectors[j].boot_counter == sectors[j + 1].boot_counter) {
                // 启动计数器相同，按序列计数器排序
                if (sectors[j].sequence_counter > sectors[j + 1].sequence_counter) {
                    should_swap = true;
                } else if (sectors[j].sequence_counter == sectors[j + 1].sequence_counter) {
                    // 序列计数器也相同，按时间戳排序
                    if (sectors[j].timestamp_first > sectors[j + 1].timestamp_first) {
                        should_swap = true;
                    }
                }
            }
            
            if (should_swap) {
                SectorMeta temp = sectors[j];
                sectors[j] = sectors[j + 1];
                sectors[j + 1] = temp;
            }
        }
    }
    
    // 3. 将排序后的扇区编号复制到输出数组
    for (uint32_t i = 0; i < valid_count; i++) {
        sector_array[i] = sectors[i].sector_index;
    }
    
    *actual_count = valid_count;
    
    return LOG_RESULT_SUCCESS;
}

LogResult Logger_GetSectorLogs(uint32_t sector_index, LogEntry* log_array, uint32_t* actual_count) {
    if (!log_array || !actual_count) {
        return LOG_RESULT_ERROR_INVALID_PARAM;
    }
    
    // 检查扇区编号有效性（排除全局状态扇区）
    if (sector_index >= LOG_FLASH_SECTOR_COUNT - 1) {
        return LOG_RESULT_ERROR_INVALID_PARAM;
    }
    
    *actual_count = 0;
    
    // 读取扇区头部
    LogSectorHeader header;
    uint32_t sector_addr = LOG_FLASH_BASE_ADDR + sector_index * LOG_FLASH_SECTOR_SIZE;
    
    LogResult result = read_from_flash(sector_addr, (uint8_t*)&header, sizeof(LogSectorHeader));
    if (result != LOG_RESULT_SUCCESS) {
        return result;
    }
    
    // 检查扇区是否有效
    if (header.magic != LOG_MAGIC_NUMBER) {
        return LOG_RESULT_SUCCESS;  // 扇区无效，返回0条日志
    }
    
    if (header.current_count == 0) {
        return LOG_RESULT_SUCCESS;  // 扇区为空，返回0条日志
    }
    
    // 按写入顺序读取日志条目
    uint32_t start_index = header.queue_start_index;
    uint32_t logs_read = 0;
    
    for (uint32_t i = 0; i < header.current_count; i++) {
        uint32_t entry_index = (start_index + i) % LOG_ENTRIES_PER_SECTOR;
        
        // 计算日志条目在Flash中的地址
        uint32_t entry_addr = sector_addr + LOG_HEADER_SIZE + entry_index * LOG_ENTRY_SIZE;
        
        // 读取日志条目
        result = read_from_flash(entry_addr, (uint8_t*)log_array[logs_read], LOG_ENTRY_SIZE);
        if (result != LOG_RESULT_SUCCESS) {
            break;  // 读取失败，返回已读取的日志数
        }
        
        // 确保字符串以null结尾
        log_array[logs_read][LOG_ENTRY_SIZE - 1] = '\0';
        
        // 移除末尾的换行符（如果存在）
        char* newline = strchr(log_array[logs_read], '\n');
        if (newline) {
            *newline = '\0';
        }
        
        logs_read++;
    }
    
    *actual_count = logs_read;
    return LOG_RESULT_SUCCESS;
}

static LogResult logger_log_internal(LogLevel level, const char* component, bool immediate_flush, const char* format, va_list args) {
    if (!g_logger_state.is_initialized) {
        return LOG_RESULT_ERROR_NOT_INITIALIZED;
    }

    if (!component || !format) {
        return LOG_RESULT_ERROR_INVALID_PARAM;
    }

    // 检查日志级别过滤
    if (level < g_logger_state.minimum_level) {
        return LOG_RESULT_SUCCESS; // 级别太低，不记录
    }

    // 格式化消息
    char message[256];
    vsnprintf(message, sizeof(message), format, args);

    // 创建日志条目
    LogEntry entry;
    LogResult result = format_log_entry(level, component, message, entry);
    if (result != LOG_RESULT_SUCCESS) {
        return result;
    }

    // 添加到内存缓冲区
    result = add_entry_to_memory_buffer(entry);

    if (immediate_flush) {
        return flush_memory_buffer_to_flash();
    }

    return LOG_RESULT_SUCCESS;
}

/* ============================================================================
 * 混合方案：快速启动功能实现
 * ========================================================================== */

static uint32_t calculate_checksum(const LogGlobalState* state) {
    uint32_t checksum = 0;
    const uint32_t* data = (const uint32_t*)state;
    
    // 计算除checksum字段外所有字段的异或值
    for (int i = 0; i < 7; i++) {  // 前7个uint32_t字段
        checksum ^= data[i];
    }
    
    return checksum;
}

static bool verify_global_state(const LogGlobalState* state) {
    // 检查魔术数字
    if (state->magic1 != LOG_GLOBAL_STATE_MAGIC || state->magic2 != LOG_GLOBAL_STATE_MAGIC) {
        return false;
    }
    
    // 检查备份字段一致性
    if (state->last_active_sector != state->last_active_sector_backup) {
        return false;
    }
    
    // 检查扇区索引有效性
    if (state->last_active_sector >= LOG_FLASH_SECTOR_COUNT - 1) {  // 减1因为最后一个扇区用于全局状态
        return false;
    }
    
    // 检查校验和
    uint32_t calculated_checksum = calculate_checksum(state);
    if (calculated_checksum != state->checksum) {
        return false;
    }
    
    return true;
}

static LogResult read_global_state(LogGlobalState* state) {
    if (!state) {
        return LOG_RESULT_ERROR_INVALID_PARAM;
    }
    
    uint32_t global_state_addr = LOG_FLASH_BASE_ADDR + LOG_GLOBAL_STATE_SECTOR * LOG_FLASH_SECTOR_SIZE;
    return read_from_flash(global_state_addr, (uint8_t*)state, sizeof(LogGlobalState));
}

static LogResult write_global_state(const LogGlobalState* state) {
    if (!state) {
        return LOG_RESULT_ERROR_INVALID_PARAM;
    }
    
    uint32_t global_state_addr = LOG_FLASH_BASE_ADDR + LOG_GLOBAL_STATE_SECTOR * LOG_FLASH_SECTOR_SIZE;
    
    // 先擦除全局状态扇区
    LogResult result = erase_flash_sector(LOG_GLOBAL_STATE_SECTOR);
    if (result != LOG_RESULT_SUCCESS) {
        return result;
    }
    
    // 写入全局状态
    return write_to_flash(global_state_addr, (uint8_t*)state, sizeof(LogGlobalState));
}

static LogResult quick_start_init(LogLevel min_level) {
    // 读取全局状态
    LogGlobalState global_state;
    LogResult result = read_global_state(&global_state);
    if (result != LOG_RESULT_SUCCESS) {
        return result;  // 读取失败，需要降级到完整扫描
    }
    
    // 验证全局状态
    if (!verify_global_state(&global_state)) {
        return LOG_RESULT_ERROR_INIT;  // 验证失败，需要降级到完整扫描
    }
    
    // 验证指定的活跃扇区是否真的有效
    LogSectorHeader header;
    uint32_t sector_addr = LOG_FLASH_BASE_ADDR + global_state.last_active_sector * LOG_FLASH_SECTOR_SIZE;
    result = read_from_flash(sector_addr, (uint8_t*)&header, sizeof(LogSectorHeader));
    if (result != LOG_RESULT_SUCCESS) {
        return result;  // 读取失败，需要降级到完整扫描
    }
    
    // 检查扇区是否真的是活跃的
    if (header.magic != LOG_MAGIC_NUMBER || !header.is_active) {
        return LOG_RESULT_ERROR_INIT;  // 扇区无效，需要降级到完整扫描
    }
    
    // 检查序列号是否匹配
    if (header.sequence_counter != global_state.global_sequence) {
        return LOG_RESULT_ERROR_INIT;  // 序列号不匹配，可能数据不一致
    }
    
    // 快速启动成功，设置状态
    g_logger_state.current_sector = global_state.last_active_sector;
    g_logger_state.global_sequence = global_state.global_sequence;
    g_logger_state.boot_counter = global_state.boot_counter + 1;
    g_logger_state.minimum_level = min_level;
    
    return LOG_RESULT_SUCCESS;
}

static LogResult full_scan_init(LogLevel min_level) {
    // 扫描Flash找到最新的活跃扇区
    uint32_t max_sequence = 0;
    uint32_t max_boot_count = 0;
    uint32_t active_sector = 0;
    bool found_active = false;

    for (uint32_t sector = 0; sector < LOG_FLASH_SECTOR_COUNT - 1; sector++) {  // 减1因为最后一个扇区用于全局状态
        LogSectorHeader header;
        uint32_t sector_addr = LOG_FLASH_BASE_ADDR + sector * LOG_FLASH_SECTOR_SIZE;
        
        if (read_from_flash(sector_addr, (uint8_t*)&header, sizeof(LogSectorHeader)) == LOG_RESULT_SUCCESS) {
            if (header.magic == LOG_MAGIC_NUMBER && header.is_active) {
                if (header.sequence_counter > max_sequence || 
                    (header.sequence_counter == max_sequence && header.boot_counter > max_boot_count)) {
                    max_sequence = header.sequence_counter;
                    max_boot_count = header.boot_counter;
                    active_sector = sector;
                    found_active = true;
                }
            }
        }
    }

    if (found_active) {
        g_logger_state.current_sector = active_sector;
        g_logger_state.global_sequence = max_sequence;
        g_logger_state.boot_counter = max_boot_count + 1;
        g_logger_state.minimum_level = min_level;
        
    } else {
        // 没有找到活跃扇区，初始化第0个扇区
        g_logger_state.current_sector = 0;
        g_logger_state.global_sequence = 0;
        g_logger_state.boot_counter = 1;
        g_logger_state.minimum_level = min_level;
        
        // 擦除并初始化第0个扇区
        LogResult result = erase_flash_sector(0);
        if (result != LOG_RESULT_SUCCESS) {
            return result;
        }
        
        // 初始化扇区头部
        LogSectorHeader header;
        memset(&header, 0, sizeof(LogSectorHeader));
        
        header.magic = LOG_MAGIC_NUMBER;
        header.next_write_index = 0;
        header.queue_start_index = 0;
        header.current_count = 0;
        header.total_written = 0;
        header.sector_index = 0;
        header.timestamp_first = 0;
        header.timestamp_last = 0;
        header.boot_counter = g_logger_state.boot_counter;
        header.sequence_counter = g_logger_state.global_sequence;
        header.is_active = 1;

        // 写入扇区头部（只写入64字节头部）
        uint32_t sector_addr = LOG_FLASH_BASE_ADDR;
        result = write_to_flash(sector_addr, (uint8_t*)&header, sizeof(LogSectorHeader));
        if (result != LOG_RESULT_SUCCESS) {
            return result;
        }
    }
    
    return LOG_RESULT_SUCCESS;
} 