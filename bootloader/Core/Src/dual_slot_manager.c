/**
 * @file dual_slot_manager.c
 * @brief 双槽升级管理模块实现
 * @author STM32 Project
 * @date 2025
 */

#include "dual_slot_config.h"
#include "qspi-w25q64.h"
#include "board_cfg.h"
#include <string.h>

/* ================================ 全局变量 ================================ */

// 全局固件元数据
firmware_metadata_t g_firmware_metadata = {0};

// 默认元数据（用于初始化）
static const firmware_metadata_t default_metadata = {
    .magic_number = FIRMWARE_METADATA_MAGIC,
    .current_version = 0x01000000,  // 版本 1.0.0.0
    
    .slot_a_version = 0x01000000,
    .slot_a_address = SLOT_A_APPLICATION_BASE,
    .slot_a_size = SLOT_A_APPLICATION_SIZE,
    .slot_a_valid = 0,
    .slot_a_crc_valid = 0,
    .slot_a_crc32 = 0,
    
    .slot_b_version = 0x00000000,
    .slot_b_address = SLOT_B_APPLICATION_BASE,
    .slot_b_size = SLOT_B_APPLICATION_SIZE,
    .slot_b_valid = 0,
    .slot_b_crc_valid = 0,
    .slot_b_crc32 = 0,
    
    .current_slot = SLOT_A,
    .upgrade_slot = SLOT_B,
    .boot_count = 0,
    .rollback_enabled = 1,
    
    .boot_timestamp = 0,
    .upgrade_timestamp = 0,
    .metadata_crc32 = 0  // 在保存时计算
};

/* ================================ 私有函数声明 ================================ */

static uint32_t calculate_metadata_crc32(const firmware_metadata_t* metadata);
static bool validate_metadata(const firmware_metadata_t* metadata);
static void init_default_metadata(void);

/* ================================ CRC32计算 ================================ */

// CRC32表
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

/**
 * @brief 计算CRC32校验值
 * @param data 数据指针
 * @param length 数据长度
 * @return CRC32校验值
 */
uint32_t DualSlot_CalculateCRC32(const uint8_t* data, uint32_t length)
{
    uint32_t crc = 0xFFFFFFFF;
    
    for (uint32_t i = 0; i < length; i++) {
        crc = crc32_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    
    return crc ^ 0xFFFFFFFF;
}

/**
 * @brief 计算元数据CRC32（不包含crc字段本身）
 * @param metadata 元数据指针
 * @return CRC32校验值
 */
static uint32_t calculate_metadata_crc32(const firmware_metadata_t* metadata)
{
    // 计算CRC时排除最后的CRC字段
    uint32_t size = sizeof(firmware_metadata_t) - sizeof(uint32_t);
    return DualSlot_CalculateCRC32((const uint8_t*)metadata, size);
}

/**
 * @brief 验证元数据的有效性
 * @param metadata 元数据指针
 * @return true=有效, false=无效
 */
static bool validate_metadata(const firmware_metadata_t* metadata)
{
    // 检查魔术数字
    if (metadata->magic_number != FIRMWARE_METADATA_MAGIC) {
        BOOT_ERR("Invalid magic number: 0x%08X", metadata->magic_number);
        return false;
    }
    
    // 验证CRC
    uint32_t calculated_crc = calculate_metadata_crc32(metadata);
    if (calculated_crc != metadata->metadata_crc32) {
        BOOT_ERR("Metadata CRC mismatch: calculated=0x%08X, stored=0x%08X", 
                 calculated_crc, metadata->metadata_crc32);
        return false;
    }
    
    // 验证槽地址
    if (metadata->slot_a_address != SLOT_A_APPLICATION_BASE ||
        metadata->slot_b_address != SLOT_B_APPLICATION_BASE) {
        BOOT_ERR("Invalid slot addresses: A=0x%08X, B=0x%08X", 
                 metadata->slot_a_address, metadata->slot_b_address);
        return false;
    }
    
    // 验证当前槽
    if (metadata->current_slot != SLOT_A && metadata->current_slot != SLOT_B) {
        BOOT_ERR("Invalid current slot: %d", metadata->current_slot);
        return false;
    }
    
    return true;
}

/**
 * @brief 初始化默认元数据
 */
static void init_default_metadata(void)
{
    memcpy(&g_firmware_metadata, &default_metadata, sizeof(firmware_metadata_t));
    g_firmware_metadata.metadata_crc32 = calculate_metadata_crc32(&g_firmware_metadata);
    
    BOOT_DBG("Initialized default metadata");
    BOOT_DBG("Current slot: %s", g_firmware_metadata.current_slot == SLOT_A ? "A" : "B");
    BOOT_DBG("Upgrade slot: %s", g_firmware_metadata.upgrade_slot == SLOT_A ? "A" : "B");
}

/* ================================ 公共函数实现 ================================ */

/**
 * @brief 双槽系统初始化
 * @return 0=成功, <0=失败
 */
int8_t DualSlot_Init(void)
{
    BOOT_DBG("Dual slot system initializing...");
    
    if (!DualSlot_IsEnabled()) {
        BOOT_DBG("Dual slot disabled, using legacy mode");
        return 0;
    }
    
    // 尝试从Flash加载元数据
    if (DualSlot_LoadMetadata(&g_firmware_metadata) != 0) {
        BOOT_DBG("Failed to load metadata, initializing default");
        init_default_metadata();
        
        // 保存默认元数据到Flash
        if (DualSlot_SaveMetadata(&g_firmware_metadata) != 0) {
            BOOT_ERR("Failed to save default metadata");
            return -1;
        }
    }
    
    BOOT_DBG("Dual slot system initialized successfully");
    BOOT_DBG("Current slot: %s (version: 0x%08X)", 
             g_firmware_metadata.current_slot == SLOT_A ? "A" : "B",
             g_firmware_metadata.current_version);
    
    return 0;
}

/**
 * @brief 从Flash加载元数据
 * @param metadata 元数据缓冲区
 * @return 0=成功, <0=失败
 */
int8_t DualSlot_LoadMetadata(firmware_metadata_t* metadata)
{
    if (!metadata) {
        return -1;
    }
    
    // 退出内存映射模式以便读取
    bool was_mapped = QSPI_W25Qxx_IsMemoryMappedMode();
    if (was_mapped) {
        if (QSPI_W25Qxx_ExitMemoryMappedMode() != QSPI_W25Qxx_OK) {
            BOOT_ERR("Failed to exit memory mapped mode");
            return -2;
        }
    }
    
    // 计算Flash中的元数据地址（相对于Flash基地址）
    uint32_t flash_address = FIRMWARE_METADATA_BASE - EXTERNAL_FLASH_BASE;
    
    // 从Flash读取元数据
    int8_t result = QSPI_W25Qxx_ReadBuffer((uint8_t*)metadata, flash_address, sizeof(firmware_metadata_t));
    
    // 恢复内存映射模式
    if (was_mapped) {
        QSPI_W25Qxx_EnterMemoryMappedMode();
    }
    
    if (result != QSPI_W25Qxx_OK) {
        BOOT_ERR("Failed to read metadata from Flash");
        return -3;
    }
    
    // 验证元数据
    if (!validate_metadata(metadata)) {
        BOOT_ERR("Metadata validation failed");
        return -4;
    }
    
    BOOT_DBG("Metadata loaded successfully");
    return 0;
}

/**
 * @brief 保存元数据到Flash
 * @param metadata 元数据
 * @return 0=成功, <0=失败
 */
int8_t DualSlot_SaveMetadata(const firmware_metadata_t* metadata)
{
    if (!metadata) {
        return -1;
    }
    
    // 计算并更新CRC
    firmware_metadata_t temp_metadata;
    memcpy(&temp_metadata, metadata, sizeof(firmware_metadata_t));
    temp_metadata.metadata_crc32 = calculate_metadata_crc32(&temp_metadata);
    
    // 退出内存映射模式
    bool was_mapped = QSPI_W25Qxx_IsMemoryMappedMode();
    if (was_mapped) {
        if (QSPI_W25Qxx_ExitMemoryMappedMode() != QSPI_W25Qxx_OK) {
            BOOT_ERR("Failed to exit memory mapped mode");
            return -2;
        }
    }
    
    // 计算Flash中的元数据地址
    uint32_t flash_address = FIRMWARE_METADATA_BASE - EXTERNAL_FLASH_BASE;
    
    // 擦除元数据扇区
    if (QSPI_W25Qxx_SectorErase(flash_address) != QSPI_W25Qxx_OK) {
        BOOT_ERR("Failed to erase metadata sector");
        if (was_mapped) QSPI_W25Qxx_EnterMemoryMappedMode();
        return -3;
    }
    
    // 写入元数据
    int8_t result = QSPI_W25Qxx_WriteBuffer((uint8_t*)&temp_metadata, flash_address, sizeof(firmware_metadata_t));
    
    // 恢复内存映射模式
    if (was_mapped) {
        QSPI_W25Qxx_EnterMemoryMappedMode();
    }
    
    if (result != QSPI_W25Qxx_OK) {
        BOOT_ERR("Failed to write metadata to Flash");
        return -4;
    }
    
    BOOT_DBG("Metadata saved successfully");
    return 0;
}

/**
 * @brief 获取指定槽的信息
 * @param slot 槽编号 (SLOT_A 或 SLOT_B)
 * @param slot_info 槽信息结构体指针
 * @return 0=成功, <0=失败
 */
int8_t DualSlot_GetSlotInfo(uint8_t slot, slot_info_t* slot_info)
{
    if (!slot_info || (slot != SLOT_A && slot != SLOT_B)) {
        return -1;
    }
    
    if (slot == SLOT_A) {
        slot_info->base_address = SLOT_A_BASE;
        slot_info->application_address = SLOT_A_APPLICATION_BASE;
        slot_info->application_size = SLOT_A_APPLICATION_SIZE;
        slot_info->webresources_address = SLOT_A_WEBRESOURCES_BASE;
        slot_info->webresources_size = SLOT_A_WEBRESOURCES_SIZE;
        slot_info->adc_mapping_address = SLOT_A_ADC_MAPPING_BASE;
        slot_info->adc_mapping_size = SLOT_A_ADC_MAPPING_SIZE;
        slot_info->total_size = SLOT_A_TOTAL_SIZE;
    } else {
        slot_info->base_address = SLOT_B_BASE;
        slot_info->application_address = SLOT_B_APPLICATION_BASE;
        slot_info->application_size = SLOT_B_APPLICATION_SIZE;
        slot_info->webresources_address = SLOT_B_WEBRESOURCES_BASE;
        slot_info->webresources_size = SLOT_B_WEBRESOURCES_SIZE;
        slot_info->adc_mapping_address = SLOT_B_ADC_MAPPING_BASE;
        slot_info->adc_mapping_size = SLOT_B_ADC_MAPPING_SIZE;
        slot_info->total_size = SLOT_B_TOTAL_SIZE;
    }
    
    return 0;
}

/**
 * @brief 获取指定槽的Application地址
 * @param slot 槽编号 (SLOT_A 或 SLOT_B)
 * @return Application地址，失败返回0
 */
uint32_t DualSlot_GetSlotApplicationAddress(uint8_t slot)
{
    if (slot == SLOT_A) {
        return SLOT_A_APPLICATION_BASE;
    } else if (slot == SLOT_B) {
        return SLOT_B_APPLICATION_BASE;
    }
    return 0;
}

/**
 * @brief 获取当前运行的槽
 * @return SLOT_A 或 SLOT_B
 */
uint8_t DualSlot_GetCurrentSlot(void)
{
    if (!DualSlot_IsEnabled()) {
        return SLOT_A; // 默认返回槽A用于兼容
    }
    return g_firmware_metadata.current_slot;
}

/**
 * @brief 获取升级目标槽
 * @return SLOT_A 或 SLOT_B
 */
uint8_t DualSlot_GetUpgradeSlot(void)
{
    if (!DualSlot_IsEnabled()) {
        return SLOT_A; // 默认返回槽A用于兼容
    }
    return g_firmware_metadata.upgrade_slot;
}

/**
 * @brief 设置当前运行的槽
 * @param slot 槽编号 (SLOT_A 或 SLOT_B)
 * @return 0=成功, <0=失败
 */
int8_t DualSlot_SetCurrentSlot(uint8_t slot)
{
    if (!DualSlot_IsEnabled()) {
        return 0; // 禁用状态下忽略设置
    }
    
    if (slot != SLOT_A && slot != SLOT_B) {
        return -1;
    }
    
    g_firmware_metadata.current_slot = slot;
    g_firmware_metadata.upgrade_slot = (slot == SLOT_A) ? SLOT_B : SLOT_A;
    
    // 保存到Flash
    return DualSlot_SaveMetadata(&g_firmware_metadata);
}

/**
 * @brief 验证指定槽的固件完整性
 * @param slot 槽编号 (SLOT_A 或 SLOT_B)
 * @return 0=有效, <0=无效
 */
int8_t DualSlot_ValidateSlot(uint8_t slot)
{
    if (slot != SLOT_A && slot != SLOT_B) {
        return -1;
    }
    
    // 检查槽是否标记为有效
    if (slot == SLOT_A) {
        if (!g_firmware_metadata.slot_a_valid) {
            BOOT_DBG("Slot A marked as invalid");
            return -2;
        }
    } else {
        if (!g_firmware_metadata.slot_b_valid) {
            BOOT_DBG("Slot B marked as invalid");
            return -2;
        }
    }
    
    // 获取槽地址
    uint32_t app_address = DualSlot_GetSlotApplicationAddress(slot);
    if (app_address == 0) {
        return -3;
    }
    
    // 检查向量表的有效性
    uint32_t stack_ptr = *(__IO uint32_t*)app_address;
    uint32_t reset_handler = *(__IO uint32_t*)(app_address + 4);
    
    // 验证栈指针 (应该指向RAM区域)
    if ((stack_ptr & 0xFF000000) != 0x20000000) {
        BOOT_DBG("Slot %c: Invalid stack pointer: 0x%08X", 
                 (slot == SLOT_A) ? 'A' : 'B', stack_ptr);
        return -4;
    }
    
    // 验证Reset Handler地址 (应该指向Flash区域，且为Thumb模式)
    if ((reset_handler & 0xFF000000) != 0x90000000 || !(reset_handler & 0x1)) {
        BOOT_DBG("Slot %c: Invalid reset handler: 0x%08X", 
                 (slot == SLOT_A) ? 'A' : 'B', reset_handler);
        return -5;
    }
    
    BOOT_DBG("Slot %c validation passed", (slot == SLOT_A) ? 'A' : 'B');
    return 0;
}

/**
 * @brief 切换到另一个槽
 * @return 0=成功, <0=失败
 */
int8_t DualSlot_SwitchSlot(void)
{
    if (!DualSlot_IsEnabled()) {
        return 0; // 禁用状态下忽略切换
    }
    
    uint8_t new_slot = (g_firmware_metadata.current_slot == SLOT_A) ? SLOT_B : SLOT_A;
    
    // 验证目标槽
    if (DualSlot_ValidateSlot(new_slot) != 0) {
        BOOT_ERR("Target slot %c is invalid", (new_slot == SLOT_A) ? 'A' : 'B');
        return -1;
    }
    
    // 切换槽
    return DualSlot_SetCurrentSlot(new_slot);
}

/**
 * @brief 检查双槽功能是否启用
 * @return true=启用, false=禁用
 */
bool DualSlot_IsEnabled(void)
{
    return DUAL_SLOT_ENABLE == 1;
}

/**
 * @brief 获取兼容模式的Application地址（原有接口）
 * @return Application地址
 */
uint32_t DualSlot_GetLegacyApplicationAddress(void)
{
    if (!DualSlot_IsEnabled()) {
        // 禁用双槽时，使用原来的固定地址
        return EXTERNAL_FLASH_BASE; // 0x90000000，与原来的 W25Qxx_Mem_Addr 相同
    }
    
    // 启用双槽时，返回当前槽的地址
    return DualSlot_GetSlotApplicationAddress(DualSlot_GetCurrentSlot());
} 