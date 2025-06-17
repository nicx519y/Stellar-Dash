#ifndef FIRMWARE_MANAGER_HPP
#define FIRMWARE_MANAGER_HPP

#include <stdint.h>
#include <stdbool.h>
#include <cstring>

// 包含统一的固件元数据定义
#include "../../../common/firmware_metadata.h"

#ifdef __cplusplus
extern "C" {
#endif

// 升级状态（firmware_manager特有的定义）
typedef enum {
    UPGRADE_STATUS_IDLE = 0,
    UPGRADE_STATUS_ACTIVE,
    UPGRADE_STATUS_COMPLETED,
    UPGRADE_STATUS_ABORTED,
    UPGRADE_STATUS_FAILED
} UpgradeStatus;

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
    // 单例模式
    static FirmwareManager* GetInstance();
    static void DestroyInstance();
    
    // 初始化和配置
    bool Initialize();
    
    // 元数据管理
    const FirmwareMetadata* GetCurrentMetadata();
    bool UpdateMetadata(const FirmwareMetadata* metadata);
    
    // 槽位管理
    FirmwareSlot GetCurrentSlot();
    
    // 升级管理
    bool CreateUpgradeSession(const char* session_id, const FirmwareMetadata* manifest);
    
    // 分片处理
    bool ProcessFirmwareChunk(const char* session_id, const char* component_name, 
                             const ChunkData* chunk);
    
    // 升级完成
    bool CompleteUpgradeSession(const char* session_id);
    
    // 升级中止
    bool AbortUpgradeSession(const char* session_id);
    
    // 进度查询
    uint32_t GetUpgradeProgress(const char* session_id);
    
    // 会话管理
    void CleanupExpiredSessions();
    
    // 强制清理
    void ForceCleanupSession();
    
    // 固件验证
    bool VerifyFirmwareIntegrity(FirmwareSlot slot);
    
    // 槽位切换
    bool SwitchBootSlot(FirmwareSlot target_slot);
    
    // 获取目标升级槽位
    FirmwareSlot GetTargetUpgradeSlot();
    
    // 系统重启
    void ScheduleSystemRestart();
};

#endif // __cplusplus

#endif // FIRMWARE_MANAGER_HPP
