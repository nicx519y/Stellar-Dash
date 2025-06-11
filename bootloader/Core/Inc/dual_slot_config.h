#ifndef __DUAL_SLOT_CONFIG_H
#define __DUAL_SLOT_CONFIG_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================ 双槽配置定义 ================================ */

// 双槽功能开关
#define DUAL_SLOT_ENABLE                1       // 设置为1启用双槽功能，0则使用原有单槽模式

// 魔术数字用于验证元数据有效性
#define FIRMWARE_METADATA_MAGIC         0x12345678

// 槽定义
#define SLOT_A                          0
#define SLOT_B                          1
#define SLOT_INVALID                    0xFF

/* ================================ 外部Flash地址映射 ================================ */

// 外部Flash基地址 (通过QSPI内存映射)
#define EXTERNAL_FLASH_BASE             0x90000000

// W25Q64容量限制
#define W25Q64_TOTAL_SIZE               (8 * 1024 * 1024)    // 8MB

// 槽A地址定义 - 从0x00000000开始，共2.625MB
#define SLOT_A_BASE                     (EXTERNAL_FLASH_BASE + 0x00000000)    // 0x90000000
#define SLOT_A_APPLICATION_BASE         (SLOT_A_BASE + 0x00000000)            // 0x90000000
#define SLOT_A_APPLICATION_SIZE         (1 * 1024 * 1024)                     // 1MB
#define SLOT_A_WEBRESOURCES_BASE        (SLOT_A_BASE + 0x00100000)            // 0x90100000
#define SLOT_A_WEBRESOURCES_SIZE        (1536 * 1024)                         // 1.5MB
#define SLOT_A_ADC_MAPPING_BASE         (SLOT_A_BASE + 0x00280000)            // 0x90280000
#define SLOT_A_ADC_MAPPING_SIZE         (128 * 1024)                          // 128KB
#define SLOT_A_TOTAL_SIZE               (SLOT_A_APPLICATION_SIZE + SLOT_A_WEBRESOURCES_SIZE + SLOT_A_ADC_MAPPING_SIZE)

// 槽B地址定义 - 从0x002B0000开始，共2.625MB
#define SLOT_B_BASE                     (EXTERNAL_FLASH_BASE + 0x002B0000)    // 0x902B0000
#define SLOT_B_APPLICATION_BASE         (SLOT_B_BASE + 0x00000000)            // 0x902B0000
#define SLOT_B_APPLICATION_SIZE         (1 * 1024 * 1024)                     // 1MB
#define SLOT_B_WEBRESOURCES_BASE        (SLOT_B_BASE + 0x00100000)            // 0x903B0000
#define SLOT_B_WEBRESOURCES_SIZE        (1536 * 1024)                         // 1.5MB
#define SLOT_B_ADC_MAPPING_BASE         (SLOT_B_BASE + 0x00280000)            // 0x90530000
#define SLOT_B_ADC_MAPPING_SIZE         (128 * 1024)                          // 128KB
#define SLOT_B_TOTAL_SIZE               (SLOT_B_APPLICATION_SIZE + SLOT_B_WEBRESOURCES_SIZE + SLOT_B_ADC_MAPPING_SIZE)

// 用户配置区 - 从0x00560000开始
#define USER_CONFIG_BASE                (EXTERNAL_FLASH_BASE + 0x00560000)    // 0x90560000
#define USER_CONFIG_SIZE                (64 * 1024)                           // 64KB

// 元数据区 - 从0x00570000开始
#define FIRMWARE_METADATA_BASE          (EXTERNAL_FLASH_BASE + 0x00570000)    // 0x90570000
#define FIRMWARE_METADATA_SIZE          (64 * 1024)                           // 64KB

/* ================================ 数据结构定义 ================================ */

// 固件元数据结构
typedef struct {
    uint32_t magic_number;          // 魔术数字，用于验证数据有效性
    uint32_t current_version;       // 当前固件版本号
    
    // 槽A信息
    uint32_t slot_a_version;        // 槽A的版本号
    uint32_t slot_a_address;        // 槽A的起始地址
    uint32_t slot_a_size;           // 槽A的大小
    uint8_t  slot_a_valid;          // 槽A是否有效 (0=无效, 1=有效)
    uint8_t  slot_a_crc_valid;      // 槽A CRC校验是否通过
    uint16_t reserved_a;            // 保留字段，对齐
    uint32_t slot_a_crc32;          // 槽A固件CRC32校验值
    
    // 槽B信息  
    uint32_t slot_b_version;        // 槽B的版本号
    uint32_t slot_b_address;        // 槽B的起始地址
    uint32_t slot_b_size;           // 槽B的大小
    uint8_t  slot_b_valid;          // 槽B是否有效 (0=无效, 1=有效)
    uint8_t  slot_b_crc_valid;      // 槽B CRC校验是否通过
    uint16_t reserved_b;            // 保留字段，对齐
    uint32_t slot_b_crc32;          // 槽B固件CRC32校验值
    
    uint8_t  current_slot;          // 当前使用的槽 (SLOT_A=0, SLOT_B=1)
    uint8_t  upgrade_slot;          // 待升级的槽 (SLOT_A=0, SLOT_B=1)
    uint8_t  boot_count;            // 启动次数计数
    uint8_t  rollback_enabled;      // 是否启用自动回滚 (0=禁用, 1=启用)
    
    uint32_t boot_timestamp;        // 最后启动时间戳
    uint32_t upgrade_timestamp;     // 最后升级时间戳
    
    uint8_t  reserved[40];          // 保留字段，用于未来扩展
    uint32_t metadata_crc32;        // 元数据CRC校验值（不包含此字段）
} firmware_metadata_t;

// 槽信息结构
typedef struct {
    uint32_t base_address;          // 槽基地址
    uint32_t application_address;   // Application地址
    uint32_t application_size;      // Application大小
    uint32_t webresources_address;  // WebResources地址
    uint32_t webresources_size;     // WebResources大小
    uint32_t adc_mapping_address;   // ADC Mapping地址
    uint32_t adc_mapping_size;      // ADC Mapping大小
    uint32_t total_size;            // 槽总大小
} slot_info_t;

/* ================================ 函数声明 ================================ */

// 双槽管理函数
int8_t DualSlot_Init(void);
int8_t DualSlot_LoadMetadata(firmware_metadata_t* metadata);
int8_t DualSlot_SaveMetadata(const firmware_metadata_t* metadata);
int8_t DualSlot_GetSlotInfo(uint8_t slot, slot_info_t* slot_info);
uint32_t DualSlot_GetSlotApplicationAddress(uint8_t slot);
uint8_t DualSlot_GetCurrentSlot(void);
uint8_t DualSlot_GetUpgradeSlot(void);
int8_t DualSlot_SetCurrentSlot(uint8_t slot);
int8_t DualSlot_ValidateSlot(uint8_t slot);
int8_t DualSlot_SwitchSlot(void);

// 工具函数
uint32_t DualSlot_CalculateCRC32(const uint8_t* data, uint32_t length);
bool DualSlot_IsEnabled(void);

// 兼容性函数 - 保持原有接口不变
uint32_t DualSlot_GetLegacyApplicationAddress(void);

/* ================================ 全局变量声明 ================================ */

extern firmware_metadata_t g_firmware_metadata;

#ifdef __cplusplus
}
#endif

#endif /* __DUAL_SLOT_CONFIG_H */ 