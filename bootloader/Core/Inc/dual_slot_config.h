#ifndef DUAL_SLOT_CONFIG_H
#define DUAL_SLOT_CONFIG_H

#include <stdint.h>
#include <stdbool.h>

// 包含统一的固件元数据定义
#include "../../../common/firmware_metadata.h"

/* ================================ 地址配置 ================================ */

// 外部Flash基地址
#define EXTERNAL_FLASH_BASE             0x90000000

// 槽位配置
#define SLOT_A_BASE                     0x90000000
#define SLOT_B_BASE                     0x902B0000

// 各组件在槽A的地址
#define SLOT_A_APPLICATION_ADDR         0x90000000
#define SLOT_A_WEBRESOURCES_ADDR        0x90100000
#define SLOT_A_ADC_MAPPING_ADDR         0x90280000

// 各组件在槽B的地址
#define SLOT_B_APPLICATION_ADDR         0x902B0000
#define SLOT_B_WEBRESOURCES_ADDR        0x903B0000
#define SLOT_B_ADC_MAPPING_ADDR         0x90530000

// 各组件大小
#define SLOT_A_APPLICATION_SIZE         0x100000    // 1MB
#define SLOT_A_WEBRESOURCES_SIZE        0x180000    // 1.5MB
#define SLOT_A_ADC_MAPPING_SIZE         0x20000     // 128KB

#define SLOT_B_APPLICATION_SIZE         0x100000    // 1MB
#define SLOT_B_WEBRESOURCES_SIZE        0x180000    // 1.5MB
#define SLOT_B_ADC_MAPPING_SIZE         0x20000     // 128KB

// 配置和元数据区域
#define USER_CONFIG_ADDR                0x90560000  // 64KB
#define USER_CONFIG_SIZE                0x10000
#define METADATA_ADDR                   0x90570000  // 64KB
#define METADATA_SIZE                   0x10000

// 槽位大小
#define SLOT_SIZE                       0x2B0000    // 2.625MB per slot

/* ================================ Bootloader特有定义 ================================ */

// Bootloader元数据结构
typedef struct {
    uint32_t magic;                     // 魔术数字
    uint32_t version;                   // 版本号
    FirmwareSlot active_slot;           // 当前激活槽位
    FirmwareSlot backup_slot;           // 备用槽位
    uint32_t boot_count;                // 启动计数
    uint32_t crc32;                     // CRC校验
} bootloader_metadata_t;

/* ================================ 函数声明 ================================ */

// 元数据管理
int8_t DualSlot_LoadMetadata(FirmwareMetadata* metadata);
int8_t DualSlot_SaveMetadata(const FirmwareMetadata* metadata);
FirmwareValidationResult DualSlot_ValidateMetadata(const FirmwareMetadata* metadata);

// 槽位管理
FirmwareSlot DualSlot_GetActiveSlot(void);
int8_t DualSlot_SetActiveSlot(FirmwareSlot slot);
uint32_t DualSlot_GetSlotAddress(const char* component_name, FirmwareSlot slot);

// 启动管理
void DualSlot_JumpToApplication(FirmwareSlot slot);
bool DualSlot_IsSlotValid(FirmwareSlot slot);

// 调试功能
void DualSlot_PrintMetadata(const FirmwareMetadata* metadata);

#endif // DUAL_SLOT_CONFIG_H 