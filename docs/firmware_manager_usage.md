# 固件管理器使用说明

## 概述

`FirmwareManager` 是一个用于STM32 HBox双槽固件升级系统的单例类，实现了完整的固件元数据管理、分片升级、Flash操作等功能。

## 主要特性

- **单例模式**: 确保全局唯一的固件管理实例
- **双槽支持**: 支持槽A和槽B的独立管理
- **分片升级**: 支持4KB分片的固件传输和写入
- **元数据管理**: 自动管理固件版本、组件信息等元数据
- **完整性验证**: 支持SHA256校验和验证
- **会话管理**: 支持升级会话的创建、监控、完成和中止
- **自动重启**: 升级完成后自动重启系统

## 内存布局

```
外部Flash布局 (8MB总容量):
┌─────────────────────────────────────────────────────────────────┐
│ 槽A (2.625MB)                                                   │
│ ├─ Application A    (0x90000000, 1MB)                          │
│ ├─ WebResources A   (0x90100000, 1.5MB)                        │
│ └─ ADC Mapping A    (0x90280000, 128KB)                        │
├─────────────────────────────────────────────────────────────────┤
│ 槽B (2.625MB)                                                   │
│ ├─ Application B    (0x902B0000, 1MB)                          │
│ ├─ WebResources B   (0x903B0000, 1.5MB)                        │
│ └─ ADC Mapping B    (0x90530000, 128KB)                        │
├─────────────────────────────────────────────────────────────────┤
│ 用户配置区          (0x90560000, 64KB)                          │
├─────────────────────────────────────────────────────────────────┤
│ 元数据区            (0x90570000, 64KB)                          │
└─────────────────────────────────────────────────────────────────┘
```

## 基本使用

### 1. 初始化

```cpp
#include "firmware/firmware_manager.hpp"

// 在系统启动时初始化
bool init_success = InitializeFirmwareManager();
if (!init_success) {
    // 处理初始化失败
}
```

### 2. 获取当前固件信息

```cpp
FirmwareManager* manager = FirmwareManager::GetInstance();

// 获取设备ID
const char* device_id = manager->GetDeviceId();

// 获取当前固件元数据
const FirmwareMetadata* metadata = manager->GetCurrentMetadata();
if (metadata) {
    printf("当前版本: %s\n", metadata->version);
    printf("当前槽位: %s\n", (metadata->slot == FIRMWARE_SLOT_A) ? "A" : "B");
    printf("构建日期: %s\n", metadata->build_date);
}

// 获取当前运行槽位
FirmwareSlot current_slot = manager->GetCurrentSlot();
```

### 3. 创建升级会话

```cpp
// 准备固件清单
FirmwareMetadata manifest;
memset(&manifest, 0, sizeof(manifest));
strncpy(manifest.version, "1.0.1", sizeof(manifest.version) - 1);
manifest.slot = FIRMWARE_SLOT_B; // 升级到槽B
strncpy(manifest.build_date, "2024-12-08 15:30:00", sizeof(manifest.build_date) - 1);
manifest.component_count = 3;

// 设置组件信息
strncpy(manifest.components[0].name, "application", 31);
manifest.components[0].address = SLOT_B_APPLICATION_ADDR;
manifest.components[0].size = SLOT_B_APPLICATION_SIZE;
// ... 设置其他组件

// 创建升级会话
const char* session_id = "upgrade_session_001";
bool success = manager->CreateUpgradeSession(session_id, &manifest);
```

### 4. 处理固件分片

```cpp
// 准备分片数据
ChunkData chunk;
chunk.chunk_index = 0;
chunk.total_chunks = 256;
chunk.chunk_size = 4096;
chunk.chunk_offset = 0;
chunk.target_address = SLOT_B_APPLICATION_ADDR;
strncpy(chunk.checksum, "calculated_sha256_hash", sizeof(chunk.checksum) - 1);
chunk.data = chunk_buffer; // 4KB数据

// 处理分片
bool success = manager->ProcessFirmwareChunk(session_id, "application", &chunk);
if (success) {
    uint32_t progress = manager->GetUpgradeProgress(session_id);
    printf("升级进度: %d%%\n", progress);
}
```

### 5. 完成升级

```cpp
// 完成升级会话
bool success = manager->CompleteUpgradeSession(session_id);
if (success) {
    // 系统将在2秒后自动重启
    printf("升级完成，系统即将重启...\n");
}
```

## HTTP API集成

### 固件元数据API

```cpp
// GET /api/firmware-metadata
char* response = HandleFirmwareMetadataRequest();
// 返回JSON格式的固件元数据
```

### 升级会话管理API

```cpp
// POST /api/firmware-upgrade
const char* request_body = R"({
    "action": "create",
    "session_id": "upgrade_001",
    "manifest": {
        "version": "1.0.1",
        "slot": "B",
        "build_date": "2024-12-08 15:30:00",
        "components": [...]
    }
})";

char* response = HandleFirmwareUpgradeRequest(request_body);
```

### 分片上传API

```cpp
// POST /api/firmware-upgrade/chunk
const char* metadata_json = R"({
    "session_id": "upgrade_001",
    "component_name": "application",
    "chunk_index": 0,
    "total_chunks": 256,
    "target_address": "0x902B0000",
    "chunk_offset": 0,
    "checksum": "sha256_hash"
})";

uint8_t chunk_data[4096]; // 分片数据
char* response = HandleFirmwareChunkRequest(metadata_json, chunk_data, 4096);
```

## 错误处理

### 常见错误类型

1. **会话不存在**: 升级会话已过期或不存在
2. **地址验证失败**: 目标地址不在有效槽位范围内
3. **校验和不匹配**: 分片数据损坏
4. **Flash操作失败**: 硬件写入或擦除失败
5. **完整性验证失败**: 固件完整性检查失败

### 错误处理示例

```cpp
bool success = manager->ProcessFirmwareChunk(session_id, component_name, &chunk);
if (!success) {
    // 检查会话状态
    const UpgradeSession* session = manager->GetUpgradeSession(session_id);
    if (!session) {
        printf("错误: 升级会话不存在或已过期\n");
        // 重新创建会话
    } else if (session->status == UPGRADE_STATUS_FAILED) {
        printf("错误: 升级会话失败\n");
        // 中止会话并重新开始
        manager->AbortUpgradeSession(session_id);
    }
}
```

## 配置选项

### 内存布局配置

可以通过修改头文件中的宏定义来调整内存布局：

```cpp
// 槽大小配置
#define SLOT_SIZE                   0x2B0000    // 2.625MB per slot

// 分片大小配置
#define CHUNK_SIZE                  4096        // 4KB per chunk

// 会话超时时间
#define UPGRADE_SESSION_TIMEOUT     1800000     // 30分钟
```

### SHA256计算

当前实现使用简化的校验和算法。在生产环境中，建议使用：

1. STM32硬件SHA加速器
2. mbedTLS库
3. 其他经过验证的加密库

```cpp
// 替换 sha256_simple 函数
static void sha256_simple(const uint8_t* data, uint32_t len, char* output) {
    // 使用真正的SHA256实现
    mbedtls_sha256(data, len, hash_bytes, 0);
    // 转换为十六进制字符串
    for (int i = 0; i < 32; i++) {
        sprintf(output + i * 2, "%02x", hash_bytes[i]);
    }
}
```

## 注意事项

1. **线程安全**: 当前实现不是线程安全的，如需多线程使用请添加互斥锁
2. **内存管理**: 确保有足够的堆内存用于会话管理
3. **Flash寿命**: 频繁的擦除操作会影响Flash寿命
4. **电源管理**: 升级过程中确保电源稳定
5. **Bootloader配合**: 需要Bootloader支持双槽启动切换

## 系统集成

### 在main函数中初始化

```cpp
int main(void) {
    // 系统初始化
    HAL_Init();
    SystemClock_Config();
    
    // 初始化固件管理器
    if (!InitializeFirmwareManager()) {
        Error_Handler();
    }
    
    // 其他初始化...
    
    while (1) {
        // 定期清理过期会话
        FirmwareManager::GetInstance()->CleanupExpiredSessions();
        
        // 主循环...
    }
}
```

### 在HTTP服务器中集成

```cpp
// 注册API路由
register_http_handler("/api/firmware-metadata", HandleFirmwareMetadataRequest);
register_http_handler("/api/firmware-upgrade", HandleFirmwareUpgradeRequest);
register_http_handler("/api/firmware-upgrade/chunk", HandleFirmwareChunkRequest);
```

这个固件管理器提供了完整的双槽固件升级功能，支持Web界面的分片上传和实时进度监控，确保固件升级的安全性和可靠性。 