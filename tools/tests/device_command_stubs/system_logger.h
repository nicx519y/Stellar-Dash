#pragma once

#include <stdint.h>

#define LOG_DEBUG(...) ((void)0)
#define LOG_INFO(...) ((void)0)
#define LOG_WARN(...) ((void)0)
#define LOG_ERROR(...) ((void)0)
#define LOG_FATAL(...) ((void)0)

#define LOG_FLASH_SECTOR_COUNT 16u
#define LOG_ENTRIES_PER_SECTOR 31u
#define LOG_ENTRY_SIZE 128u
typedef char LogEntry[LOG_ENTRY_SIZE];
typedef enum { LOG_RESULT_SUCCESS = 0, LOG_RESULT_ERROR = 1 } LogResult;

static inline LogResult Logger_Flush(void) { return LOG_RESULT_SUCCESS; }
static inline LogResult Logger_GetSortedSectors(uint32_t *, uint32_t *count)
{
    if (count != nullptr) *count = 0u;
    return LOG_RESULT_SUCCESS;
}
static inline LogResult Logger_GetSectorLogs(uint32_t, LogEntry *, uint32_t *count)
{
    if (count != nullptr) *count = 0u;
    return LOG_RESULT_SUCCESS;
}
