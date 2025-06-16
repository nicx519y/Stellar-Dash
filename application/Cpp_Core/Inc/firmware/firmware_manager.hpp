#ifndef FIRMWARE_MANAGER_HPP
#define FIRMWARE_MANAGER_HPP

#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// 固件组件类型
typedef enum {
    FIRMWARE_COMPONENT_APPLICATION = 0,
    FIRMWARE_COMPONENT_WEBRESOURCES,
    FIRMWARE_COMPONENT_ADC_MAPPING,
    FIRMWARE_COMPONENT_COUNT
} FirmwareComponentType;

// 固件槽位
typedef enum {
    FIRMWARE_SLOT_A = 0,
    FIRMWARE_SLOT_B,
    FIRMWARE_SLOT_COUNT
} FirmwareSlot;

// 升级状态
typedef enum {
    UPGRADE_STATUS_IDLE = 0,
    UPGRADE_STATUS_ACTIVE,
    UPGRADE_STATUS_COMPLETED,
    UPGRADE_STATUS_ABORTED,
    UPGRADE_STATUS_FAILED
} UpgradeStatus;

// 固件组件信息
typedef struct {
    char name[32];              // 组件名称
    char file[64];              // 文件名
    uint32_t address;           // 目标地址
    uint32_t size;              // 组件大小
    char sha256[65];            // SHA256校验和
    bool active;                // 是否激活
} __attribute__((packed)) FirmwareComponent;

// 安全固件元数据结构（与release.py完全对齐）
// 总大小: 807字节 (20+32+1+32+4+32+4+4+4+(170*3)+32+64+4+64)
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

// 兼容性定义
#define FIRMWARE_MAGIC                  0x48424F58  // "HBOX"
#define METADATA_VERSION_MAJOR          1
#define METADATA_VERSION_MINOR          0
#define DEVICE_MODEL_STRING             "STM32H750_HBOX"
#define BOOTLOADER_VERSION              0x00010000  // 1.0.0
#define HARDWARE_VERSION                0x00010000  // 1.0.0

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

// 分片数据
typedef struct {
    uint32_t chunk_index;       // 分片索引
    uint32_t total_chunks;      // 总分片数
    uint32_t chunk_size;        // 分片大小
    uint32_t chunk_offset;      // 分片偏移
    uint32_t target_address;    // 目标地址
    char checksum[65];          // SHA256校验和
    uint8_t* data;              // 数据指针
} ChunkData;

// 组件升级数据
typedef struct {
    char component_name[32];    // 组件名称
    uint32_t total_chunks;      // 总分片数
    uint32_t received_chunks;   // 已接收分片数
    uint32_t total_size;        // 总大小
    uint32_t received_size;     // 已接收大小
    uint32_t base_address;      // 基地址
    bool completed;             // 是否完成
} ComponentUpgradeData;

// 升级会话
typedef struct {
    char session_id[64];        // 会话ID
    UpgradeStatus status;       // 状态
    FirmwareMetadata manifest;  // 固件清单
    uint32_t created_at;        // 创建时间
    ComponentUpgradeData components[FIRMWARE_COMPONENT_COUNT];
    uint32_t component_count;   // 组件数量
    uint32_t total_progress;    // 总进度 (0-100)
} UpgradeSession;

// 内存布局配置
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

// 配置和元数据区域
#define USER_CONFIG_ADDR            0x90560000  // 64KB
#define USER_CONFIG_SIZE            0x10000
#define METADATA_ADDR               0x90570000  // 64KB
#define METADATA_SIZE               0x10000

// 分片大小配置
#define CHUNK_SIZE                  4096        // 4KB per chunk
#define MAX_CHUNKS_PER_COMPONENT    512         // 最大分片数

// 会话超时时间 (毫秒)
#define UPGRADE_SESSION_TIMEOUT     300000      // 5分钟 (原来30分钟太长)

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus

class FirmwareManager {
private:
    static FirmwareManager* instance;
    
    // 当前固件元数据
    FirmwareMetadata current_metadata;
    bool metadata_loaded;
    
    // 升级会话
    UpgradeSession* current_session;
    bool session_active;
    
    // 私有构造函数
    FirmwareManager();
    
    // 禁用拷贝构造和赋值
    FirmwareManager(const FirmwareManager&) = delete;
    FirmwareManager& operator=(const FirmwareManager&) = delete;
    
    // 内部方法
    bool LoadMetadataFromFlash();
    bool SaveMetadataToFlash();
    bool ValidateSlotAddress(uint32_t address, FirmwareSlot slot);
    uint32_t GetComponentAddress(FirmwareSlot slot, FirmwareComponentType component);
    uint32_t GetComponentSize(FirmwareComponentType component);
    bool EraseSlotFlash(FirmwareSlot slot);
    bool WriteChunkToFlash(uint32_t address, const uint8_t* data, uint32_t size);
    bool VerifyFlashData(uint32_t address, const uint8_t* data, uint32_t size);
    bool CalculateSHA256(const uint8_t* data, uint32_t size, char* hash_output);
    void InitializeDefaultMetadata();
    bool MarkSlotBootable(FirmwareSlot slot);
    void SystemRestart();
    
public:
    // 获取单例实例
    static FirmwareManager* GetInstance();
    
    // 销毁单例实例
    static void DestroyInstance();
    
    // 初始化
    bool Initialize();
    
    // 获取设备ID
    const char* GetDeviceId();
    
    // 获取当前固件元数据
    const FirmwareMetadata* GetCurrentMetadata();
    
    // 获取当前运行槽位
    FirmwareSlot GetCurrentSlot();
    
    // 创建升级会话
    bool CreateUpgradeSession(const char* session_id, const FirmwareMetadata* manifest);
    
    // 获取升级会话
    const UpgradeSession* GetUpgradeSession(const char* session_id);
    
    // 处理固件分片
    bool ProcessFirmwareChunk(const char* session_id, const char* component_name, 
                             const ChunkData* chunk);
    
    // 完成升级会话
    bool CompleteUpgradeSession(const char* session_id);
    
    // 中止升级会话
    bool AbortUpgradeSession(const char* session_id);
    
    // 获取升级进度
    uint32_t GetUpgradeProgress(const char* session_id);
    
    // 清理过期会话
    void CleanupExpiredSessions();
    
    // 强制清理当前会话
    void ForceCleanupSession();
    
    // 验证固件完整性
    bool VerifyFirmwareIntegrity(FirmwareSlot slot);
    
    // 切换启动槽位
    bool SwitchBootSlot(FirmwareSlot target_slot);
    
    // 获取目标升级槽位
    FirmwareSlot GetTargetUpgradeSlot();
    
    // 系统重启 (延迟2秒)
    void ScheduleSystemRestart();
};

#endif // __cplusplus

#endif // FIRMWARE_MANAGER_HPP
