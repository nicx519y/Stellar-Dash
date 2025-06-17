#ifndef FIRMWARE_METADATA_H
#define FIRMWARE_METADATA_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================ 元数据结构常量 ================================ */

// 元数据结构常量（与release.py保持一致）
#define FIRMWARE_MAGIC                  0x48424F58  // "HBOX"
#define METADATA_VERSION_MAJOR          1
#define METADATA_VERSION_MINOR          0
#define DEVICE_MODEL_STRING             "STM32H750_HBOX"
#define BOOTLOADER_VERSION              0x00010000  // 1.0.0
#define HARDWARE_VERSION                0x00010000  // 1.0.0

// 组件相关常量
#define FIRMWARE_COMPONENT_COUNT        3

// 结构体大小计算（必须与实际结构体大小完全一致）
// FirmwareComponent: 32+64+4+4+65+1 = 170字节 (packed)
// FirmwareMetadata: 20+32+1+32+4+32+4+4+4+(170*3)+32+64+4+64 = 807字节 (packed)
#define COMPONENT_STRUCT_SIZE           170
#define METADATA_STRUCT_SIZE            807

/* ================================ 枚举定义 ================================ */

// 固件组件类型
typedef enum {
    FIRMWARE_COMPONENT_APPLICATION = 0,
    FIRMWARE_COMPONENT_WEBRESOURCES,
    FIRMWARE_COMPONENT_ADC_MAPPING,
    FIRMWARE_COMPONENT_COUNT_ENUM = FIRMWARE_COMPONENT_COUNT
} FirmwareComponentType;

// 固件槽位
typedef enum {
    FIRMWARE_SLOT_A = 0,
    FIRMWARE_SLOT_B = 1,
    FIRMWARE_SLOT_COUNT = 2,
    FIRMWARE_SLOT_INVALID = 0xFF
} FirmwareSlot;

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

/* ================================ 结构体定义 ================================ */

// 固件组件信息（与release.py完全对齐）
// 总大小: 170字节 (32+64+4+4+65+1)
typedef struct {
    char name[32];              // 组件名称
    char file[64];              // 文件名
    uint32_t address;           // 目标地址
    uint32_t size;              // 组件大小
    char sha256[65];            // SHA256校验和
    bool active;                // 是否激活 (1字节)
} __attribute__((packed)) FirmwareComponent;

// 安全固件元数据结构（与release.py完全对齐）
// 总大小: 807字节 (20+32+1+32+4+32+4+4+4+(170*3)+32+64+4+64)
typedef struct {
    // === 安全校验区域 === (20字节)
    uint32_t magic;                     // 魔术数字 0x48424F58 ("HBOX")
    uint32_t metadata_version_major;    // 元数据结构版本号
    uint32_t metadata_version_minor;
    uint32_t metadata_size;             // 元数据总大小
    uint32_t metadata_crc32;            // 元数据CRC32校验（不包括此字段本身）
    
    // === 固件信息区域 === (69字节)
    char firmware_version[32];          // 固件版本号
    uint8_t target_slot;                // 目标槽位 (0=A, 1=B) - 使用uint8_t确保1字节对齐
    char build_date[32];                // 构建日期
    uint32_t build_timestamp;           // 构建时间戳
    
    // === 设备兼容性区域 === (40字节)
    char device_model[32];              // 设备型号 "STM32H750_HBOX"
    uint32_t hardware_version;          // 硬件版本
    uint32_t bootloader_min_version;    // 最低bootloader版本要求
    
    // === 组件信息区域 === (4+510字节)
    uint32_t component_count;           // 组件数量
    FirmwareComponent components[FIRMWARE_COMPONENT_COUNT]; // 3*170=510字节
    
    // === 安全签名区域 === (100字节)
    uint8_t firmware_hash[32];          // 整个固件包的SHA256哈希
    uint8_t signature[64];              // 数字签名（预留，可选）
    uint32_t signature_algorithm;       // 签名算法标识
    
    // === 预留区域 === (64字节)
    uint8_t reserved[64];               // 预留空间，用于未来扩展
} __attribute__((packed)) FirmwareMetadata;

/* ================================ 静态断言检查 ================================ */

// 编译时检查结构体大小是否正确
#ifdef __cplusplus
static_assert(sizeof(FirmwareComponent) == COMPONENT_STRUCT_SIZE, 
              "FirmwareComponent size mismatch");
static_assert(sizeof(FirmwareMetadata) == METADATA_STRUCT_SIZE, 
              "FirmwareMetadata size mismatch");
#else
// C语言版本的静态断言
_Static_assert(sizeof(FirmwareComponent) == COMPONENT_STRUCT_SIZE, 
               "FirmwareComponent size mismatch");
_Static_assert(sizeof(FirmwareMetadata) == METADATA_STRUCT_SIZE, 
               "FirmwareMetadata size mismatch");
#endif

/* ================================ 内存布局配置 ================================ */

// 外部Flash基地址
#define EXTERNAL_FLASH_BASE         0x90000000
#define SLOT_SIZE                   0x2B0000    // 2.625MB per slot

// 槽A地址配置
#define SLOT_A_BASE                 0x90000000
#define SLOT_A_APPLICATION_ADDR     0x90000000  // 1MB
#define SLOT_A_APPLICATION_SIZE     0x100000
#define SLOT_A_WEBRESOURCES_ADDR    0x90100000  // 1.5MB
#define SLOT_A_WEBRESOURCES_SIZE    0x180000
#define SLOT_A_ADC_MAPPING_ADDR     0x90280000  // 128KB
#define SLOT_A_ADC_MAPPING_SIZE     0x20000

// 槽B地址配置
#define SLOT_B_BASE                 0x902B0000
#define SLOT_B_APPLICATION_ADDR     0x902B0000  // 1MB
#define SLOT_B_APPLICATION_SIZE     0x100000
#define SLOT_B_WEBRESOURCES_ADDR    0x903B0000  // 1.5MB
#define SLOT_B_WEBRESOURCES_SIZE    0x180000
#define SLOT_B_ADC_MAPPING_ADDR     0x90530000  // 128KB
#define SLOT_B_ADC_MAPPING_SIZE     0x20000

// 元数据区域
#define METADATA_ADDR               0x90570000  // 64KB
#define METADATA_SIZE               0x10000

#ifdef __cplusplus
}
#endif

#endif // FIRMWARE_METADATA_H 