#include "firmware/firmware_manager.hpp"
#include "qspi-w25q64.h"
#include "board_cfg.h"
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <new>
#include "CRC32.hpp"
#include "sha256_simple.h"

// 外部函数声明 (需要在其他地方实现)
extern "C" {
    uint32_t HAL_GetTick(void);
    void HAL_Delay(uint32_t Delay);
    void NVIC_SystemReset(void);
}

// 静态成员初始化
FirmwareManager* FirmwareManager::instance = nullptr;

// 设备ID (可以从硬件获取)
static const char DEVICE_ID[] = "STM32H750_HBOX_001";

// CRC32计算函数（如果没有硬件CRC，使用软件实现）
static uint32_t calculate_crc32(const uint8_t* data, size_t length, uint32_t initial_crc = 0xFFFFFFFF) {
    // 使用STM32硬件CRC或软件CRC32实现
    #ifdef USE_HARDWARE_CRC
        // 使用STM32硬件CRC计算
        return HAL_CRC_Calculate(&hcrc, (uint32_t*)data, length/4);
    #else
        // 标准IEEE 802.3 CRC32查找表（与Python zlib.crc32兼容）
        static const uint32_t crc32_table[256] = {
            0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA, 0x076DC419, 0x706AF48F,
            0xE963A535, 0x9E6495A3, 0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988,
            0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91, 0x1DB71064, 0x6AB020F2,
            0xF3B97148, 0x84BE41DE, 0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
            0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC, 0x14015C4F, 0x63066CD9,
            0xFA0F3D63, 0x8D080DF5, 0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172,
            0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B, 0x35B5A8FA, 0x42B2986C,
            0xDBBBC9D6, 0xACBCF940, 0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
            0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116, 0x21B4F4B5, 0x56B3C423,
            0xCFBA9599, 0xB8BDA50F, 0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924,
            0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D, 0x76DC4190, 0x01DB7106,
            0x98D220BC, 0xEFD5102A, 0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
            0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818, 0x7F6A0DBB, 0x086D3D2D,
            0x91646C97, 0xE6635C01, 0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E,
            0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457, 0x65B0D9C6, 0x12B7E950,
            0x8BBEB8EA, 0xFCB9887C, 0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
            0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2, 0x4ADFA541, 0x3DD895D7,
            0xA4D1C46D, 0xD3D6F4FB, 0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0,
            0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9, 0x5005713C, 0x270241AA,
            0xBE0B1010, 0xC90C2086, 0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
            0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4, 0x59B33D17, 0x2EB40D81,
            0xB7BD5C3B, 0xC0BA6CAD, 0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A,
            0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683, 0xE3630B12, 0x94643B84,
            0x0D6D6A3E, 0x7A6A5AA8, 0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
            0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE, 0xF762575D, 0x806567CB,
            0x196C3671, 0x6E6B06E7, 0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC,
            0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5, 0xD6D6A3E8, 0xA1D1937E,
            0x38D8C2C4, 0x4FDFF252, 0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
            0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60, 0xDF60EFC3, 0xA867DF55,
            0x316E8EEF, 0x4669BE79, 0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236,
            0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F, 0xC5BA3BBE, 0xB2BD0B28,
            0x2BB45A92, 0x5CB36A04, 0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
            0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A, 0x9C0906A9, 0xEB0E363F,
            0x72076785, 0x05005713, 0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38,
            0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21, 0x86D3D2D4, 0xF1D4E242,
            0x68DDB3F8, 0x1FDA836E, 0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
            0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C, 0x8F659EFF, 0xF862AE69,
            0x616BFFD3, 0x166CCF45, 0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2,
            0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB, 0xAED16A4A, 0xD9D65ADC,
            0x40DF0B66, 0x37D83BF0, 0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
            0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6, 0xBAD03605, 0xCDD70693,
            0x54DE5729, 0x23D967BF, 0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94,
            0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D
        };
        
        uint32_t crc = initial_crc;
        for (size_t i = 0; i < length; i++) {
            crc = crc32_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
        }
        return crc ^ 0xFFFFFFFF;
    #endif
}

// 验证固件元数据完整性
static FirmwareValidationResult validate_firmware_metadata(const FirmwareMetadata* metadata) {
    if (!metadata) {
        return FIRMWARE_CORRUPTED;
    }
    
    // 1. 验证魔术数字
    if (metadata->magic != FIRMWARE_MAGIC) {
        return FIRMWARE_INVALID_MAGIC;
    }
    
    // 2. 验证元数据版本兼容性
    if (metadata->metadata_version_major != METADATA_VERSION_MAJOR) {
        return FIRMWARE_INVALID_VERSION;
    }
    
    // 3. 验证设备兼容性
    if (strcmp(metadata->device_model, DEVICE_MODEL_STRING) != 0) {
        return FIRMWARE_INVALID_DEVICE;
    }
    
    // 4. 验证硬件版本兼容性
    if (metadata->hardware_version > HARDWARE_VERSION) {
        return FIRMWARE_INVALID_DEVICE;
    }
    
    // 5. 验证bootloader版本要求
    if (metadata->bootloader_min_version > BOOTLOADER_VERSION) {
        return FIRMWARE_INVALID_VERSION;
    }
    
    // 6. 验证元数据大小
    if (metadata->metadata_size != sizeof(FirmwareMetadata)) {
        return FIRMWARE_INVALID_VERSION;
    }
    
    // 7. 验证CRC32校验和（跳过CRC字段本身）
    const uint8_t* data = (const uint8_t*)metadata;
    size_t crc_field_offset = offsetof(FirmwareMetadata, metadata_crc32);
    
    // 计算CRC字段之前的数据
    uint32_t calculated_crc = calculate_crc32(data, crc_field_offset);
    
    // 计算CRC字段之后的数据，传递前面的CRC作为初始值
    size_t after_crc_offset = crc_field_offset + sizeof(uint32_t);
    size_t remaining_size = sizeof(FirmwareMetadata) - after_crc_offset;
    calculated_crc = calculate_crc32(data + after_crc_offset, remaining_size, calculated_crc ^ 0xFFFFFFFF);
    
    if (calculated_crc != metadata->metadata_crc32) {
        return FIRMWARE_INVALID_CRC;
    }
    
    // 8. 验证组件数量合理性
    if (metadata->component_count > FIRMWARE_COMPONENT_COUNT) {
        return FIRMWARE_CORRUPTED;
    }
    
    return FIRMWARE_VALID;
}

// 生成元数据CRC32校验和
static uint32_t generate_metadata_crc32(FirmwareMetadata* metadata) {
    // 临时清零CRC字段
    uint32_t original_crc = metadata->metadata_crc32;
    metadata->metadata_crc32 = 0;
    
    // 计算整个结构体的CRC32
    uint32_t crc = calculate_crc32((const uint8_t*)metadata, sizeof(FirmwareMetadata));
    
    // 恢复原始CRC值
    metadata->metadata_crc32 = original_crc;
    
    return crc;
}

FirmwareManager::FirmwareManager() 
    : metadata_loaded(false), current_session(nullptr), session_active(false) {
    memset(&current_metadata, 0, sizeof(current_metadata));
    
    // 尝试从Flash加载元数据
    if (LoadMetadataFromFlash()) {
        metadata_loaded = true;
    }
}

FirmwareManager* FirmwareManager::GetInstance() {
    if (instance == nullptr) {
        instance = new FirmwareManager();
    }
    return instance;
}

void FirmwareManager::DestroyInstance() {
    if (instance != nullptr) {
        if (instance->current_session != nullptr) {
            delete instance->current_session;
        }
        delete instance;
        instance = nullptr;
    }
}

void FirmwareManager::InitializeDefaultMetadata() {
    memset(&current_metadata, 0, sizeof(FirmwareMetadata));
    
    // 设置安全字段
    current_metadata.magic = FIRMWARE_MAGIC;
    current_metadata.metadata_version_major = METADATA_VERSION_MAJOR;
    current_metadata.metadata_version_minor = METADATA_VERSION_MINOR;
    current_metadata.metadata_size = sizeof(FirmwareMetadata);
    
    // 设置固件信息
    strcpy(current_metadata.firmware_version, "0.0.1");
    current_metadata.target_slot = (uint8_t)FIRMWARE_SLOT_A;  // 转换为uint8_t
    strcpy(current_metadata.build_date, "2024-12-08 00:00:00");
    current_metadata.build_timestamp = HAL_GetTick();
    
    // 设置设备兼容性
    strcpy(current_metadata.device_model, DEVICE_MODEL_STRING);
    current_metadata.hardware_version = HARDWARE_VERSION;
    current_metadata.bootloader_min_version = BOOTLOADER_VERSION;
    
    // 设置默认组件
    current_metadata.component_count = 3;
    
    // Application组件
    strcpy(current_metadata.components[0].name, "application");
    strcpy(current_metadata.components[0].file, "application_slot_a.hex");
    current_metadata.components[0].address = 0x90000000;
    current_metadata.components[0].size = 1048576;
    current_metadata.components[0].active = true;
    
    // WebResources组件
    strcpy(current_metadata.components[1].name, "webresources");
    strcpy(current_metadata.components[1].file, "webresources.bin");
    current_metadata.components[1].address = 0x90100000;
    current_metadata.components[1].size = 1572864;
    current_metadata.components[1].active = true;
    
    // ADC Mapping组件
    strcpy(current_metadata.components[2].name, "adc_mapping");
    strcpy(current_metadata.components[2].file, "slot_a_adc_mapping.bin");
    current_metadata.components[2].address = 0x90280000;
    current_metadata.components[2].size = 131072;
    current_metadata.components[2].active = true;
    
    // 计算CRC32校验和
    current_metadata.metadata_crc32 = generate_metadata_crc32(&current_metadata);
}

bool FirmwareManager::LoadMetadataFromFlash() {

    // 从Flash读取元数据
    uint32_t flash_address = METADATA_ADDR - EXTERNAL_FLASH_BASE;
    int8_t result = QSPI_W25Qxx_ReadBuffer_WithXIPOrNot(
        (uint8_t*)&current_metadata, 
        flash_address,
        sizeof(FirmwareMetadata)
    );
    
    if (result != QSPI_W25Qxx_OK) {
        APP_ERR("FirmwareManager::LoadMetadataFromFlash: Failed to read metadata from flash");
        return false;
    }
    
    // 验证元数据完整性
    FirmwareValidationResult validation = validate_firmware_metadata(&current_metadata);
    if (validation != FIRMWARE_VALID) {
        APP_ERR("FirmwareManager::LoadMetadataFromFlash: Metadata validation failed - %d", validation);
        return false; // 返回验证失败错误码
    }
    
    return true;
}

bool FirmwareManager::SaveMetadataToFlash() {
    // 退出内存映射模式
    bool was_mapped = QSPI_W25Qxx_IsMemoryMappedMode();
    if (was_mapped) {
        if (QSPI_W25Qxx_ExitMemoryMappedMode() != QSPI_W25Qxx_OK) {
            return false;
        }
    }
    
    // 计算并更新CRC32校验和
    current_metadata.metadata_crc32 = generate_metadata_crc32(&current_metadata);
    
    // 写入元数据到Flash
    uint32_t physical_address = METADATA_ADDR - EXTERNAL_FLASH_BASE;
    int8_t result = QSPI_W25Qxx_WriteBuffer((uint8_t*)&current_metadata, 
                                           physical_address, 
                                           sizeof(FirmwareMetadata));
    
    // 恢复内存映射模式
    if (was_mapped) {
        QSPI_W25Qxx_EnterMemoryMappedMode();
    }
    
    if (result != 0) {
        return false;
    }
    
    return true;
}

const char* FirmwareManager::GetDeviceId() {
    return DEVICE_ID;
}

const FirmwareMetadata* FirmwareManager::GetCurrentMetadata() {
    if (!metadata_loaded) {
        return nullptr;
    }
    return &current_metadata;
}

FirmwareSlot FirmwareManager::GetCurrentSlot() {
    if (!metadata_loaded) {
        return FIRMWARE_SLOT_A; // 默认槽位
    }
    return (FirmwareSlot)current_metadata.target_slot;  // 转换uint8_t到FirmwareSlot
}

FirmwareSlot FirmwareManager::GetTargetUpgradeSlot() {
    FirmwareSlot current_slot = (FirmwareSlot)current_metadata.target_slot;
    return (current_slot == FIRMWARE_SLOT_A) ? FIRMWARE_SLOT_B : FIRMWARE_SLOT_A;
}

bool FirmwareManager::ValidateSlotAddress(uint32_t address, FirmwareSlot slot) {
    if (slot == FIRMWARE_SLOT_A) {
        return (address >= SLOT_A_BASE && address < SLOT_A_BASE + SLOT_SIZE);
    } else if (slot == FIRMWARE_SLOT_B) {
        return (address >= SLOT_B_BASE && address < SLOT_B_BASE + SLOT_SIZE);
    }
    return false;
}

uint32_t FirmwareManager::GetComponentAddress(FirmwareSlot slot, FirmwareComponentType component) {
    if (slot == FIRMWARE_SLOT_A) {
        switch (component) {
            case FIRMWARE_COMPONENT_APPLICATION:
                return SLOT_A_APPLICATION_ADDR;
            case FIRMWARE_COMPONENT_WEBRESOURCES:
                return SLOT_A_WEBRESOURCES_ADDR;
            case FIRMWARE_COMPONENT_ADC_MAPPING:
                return SLOT_A_ADC_MAPPING_ADDR;
            default:
                return 0;
        }
    } else if (slot == FIRMWARE_SLOT_B) {
        switch (component) {
            case FIRMWARE_COMPONENT_APPLICATION:
                return SLOT_B_APPLICATION_ADDR;
            case FIRMWARE_COMPONENT_WEBRESOURCES:
                return SLOT_B_WEBRESOURCES_ADDR;
            case FIRMWARE_COMPONENT_ADC_MAPPING:
                return SLOT_B_ADC_MAPPING_ADDR;
            default:
                return 0;
        }
    }
    return 0;
}

uint32_t FirmwareManager::GetComponentSize(FirmwareComponentType component) {
    switch (component) {
        case FIRMWARE_COMPONENT_APPLICATION:
            return SLOT_A_APPLICATION_SIZE;
        case FIRMWARE_COMPONENT_WEBRESOURCES:
            return SLOT_A_WEBRESOURCES_SIZE;
        case FIRMWARE_COMPONENT_ADC_MAPPING:
            return SLOT_A_ADC_MAPPING_SIZE;
        default:
            return 0;
    }
}

bool FirmwareManager::CreateUpgradeSession(const char* session_id, const FirmwareMetadata* manifest) {
    // 首先清理任何过期或失败的会话
    CleanupExpiredSessions();
    
    // 检查是否已有活动会话
    if (session_active && current_session != nullptr) {
        // 强制清理任何现有会话，无论其状态如何
        APP_DBG("FirmwareManager::CreateUpgradeSession: Force terminating existing session (ID: %s, status: %d)", 
                current_session->session_id, current_session->status);
        
        // 如果旧会话仍在进行中，标记为中止
        if (current_session->status == UPGRADE_STATUS_ACTIVE) {
            current_session->status = UPGRADE_STATUS_ABORTED;
        }
        
        // 清理旧会话
        delete current_session;
        current_session = nullptr;
        session_active = false;
        
        APP_DBG("FirmwareManager::CreateUpgradeSession: Previous session terminated successfully");
    }
    
    // 创建新会话
    current_session = new UpgradeSession();
    if (current_session == nullptr) {
        APP_ERR("FirmwareManager::CreateUpgradeSession: Failed to create new session");
        return false;
    }
    
    // 初始化会话数据
    strncpy(current_session->session_id, session_id, sizeof(current_session->session_id) - 1);
    current_session->status = UPGRADE_STATUS_ACTIVE;
    current_session->manifest = *manifest;
    current_session->created_at = HAL_GetTick();
    current_session->component_count = manifest->component_count;
    current_session->total_progress = 0;
    
    // 初始化组件数据
    for (uint32_t i = 0; i < manifest->component_count && i < FIRMWARE_COMPONENT_COUNT; i++) {
        ComponentUpgradeData* comp = &current_session->components[i];
        strncpy(comp->component_name, manifest->components[i].name, sizeof(comp->component_name) - 1);
        comp->total_chunks = 0;
        comp->received_chunks = 0;
        comp->total_size = manifest->components[i].size;
        comp->received_size = 0;
        comp->base_address = manifest->components[i].address;
        comp->completed = false;
    }
    
    // 擦除目标槽位
    FirmwareSlot target_slot = GetTargetUpgradeSlot();
    // 不做擦除，因为升级时会擦除
    // if (!EraseSlotFlash(target_slot)) {
    //     delete current_session;
    //     current_session = nullptr;
    //     APP_ERR("FirmwareManager::CreateUpgradeSession: Failed to erase target slot");
    //     return false;
    // }
    
    session_active = true;
    APP_DBG("FirmwareManager::CreateUpgradeSession: Session created successfully");
    return true;
}

const UpgradeSession* FirmwareManager::GetUpgradeSession(const char* session_id) {
    // 先清理过期会话
    CleanupExpiredSessions();
    
    if (!session_active || current_session == nullptr) {
        return nullptr;
    }
    
    if (strcmp(current_session->session_id, session_id) != 0) {
        return nullptr;
    }
    
    return current_session;
}

bool FirmwareManager::ProcessFirmwareChunk(const char* session_id, const char* component_name, 
                                          const ChunkData* chunk) {
    // 先清理过期会话
    CleanupExpiredSessions();
    
    if (!session_active || current_session == nullptr) {
        APP_ERR("FirmwareManager::ProcessFirmwareChunk: No active session");
        return false;
    }
    
    APP_DBG("FirmwareManager::ProcessFirmwareChunk: session_id: %s, current_session_id: %s, chunk->chunk_size: %d", session_id, current_session->session_id, chunk->chunk_size);

    if (strcmp(current_session->session_id, session_id) != 0) {
        APP_ERR("FirmwareManager::ProcessFirmwareChunk: Session ID mismatch");
        return false;
    }
    
    if (current_session->status != UPGRADE_STATUS_ACTIVE) {
        APP_ERR("FirmwareManager::ProcessFirmwareChunk: Session status is not active");
        return false;
    }
    
    // 查找对应的组件
    ComponentUpgradeData* target_component = nullptr;
    for (uint32_t i = 0; i < current_session->component_count; i++) {
        if (strcmp(current_session->components[i].component_name, component_name) == 0) {
            target_component = &current_session->components[i];
            break;
        }
    }
    
    if (target_component == nullptr) {
        APP_ERR("FirmwareManager::ProcessFirmwareChunk: Component not found");
        current_session->status = UPGRADE_STATUS_FAILED;  // 标记会话为失败状态
        return false;
    }
    
    // 验证地址
    FirmwareSlot target_slot = GetTargetUpgradeSlot();
    // APP_DBG("FirmwareManager::check address %d, %d", chunk->target_address, target_slot);
    if (!ValidateSlotAddress(chunk->target_address, target_slot)) {
        APP_ERR("FirmwareManager::ProcessFirmwareChunk: Invalid target address");
        current_session->status = UPGRADE_STATUS_FAILED;  // 标记会话为失败状态
        return false;
    }
    
    // 计算实际写入地址 (转换为物理地址)
    uint32_t write_address = chunk->target_address - EXTERNAL_FLASH_BASE;
    
    // 验证校验和
    char calculated_hash[65];
    if (!CalculateSHA256(chunk->data, chunk->chunk_size, calculated_hash)) {
        APP_ERR("FirmwareManager::ProcessFirmwareChunk: Failed to calculate SHA256");
        current_session->status = UPGRADE_STATUS_FAILED;  // 标记会话为失败状态
        return false;
    }
    
    APP_DBG("FirmwareManager::ProcessFirmwareChunk: calculated_hash: %s, chunk->checksum: %s", calculated_hash, chunk->checksum);

    // 进行校验和验证
    if (strcmp(calculated_hash, chunk->checksum) != 0) {
        APP_ERR("FirmwareManager::ProcessFirmwareChunk: Checksum mismatch - chunk_index: %d, calculated: %s, received: %s", 
                chunk->chunk_index, calculated_hash, chunk->checksum);
        current_session->status = UPGRADE_STATUS_FAILED;  // 标记会话为失败状态
        return false; // 校验和不匹配
    }
    APP_DBG("FirmwareManager::ProcessFirmwareChunk: SHA256 checksum verification passed - chunk_index: %d", chunk->chunk_index);
    
    // 写入Flash
    if (!WriteChunkToFlash(write_address, chunk->data, chunk->chunk_size)) {
        APP_ERR("FirmwareManager::ProcessFirmwareChunk: Failed to write chunk to flash");
        current_session->status = UPGRADE_STATUS_FAILED;  // 标记会话为失败状态
        return false;
    }
    
    // 验证写入
    if (!VerifyFlashData(write_address, chunk->data, chunk->chunk_size)) {
        APP_ERR("FirmwareManager::ProcessFirmwareChunk: Failed to verify flash data");
        current_session->status = UPGRADE_STATUS_FAILED;  // 标记会话为失败状态
        return false;
    }
    
    // 更新组件进度
    if (target_component->total_chunks == 0) {
        target_component->total_chunks = chunk->total_chunks;
    }
    
    target_component->received_chunks++;
    target_component->received_size += chunk->chunk_size;
    
    // 检查组件是否完成
    if (target_component->received_chunks >= target_component->total_chunks) {
        target_component->completed = true;
    }
    
    // 更新总进度
    uint32_t total_progress = 0;
    for (uint32_t i = 0; i < current_session->component_count; i++) {
        if (current_session->components[i].total_chunks > 0) {
            uint32_t comp_progress = (current_session->components[i].received_chunks * 100) / 
                                   current_session->components[i].total_chunks;
            total_progress += comp_progress;
        }
    }
    current_session->total_progress = total_progress / current_session->component_count;
    
    return true;
}

bool FirmwareManager::CompleteUpgradeSession(const char* session_id) {
    if (!session_active || current_session == nullptr) {
        return false;
    }
    
    APP_DBG("FirmwareManager::CompleteUpgradeSession start.");

    if (strcmp(current_session->session_id, session_id) != 0) {
        APP_ERR("FirmwareManager::CompleteUpgradeSession: Session ID mismatch");
        return false;
    }
    
    // 检查所有组件是否完成
    for (uint32_t i = 0; i < current_session->component_count; i++) {
        if (!current_session->components[i].completed) {
            APP_ERR("FirmwareManager::CompleteUpgradeSession: Component %s not completed", current_session->components[i].component_name);
            return false; // 还有组件未完成
        }
    }
    
    // 验证固件完整性
    FirmwareSlot target_slot = GetTargetUpgradeSlot();
    if (!VerifyFirmwareIntegrity(target_slot)) {
        current_session->status = UPGRADE_STATUS_FAILED;
        APP_ERR("FirmwareManager::CompleteUpgradeSession: Firmware integrity verification failed");
        return false;
    }
    
    // 更新元数据
    current_metadata = current_session->manifest;
    current_metadata.target_slot = (uint8_t)target_slot;  // 转换FirmwareSlot到uint8_t
    
    // 保存元数据到Flash
    if (!SaveMetadataToFlash()) {
        current_session->status = UPGRADE_STATUS_FAILED;
        APP_ERR("FirmwareManager::CompleteUpgradeSession: Failed to save metadata to flash");
        return false;
    }
    
    // 标记新槽位为可启动
    if (!MarkSlotBootable(target_slot)) {
        APP_ERR("FirmwareManager::CompleteUpgradeSession: Failed to mark slot bootable");
        current_session->status = UPGRADE_STATUS_FAILED;
        return false;
    }
    
    current_session->status = UPGRADE_STATUS_COMPLETED;
    current_session->total_progress = 100;
    
    APP_DBG("FirmwareManager::CompleteUpgradeSession: Upgrade completed, target slot: %d", target_slot);

    // 调度系统重启
    // ScheduleSystemRestart();
    
    return true;
}

bool FirmwareManager::AbortUpgradeSession(const char* session_id) {
    if (!session_active || current_session == nullptr) {
        return false;
    }
    
    if (strcmp(current_session->session_id, session_id) != 0) {
        return false;
    }
    
    current_session->status = UPGRADE_STATUS_ABORTED;
    
    // 清理会话
    delete current_session;
    current_session = nullptr;
    session_active = false;
    
    return true;
}

uint32_t FirmwareManager::GetUpgradeProgress(const char* session_id) {
    if (!session_active || current_session == nullptr) {
        return 0;
    }
    
    if (strcmp(current_session->session_id, session_id) != 0) {
        return 0;
    }
    
    return current_session->total_progress;
}

bool FirmwareManager::EraseSlotFlash(FirmwareSlot slot) {
    uint32_t slot_base = (slot == FIRMWARE_SLOT_A) ? SLOT_A_BASE : SLOT_B_BASE;
    
    // 使用QSPI接口擦除整个槽位
    int8_t result = QSPI_W25Qxx_BufferErase(slot_base - EXTERNAL_FLASH_BASE, SLOT_SIZE);
    
    APP_DBG("FirmwareManager::EraseSlotFlash: Erase slot flash result: %d", result);

    if (result != QSPI_W25Qxx_OK) {
        return false;
    }
    
    return true;
}

bool FirmwareManager::WriteChunkToFlash(uint32_t address, const uint8_t* data, uint32_t size) {
    // 使用QSPI接口写入数据
    int8_t result = QSPI_W25Qxx_WriteBuffer_WithXIPOrNot((uint8_t*)data, address, size);
    
    if (result != QSPI_W25Qxx_OK) {
        return false;
    }
    
    return true;
}

bool FirmwareManager::VerifyFlashData(uint32_t address, const uint8_t* data, uint32_t size) {
    // 分配临时缓冲区用于读取验证
    uint8_t* read_buffer = new(std::nothrow) uint8_t[size];
    if (!read_buffer) {
        return false;
    }
    
    // 使用QSPI接口读取数据
    int8_t result = QSPI_W25Qxx_ReadBuffer_WithXIPOrNot(read_buffer, address, size);
    
    if (result != QSPI_W25Qxx_OK) {
        delete[] read_buffer;
        return false;
    }
    
    // 比较数据
    bool verify_ok = (memcmp(data, read_buffer, size) == 0);
    
    delete[] read_buffer;
    return verify_ok;
}

bool FirmwareManager::CalculateSHA256(const uint8_t* data, uint32_t size, char* hash_output) {
    if (!data || size == 0 || !hash_output) {
        return false;
    }
    
    // 使用我们的轻量级SHA256实现
    return sha256_calculate(data, size, hash_output) == 1;
}

bool FirmwareManager::VerifyFirmwareIntegrity(FirmwareSlot slot) {
    // 简化的完整性验证 - 实际项目中应该验证每个组件的校验和
    // 这里只检查关键区域是否不为空
    
    uint32_t app_addr = GetComponentAddress(slot, FIRMWARE_COMPONENT_APPLICATION);
    uint8_t test_buffer[256];
    
    // 使用QSPI接口读取数据进行验证
    int8_t result = QSPI_W25Qxx_ReadBuffer_WithXIPOrNot(
        test_buffer, 
        app_addr - EXTERNAL_FLASH_BASE, 
        sizeof(test_buffer)
    );
    
    if (result != QSPI_W25Qxx_OK) {
        return false;
    }
    
    // 检查是否全为0xFF (未写入)
    bool all_ff = true;
    for (uint32_t i = 0; i < sizeof(test_buffer); i++) {
        if (test_buffer[i] != 0xFF) {
            all_ff = false;
            break;
        }
    }
    
    return !all_ff; // 如果不全是0xFF，说明有数据写入
}

bool FirmwareManager::MarkSlotBootable(FirmwareSlot slot) {
    // 这里应该设置Bootloader的启动标志
    // 具体实现取决于Bootloader的设计
    // 可能需要写入特定的标志到特定地址
    
    // 示例：在用户配置区域写入启动槽位标志
    uint8_t boot_flag = (slot == FIRMWARE_SLOT_A) ? 0xAA : 0xBB;
    
    // 使用QSPI接口写入启动标志
    int8_t result = QSPI_W25Qxx_WriteBuffer_WithXIPOrNot(
        &boot_flag, 
        USER_CONFIG_ADDR - EXTERNAL_FLASH_BASE, 
        1
    );
    
    if (result != QSPI_W25Qxx_OK) {
        return false;
    }
    
    return true;
}

bool FirmwareManager::SwitchBootSlot(FirmwareSlot target_slot) {
    return MarkSlotBootable(target_slot);
}

void FirmwareManager::ScheduleSystemRestart() {
    // 延迟2秒后重启系统
    HAL_Delay(2000);
    SystemRestart();
}

void FirmwareManager::SystemRestart() {
    NVIC_SystemReset();
}

void FirmwareManager::CleanupExpiredSessions() {
    if (!session_active || current_session == nullptr) {
        return;
    }
    
    uint32_t current_time = HAL_GetTick();
    uint32_t session_age = current_time - current_session->created_at;
    
    // 清理超时、失败或已中止的会话
    if (session_age > UPGRADE_SESSION_TIMEOUT || 
        current_session->status == UPGRADE_STATUS_FAILED ||
        current_session->status == UPGRADE_STATUS_ABORTED ||
        current_session->status == UPGRADE_STATUS_COMPLETED) {
        
        APP_DBG("FirmwareManager::CleanupExpiredSessions: Cleaning up session with status %d, age %d ms", 
                current_session->status, session_age);
        
        delete current_session;
        current_session = nullptr;
        session_active = false;
    }
}

// 添加主动会话清理函数，在多个API调用时使用
void FirmwareManager::ForceCleanupSession() {
    if (session_active && current_session != nullptr) {
        APP_DBG("FirmwareManager::ForceCleanupSession: Force cleaning session (ID: %s, status: %d)", 
                current_session->session_id, current_session->status);
        
        // 标记为中止状态
        current_session->status = UPGRADE_STATUS_ABORTED;
        
        delete current_session;
        current_session = nullptr;
        session_active = false;
    }
}
