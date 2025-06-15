#ifndef DUAL_SLOT_CONFIG_H
#define DUAL_SLOT_CONFIG_H

#include <stdint.h>
#include <stdbool.h>

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

// 元数据区域
#define FIRMWARE_METADATA_BASE          0x90570000
#define FIRMWARE_METADATA_SIZE          0x10000     // 64KB

/* ================================ 元数据结构定义 ================================ */

// 元数据结构常量（与release.py保持一致）
#define FIRMWARE_MAGIC                  0x48424F58  // "HBOX"
#define METADATA_VERSION_MAJOR          1
#define METADATA_VERSION_MINOR          0
#define DEVICE_MODEL_STRING             "STM32H750_HBOX"
#define BOOTLOADER_VERSION              0x00010000  // 1.0.0
#define HARDWARE_VERSION                0x00010000  // 1.0.0

// 组件相关常量
#define FIRMWARE_COMPONENT_COUNT        3
#define COMPONENT_SIZE                  170         // FirmwareComponent packed结构体大小: 32+64+4+4+65+1 = 170字节
#define METADATA_STRUCT_SIZE            807         // 完整元数据结构大小: 20+32+1+32+4+32+4+4+4+(170*3)+32+64+4+64 = 807字节

// 槽位定义
typedef enum {
    SLOT_A = 0,
    SLOT_B = 1,
    SLOT_INVALID = 0xFF
} FirmwareSlot;

// 固件组件信息（与release.py完全对齐）
typedef struct {
    char name[32];              // 组件名称
    char file[64];              // 文件名
    uint32_t address;           // 目标地址
    uint32_t size;              // 组件大小
    char sha256[65];            // SHA256校验和
    bool active;                // 是否激活
    // 编译器会在这里添加1字节padding对齐到172字节
} __attribute__((packed)) FirmwareComponent;

// 安全固件元数据结构（与release.py完全对齐）
typedef struct {
    // === 安全校验区域 ===
    uint32_t magic;                     // 魔术数字 0x48424F58 ("HBOX")
    uint32_t metadata_version_major;    // 元数据结构版本号
    uint32_t metadata_version_minor;
    uint32_t metadata_size;             // 元数据总大小
    uint32_t metadata_crc32;            // 元数据CRC32校验（不包括此字段本身）
    
    // === 固件信息区域 ===
    char firmware_version[32];          // 固件版本号
    uint8_t target_slot;                // 目标槽位 (0=A, 1=B) - 使用uint8_t确保1字节对齐
    char build_date[32];                // 构建日期
    uint32_t build_timestamp;           // 构建时间戳
    
    // === 设备兼容性区域 ===
    char device_model[32];              // 设备型号 "STM32H750_HBOX"
    uint32_t hardware_version;          // 硬件版本
    uint32_t bootloader_min_version;    // 最低bootloader版本要求
    
    // === 组件信息区域 ===
    uint32_t component_count;           // 组件数量
    FirmwareComponent components[FIRMWARE_COMPONENT_COUNT];
    
    // === 安全签名区域 ===
    uint8_t firmware_hash[32];          // 整个固件包的SHA256哈希
    uint8_t signature[64];              // 数字签名（预留，可选）
    uint32_t signature_algorithm;       // 签名算法标识
    
    // === 预留区域 ===
    uint8_t reserved[64];               // 预留空间，用于未来扩展
} __attribute__((packed)) FirmwareMetadata;

// 固件验证结果
typedef enum {
    FIRMWARE_VALID = 0,
    FIRMWARE_INVALID_MAGIC,
    FIRMWARE_INVALID_CRC,
    FIRMWARE_INVALID_VERSION,
    FIRMWARE_INVALID_DEVICE,
    FIRMWARE_INVALID_HASH,
    FIRMWARE_INVALID_SIGNATURE,
    FIRMWARE_CORRUPTED
} FirmwareValidationResult;

// Bootloader内部使用的简化元数据结构
typedef struct {
    uint32_t magic;                     // 魔术数字
    uint32_t version;                   // 版本号
    FirmwareSlot active_slot;           // 当前激活槽位
    FirmwareSlot backup_slot;           // 备用槽位
    uint32_t boot_count;                // 启动计数
    uint32_t crc32;                     // CRC校验
} bootloader_metadata_t;

/* ================================ 函数声明 ================================ */

// 元数据管理函数
int8_t DualSlot_LoadMetadata(FirmwareMetadata* metadata);
int8_t DualSlot_SaveMetadata(const FirmwareMetadata* metadata);
FirmwareValidationResult DualSlot_ValidateMetadata(const FirmwareMetadata* metadata);

// 槽位管理函数
FirmwareSlot DualSlot_GetActiveSlot(void);
int8_t DualSlot_SetActiveSlot(FirmwareSlot slot);
uint32_t DualSlot_GetSlotAddress(const char* component_name, FirmwareSlot slot);

// 启动跳转函数
void DualSlot_JumpToApplication(FirmwareSlot slot);
bool DualSlot_IsSlotValid(FirmwareSlot slot);

// 调试函数
void DualSlot_PrintMetadata(const FirmwareMetadata* metadata);

#endif // DUAL_SLOT_CONFIG_H 