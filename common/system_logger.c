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
    
    int8_t result = QSPI_W25Qxx_WriteBuffer_WithXIPOrNot((uint8_t*)data, physical_addr, size);
    
    if (result != QSPI_W25Qxx_OK) {
        return LOG_RESULT_ERROR_FLASH_WRITE;
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

    // 获取时间戳
    uint32_t timestamp = get_current_timestamp_ms();
    uint32_t sec = timestamp / 1000;
    uint32_t ms = timestamp % 1000;

    // 模拟日期时间 (简化实现)
    uint32_t days = sec / 86400;  // 秒转天数
    uint32_t hours = (sec % 86400) / 3600;
    uint32_t minutes = (sec % 3600) / 60;
    uint32_t seconds = sec % 60;
    
    // 从2024年开始计算 (简化的日期计算)
    uint32_t year = 2024 + days / 365;
    uint32_t month = (days % 365) / 30 + 1;  // 简化月份计算
    uint32_t day = (days % 365) % 30 + 1;

    // 格式化日志条目 (最多63个字符 + \n)
    snprintf(entry, LOG_ENTRY_SIZE, 
             "%04lu-%02lu-%02lu %02lu:%02lu:%02lu.%03lu [%s] %s: %s\n",
             (unsigned long)year, (unsigned long)month, (unsigned long)day,
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
    // 切换到下一个扇区
    uint32_t next_sector = (g_logger_state.current_sector + 1) % LOG_FLASH_SECTOR_COUNT;
    
    if (g_logger_state.is_bootloader_mode) {
        printf("[LOGGER] Switching to sector %lu\r\n", (unsigned long)next_sector);
    }
    
    // 擦除下一个扇区
    LogResult result = erase_flash_sector(next_sector);
    if (result != LOG_RESULT_SUCCESS) {
        return result;
    }

    // 初始化新扇区头部
    LogSector sector;
    memset(&sector, 0, sizeof(LogSector));
    
    sector.header.magic = LOG_MAGIC_NUMBER;
    sector.header.next_write_index = 0;
    sector.header.queue_start_index = 0;
    sector.header.current_count = 0;
    sector.header.total_written = 0;
    sector.header.sector_index = next_sector;
    sector.header.timestamp_first = 0;
    sector.header.timestamp_last = 0;
    sector.header.boot_counter = g_logger_state.boot_counter;
    sector.header.sequence_counter = g_logger_state.global_sequence;
    sector.header.is_active = 1;

    // 写入扇区头部
    uint32_t sector_addr = LOG_FLASH_BASE_ADDR + next_sector * LOG_FLASH_SECTOR_SIZE;
    result = write_to_flash(sector_addr, (uint8_t*)&sector.header, LOG_HEADER_SIZE);
    if (result != LOG_RESULT_SUCCESS) {
        return result;
    }

    g_logger_state.current_sector = next_sector;
    return LOG_RESULT_SUCCESS;
}

static LogResult flush_memory_buffer_to_flash(void) {
    if (g_logger_state.buffer_count == 0) {
        return LOG_RESULT_SUCCESS; // 没有数据需要刷新
    }

    if (g_logger_state.is_bootloader_mode) {
        printf("[LOGGER] Flushing %lu entries to sector %lu\r\n",
               (unsigned long)g_logger_state.buffer_count, (unsigned long)g_logger_state.current_sector);
    }

    // 读取当前扇区的完整数据
    LogSector sector;
    uint32_t sector_addr = LOG_FLASH_BASE_ADDR + g_logger_state.current_sector * LOG_FLASH_SECTOR_SIZE;
    
    LogResult result = read_from_flash(sector_addr, (uint8_t*)&sector, sizeof(LogSector));
    if (result != LOG_RESULT_SUCCESS) {
        return result;
    }

    // 检查扇区头部有效性
    if (sector.header.magic != LOG_MAGIC_NUMBER) {
        // 扇区无效，重新初始化
        if (g_logger_state.is_bootloader_mode) {
            printf("[LOGGER] Invalid sector header, reinitializing...\r\n");
        }
        
        memset(&sector, 0, sizeof(LogSector));
        sector.header.magic = LOG_MAGIC_NUMBER;
        sector.header.next_write_index = 0;
        sector.header.queue_start_index = 0;
        sector.header.current_count = 0;
        sector.header.sector_index = g_logger_state.current_sector;
        sector.header.boot_counter = g_logger_state.boot_counter;
        sector.header.sequence_counter = g_logger_state.global_sequence;
        sector.header.is_active = 1;
    }

    // 添加新日志条目到扇区数组
    for (uint32_t i = 0; i < g_logger_state.buffer_count; i++) {
        uint32_t write_index = sector.header.next_write_index;
        
        // 检查是否需要循环覆盖
        if (sector.header.current_count >= LOG_ENTRIES_PER_SECTOR) {
            // 扇区满了，循环覆盖最旧的日志
            write_index = sector.header.queue_start_index;
            sector.header.queue_start_index = (sector.header.queue_start_index + 1) % LOG_ENTRIES_PER_SECTOR;
        } else {
            // 扇区未满，简单增加计数
            sector.header.current_count++;
        }
        
        // 复制日志条目
        memcpy(sector.entries[write_index], g_logger_state.memory_buffer[i], LOG_ENTRY_SIZE);
        
        // 更新写入位置
        sector.header.next_write_index = (write_index + 1) % LOG_ENTRIES_PER_SECTOR;
        sector.header.total_written++;
        
        // 更新时间戳
        uint32_t current_time = get_current_timestamp_ms();
        if (sector.header.timestamp_first == 0) {
            sector.header.timestamp_first = current_time;
        }
        sector.header.timestamp_last = current_time;
        sector.header.sequence_counter = ++g_logger_state.global_sequence;
    }

    // 检查是否需要切换扇区 (当前扇区循环使用完毕)
    if (sector.header.current_count >= LOG_ENTRIES_PER_SECTOR && 
        sector.header.total_written >= LOG_ENTRIES_PER_SECTOR * 2) {
        // 切换到下一个扇区
        result = switch_to_next_sector();
        if (result != LOG_RESULT_SUCCESS) {
            return result;
        }
        
        // 重新开始写入新扇区
        return flush_memory_buffer_to_flash();
    }

    // 写回整个扇区数据
    result = write_to_flash(sector_addr, (uint8_t*)&sector, sizeof(LogSector));
    if (result != LOG_RESULT_SUCCESS) {
        return result;
    }

    // 清空缓冲区
    g_logger_state.buffer_count = 0;
    g_logger_state.last_flush_time = HAL_GetTick();

    if (g_logger_state.is_bootloader_mode) {
        printf("[LOGGER] Flush complete: sector=%lu, count=%lu, next_index=%lu\r\n",
               (unsigned long)g_logger_state.current_sector, 
               (unsigned long)sector.header.current_count,
               (unsigned long)sector.header.next_write_index);
    }

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
    g_logger_state.minimum_level = min_level;
    g_logger_state.last_flush_time = HAL_GetTick();
    g_logger_state.current_sector = 0;
    g_logger_state.global_sequence = 0;
    g_logger_state.boot_counter = 1;

    if (is_bootloader) {
        printf("[LOGGER] Initializing logger system...\r\n");
    }

    // 扫描Flash找到最新的活跃扇区
    uint32_t max_sequence = 0;
    uint32_t max_boot_count = 0;
    uint32_t active_sector = 0;
    bool found_active = false;

    for (uint32_t sector = 0; sector < 8; sector++) {  // 只扫描前8个扇区
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
                
                if (is_bootloader) {
                    printf("[LOGGER] Found sector %lu: seq=%lu, boot=%lu, count=%lu\r\n",
                           (unsigned long)sector, (unsigned long)header.sequence_counter,
                           (unsigned long)header.boot_counter, (unsigned long)header.current_count);
                }
            }
        }
    }

    if (found_active) {
        g_logger_state.current_sector = active_sector;
        g_logger_state.global_sequence = max_sequence;
        g_logger_state.boot_counter = max_boot_count + 1;
        
        if (is_bootloader) {
            printf("[LOGGER] Continuing from sector %lu (boot #%lu, seq #%lu)\r\n",
                   (unsigned long)active_sector, (unsigned long)g_logger_state.boot_counter,
                   (unsigned long)g_logger_state.global_sequence);
        }
    } else {
        // 没有找到活跃扇区，初始化第一个扇区
        LogResult result = switch_to_next_sector();
        if (result != LOG_RESULT_SUCCESS) {
            return result;
        }
        
        if (is_bootloader) {
            printf("[LOGGER] No active sector found, initialized sector 0\r\n");
        }
    }

    // 设置初始化完成标志
    g_logger_state.is_initialized = true;

    // 记录初始化日志
    Logger_Log(LOG_LEVEL_SYSTEM, "LOGGER", "Logger initialized in %s mode (boot #%lu)", 
               is_bootloader ? "bootloader" : "application", 
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

    // 简单锁定
    LOGGER_LOCK();

    // 格式化消息
    char message[256];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    // 创建日志条目
    LogEntry entry;
    LogResult result = format_log_entry(level, component, message, entry);
    if (result != LOG_RESULT_SUCCESS) {
        LOGGER_UNLOCK();
        return result;
    }

    // 添加到内存缓冲区
    result = add_entry_to_memory_buffer(entry);

    LOGGER_UNLOCK();
    return result;
}

LogResult Logger_Flush(void) {
    if (!g_logger_state.is_initialized) {
        return LOG_RESULT_ERROR_NOT_INITIALIZED;
    }

    LOGGER_LOCK();
    LogResult result = flush_memory_buffer_to_flash();
    LOGGER_UNLOCK();

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

    // 擦除所有日志扇区
    for (uint32_t i = 0; i < LOG_FLASH_SECTOR_COUNT; i++) {
        LogResult result = erase_flash_sector(i);
        if (result != LOG_RESULT_SUCCESS) {
            LOGGER_UNLOCK();
            return result;
        }
    }

    // 重置状态
    g_logger_state.current_sector = 0;
    g_logger_state.global_sequence = 0;
    g_logger_state.buffer_count = 0;

    // 重新初始化第一个扇区
    LogResult result = switch_to_next_sector();

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