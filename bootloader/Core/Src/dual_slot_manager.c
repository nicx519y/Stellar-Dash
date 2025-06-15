#include "dual_slot_config.h"
#include "qspi-w25q64.h"
#include "board_cfg.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

/* ================================ 私有变量 ================================ */

static FirmwareMetadata g_current_metadata;
static bool g_metadata_loaded = false;

/* ================================ 私有函数声明 ================================ */

static uint32_t calculate_crc32_skip_field(const uint8_t* data, size_t length, size_t skip_offset, size_t skip_size);
static FirmwareValidationResult validate_metadata(const FirmwareMetadata* metadata);
static void init_default_metadata(FirmwareMetadata* metadata);
static void print_debug_info(const char* format, ...);

/* ================================ CRC32计算函数 ================================ */

// 标准CRC32查找表 (IEEE 802.3)
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

static uint32_t calculate_crc32_skip_field(const uint8_t* data, size_t length, size_t skip_offset, size_t skip_size) {
    uint32_t crc = 0xFFFFFFFF;
    
    for (size_t i = 0; i < length; i++) {
        // 跳过指定区域（CRC32字段本身）
        if (i >= skip_offset && i < skip_offset + skip_size) {
            continue;
        }
        crc = crc32_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    
    return crc ^ 0xFFFFFFFF;
}

/* ================================ 元数据验证函数 ================================ */

static FirmwareValidationResult validate_metadata(const FirmwareMetadata* metadata) {
    if (!metadata) {
        return FIRMWARE_CORRUPTED;
    }
    
    // 1. 验证魔术数字
    if (metadata->magic != FIRMWARE_MAGIC) {
        print_debug_info("Metadata validation failed: Invalid magic number (0x%08X != 0x%08X)", 
                         metadata->magic, FIRMWARE_MAGIC);
        return FIRMWARE_INVALID_MAGIC;
    }
    
    // 2. 验证元数据版本兼容性
    if (metadata->metadata_version_major != METADATA_VERSION_MAJOR) {
        print_debug_info("Metadata validation failed: Version incompatible (%d.%d != %d.%d)", 
                         metadata->metadata_version_major, metadata->metadata_version_minor,
                         METADATA_VERSION_MAJOR, METADATA_VERSION_MINOR);
        return FIRMWARE_INVALID_VERSION;
    }
    
    // 3. 验证设备兼容性
    if (strncmp(metadata->device_model, DEVICE_MODEL_STRING, sizeof(metadata->device_model)) != 0) {
        print_debug_info("Metadata validation failed: Device model mismatch (%s != %s)", 
                         metadata->device_model, DEVICE_MODEL_STRING);
        return FIRMWARE_INVALID_DEVICE;
    }
    
    // 4. 验证硬件版本兼容性
    if (metadata->hardware_version > HARDWARE_VERSION) {
        print_debug_info("Metadata validation failed: Hardware version too high (0x%08X > 0x%08X)", 
                         metadata->hardware_version, HARDWARE_VERSION);
        return FIRMWARE_INVALID_DEVICE;
    }
    
    // 5. 验证bootloader版本要求
    if (metadata->bootloader_min_version > BOOTLOADER_VERSION) {
        print_debug_info("Metadata validation failed: Bootloader version too high (0x%08X > 0x%08X)", 
                         metadata->bootloader_min_version, BOOTLOADER_VERSION);
        return FIRMWARE_INVALID_VERSION;
    }
    
    // 6. 验证元数据大小
    if (metadata->metadata_size != METADATA_STRUCT_SIZE) {
        print_debug_info("Metadata validation failed: Size mismatch (%d != %d)", 
                         metadata->metadata_size, METADATA_STRUCT_SIZE);
        return FIRMWARE_INVALID_VERSION;
    }
    
    // 7. 验证CRC32校验和
    size_t crc_offset = offsetof(FirmwareMetadata, metadata_crc32);
    uint32_t calculated_crc = calculate_crc32_skip_field((const uint8_t*)metadata, 
                                                       METADATA_STRUCT_SIZE, 
                                                       crc_offset, 
                                                       sizeof(uint32_t));
    
    if (calculated_crc != metadata->metadata_crc32) {
        print_debug_info("Metadata validation failed: CRC32 error (0x%08X != 0x%08X)", 
                         calculated_crc, metadata->metadata_crc32);
        return FIRMWARE_INVALID_CRC;
    }
    
    // 8. 验证组件数量合理性
    if (metadata->component_count > FIRMWARE_COMPONENT_COUNT) {
        print_debug_info("Metadata validation failed: Too many components (%d > %d)", 
                         metadata->component_count, FIRMWARE_COMPONENT_COUNT);
        return FIRMWARE_CORRUPTED;
    }
    
    print_debug_info("Metadata validation successful: Version=%s, Slot=%d, Components=%d", 
                     metadata->firmware_version, metadata->target_slot, metadata->component_count);
    
    return FIRMWARE_VALID;
}

/* ================================ 默认元数据初始化 ================================ */

static void init_default_metadata(FirmwareMetadata* metadata) {
    memset(metadata, 0, sizeof(FirmwareMetadata));
    
    // 设置安全字段
    metadata->magic = FIRMWARE_MAGIC;
    metadata->metadata_version_major = METADATA_VERSION_MAJOR;
    metadata->metadata_version_minor = METADATA_VERSION_MINOR;
    metadata->metadata_size = METADATA_STRUCT_SIZE;
    
    // 设置固件信息
    strncpy(metadata->firmware_version, "0.0.1", sizeof(metadata->firmware_version) - 1);
    metadata->target_slot = (uint8_t)SLOT_A;  // 转换FirmwareSlot到uint8_t
    strncpy(metadata->build_date, "2024-12-08 00:00:00", sizeof(metadata->build_date) - 1);
    metadata->build_timestamp = 0;
    
    // 设置设备兼容性
    strncpy(metadata->device_model, DEVICE_MODEL_STRING, sizeof(metadata->device_model) - 1);
    metadata->hardware_version = HARDWARE_VERSION;
    metadata->bootloader_min_version = BOOTLOADER_VERSION;
    
    // 设置默认组件
    metadata->component_count = 1;
    
    // Application组件
    strncpy(metadata->components[0].name, "application", sizeof(metadata->components[0].name) - 1);
    strncpy(metadata->components[0].file, "application_slot_a.hex", sizeof(metadata->components[0].file) - 1);
    metadata->components[0].address = SLOT_A_APPLICATION_ADDR;
    metadata->components[0].size = 1048576;
    metadata->components[0].active = true;
    
    // 计算CRC32校验和
    size_t crc_offset = offsetof(FirmwareMetadata, metadata_crc32);
    metadata->metadata_crc32 = calculate_crc32_skip_field((const uint8_t*)metadata, 
                                                       METADATA_STRUCT_SIZE, 
                                                       crc_offset, 
                                                       sizeof(uint32_t));
}

/* ================================ 调试输出函数 ================================ */

static void print_debug_info(const char* format, ...) {
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    // 直接输出调试信息
    printf("[BOOT DEBUG] %s\r\n", buffer);
}

/* ================================ 公共函数实现 ================================ */

int8_t DualSlot_LoadMetadata(FirmwareMetadata* metadata) {
    if (!metadata) {
        return -1;
    }
    
    // 退出内存映射模式
    bool was_mapped = QSPI_W25Qxx_IsMemoryMappedMode();
    if (was_mapped) {
        if (QSPI_W25Qxx_ExitMemoryMappedMode() != QSPI_W25Qxx_OK) {
            print_debug_info("Failed to exit memory mapped mode");
            return -2;
        }
    }
    
    // 从Flash读取元数据
    uint32_t flash_address = FIRMWARE_METADATA_BASE - EXTERNAL_FLASH_BASE;
    int8_t result = QSPI_W25Qxx_ReadBuffer(
        (uint8_t*)metadata, 
        flash_address,
        METADATA_STRUCT_SIZE
    );
    
    // 恢复内存映射模式
    if (was_mapped) {
        QSPI_W25Qxx_EnterMemoryMappedMode();
    }
    
    if (result != QSPI_W25Qxx_OK) {
        print_debug_info("Failed to read metadata from Flash: %d", result);
        return -3;
    }
    
    // 添加调试输出：显示读取到的原始数据
    print_debug_info("Raw metadata first 128 bytes:");
    uint8_t* raw_data = (uint8_t*)metadata;
    for (int i = 0; i < 128; i += 16) {
        printf("[BOOT DEBUG] %04X: ", i);
        for (int j = 0; j < 16 && (i + j) < 128; j++) {
            printf("%02X ", raw_data[i + j]);
        }
        printf("\r\n");
    }
    
    // 显示结构体字段偏移信息
    print_debug_info("=== C Structure Field Offsets ===");
    print_debug_info("magic: %d", offsetof(FirmwareMetadata, magic));
    print_debug_info("metadata_version_major: %d", offsetof(FirmwareMetadata, metadata_version_major));
    print_debug_info("metadata_version_minor: %d", offsetof(FirmwareMetadata, metadata_version_minor));
    print_debug_info("metadata_size: %d", offsetof(FirmwareMetadata, metadata_size));
    print_debug_info("metadata_crc32: %d", offsetof(FirmwareMetadata, metadata_crc32));
    print_debug_info("firmware_version: %d", offsetof(FirmwareMetadata, firmware_version));
    print_debug_info("target_slot: %d", offsetof(FirmwareMetadata, target_slot));
    print_debug_info("build_date: %d", offsetof(FirmwareMetadata, build_date));
    print_debug_info("build_timestamp: %d", offsetof(FirmwareMetadata, build_timestamp));
    print_debug_info("device_model: %d", offsetof(FirmwareMetadata, device_model));
    print_debug_info("hardware_version: %d", offsetof(FirmwareMetadata, hardware_version));
    print_debug_info("bootloader_min_version: %d", offsetof(FirmwareMetadata, bootloader_min_version));
    print_debug_info("component_count: %d", offsetof(FirmwareMetadata, component_count));
    print_debug_info("components: %d", offsetof(FirmwareMetadata, components));
    print_debug_info("Total structure size: %d", sizeof(FirmwareMetadata));
    print_debug_info("================================");
    
    // 特别检查设备型号字段的位置和内容
    size_t device_model_offset = offsetof(FirmwareMetadata, device_model);
    print_debug_info("Device model offset: %d", device_model_offset);
    print_debug_info("Device model raw bytes:");
    printf("[BOOT DEBUG] ");
    for (int i = 0; i < 32; i++) {
        printf("%02X ", raw_data[device_model_offset + i]);
    }
    printf("\r\n");
    
    print_debug_info("Device model as string: '%s'", metadata->device_model);
    print_debug_info("Expected device model: '%s'", DEVICE_MODEL_STRING);
    
    // 验证元数据完整性
    FirmwareValidationResult validation = validate_metadata(metadata);
    if (validation != FIRMWARE_VALID) {
        print_debug_info("Metadata validation failed: %d", validation);
        // 初始化默认元数据
        init_default_metadata(metadata);
        return -4;
    }
    
    // 保存到全局变量
    memcpy(&g_current_metadata, metadata, sizeof(FirmwareMetadata));
    g_metadata_loaded = true;
    
    print_debug_info("Metadata loaded successfully: Version=%s, Slot=%d", 
                     metadata->firmware_version, metadata->target_slot);
    
    return 0;
}

int8_t DualSlot_SaveMetadata(const FirmwareMetadata* metadata) {
    if (!metadata) {
        return -1;
    }
    
    // 验证元数据
    FirmwareValidationResult validation = validate_metadata(metadata);
    if (validation != FIRMWARE_VALID) {
        print_debug_info("Failed to save metadata: Validation failed (%d)", validation);
        return -2;
    }
    
    // 退出内存映射模式
    bool was_mapped = QSPI_W25Qxx_IsMemoryMappedMode();
    if (was_mapped) {
        if (QSPI_W25Qxx_ExitMemoryMappedMode() != QSPI_W25Qxx_OK) {
            return -3;
        }
    }
    
    // 擦除元数据扇区
    uint32_t flash_address = FIRMWARE_METADATA_BASE - EXTERNAL_FLASH_BASE;
    if (QSPI_W25Qxx_SectorErase(flash_address) != QSPI_W25Qxx_OK) {
        if (was_mapped) QSPI_W25Qxx_EnterMemoryMappedMode();
        return -4;
    }
    
    // 写入元数据到Flash
    int8_t result = QSPI_W25Qxx_WriteBuffer(
        (uint8_t*)metadata,
        flash_address,
        METADATA_STRUCT_SIZE
    );
    
    // 恢复内存映射模式
    if (was_mapped) {
        QSPI_W25Qxx_EnterMemoryMappedMode();
    }
    
    if (result == QSPI_W25Qxx_OK) {
        // 更新全局变量
        memcpy(&g_current_metadata, metadata, sizeof(FirmwareMetadata));
        g_metadata_loaded = true;
        print_debug_info("Metadata saved successfully");
    }
    
    return result;
}

FirmwareValidationResult DualSlot_ValidateMetadata(const FirmwareMetadata* metadata) {
    return validate_metadata(metadata);
}

FirmwareSlot DualSlot_GetActiveSlot(void) {
    if (!g_metadata_loaded) {
        // 尝试加载元数据
        if (DualSlot_LoadMetadata(&g_current_metadata) != 0) {
            return SLOT_A; // 默认返回槽A
        }
    }
    
    return (FirmwareSlot)g_current_metadata.target_slot;  // 从uint8_t转换为FirmwareSlot
}

int8_t DualSlot_SetActiveSlot(FirmwareSlot slot) {
    if (slot != SLOT_A && slot != SLOT_B) {
        return -1;
    }
    
    if (!g_metadata_loaded) {
        if (DualSlot_LoadMetadata(&g_current_metadata) != 0) {
            init_default_metadata(&g_current_metadata);
        }
    }
    
    g_current_metadata.target_slot = (uint8_t)slot;  // 从FirmwareSlot转换为uint8_t
    
    // 重新计算CRC32
    size_t crc_offset = offsetof(FirmwareMetadata, metadata_crc32);
    g_current_metadata.metadata_crc32 = calculate_crc32_skip_field((const uint8_t*)&g_current_metadata, 
                                                                  METADATA_STRUCT_SIZE, 
                                                                  crc_offset, 
                                                                  sizeof(uint32_t));
    
    return DualSlot_SaveMetadata(&g_current_metadata);
}

uint32_t DualSlot_GetSlotAddress(const char* component_name, FirmwareSlot slot) {
    if (!component_name) {
        return 0;
    }
    
    if (slot == SLOT_A) {
        if (strcmp(component_name, "application") == 0) {
            return SLOT_A_APPLICATION_ADDR;
        } else if (strcmp(component_name, "webresources") == 0) {
            return SLOT_A_WEBRESOURCES_ADDR;
        } else if (strcmp(component_name, "adc_mapping") == 0) {
            return SLOT_A_ADC_MAPPING_ADDR;
        }
    } else if (slot == SLOT_B) {
        if (strcmp(component_name, "application") == 0) {
            return SLOT_B_APPLICATION_ADDR;
        } else if (strcmp(component_name, "webresources") == 0) {
            return SLOT_B_WEBRESOURCES_ADDR;
        } else if (strcmp(component_name, "adc_mapping") == 0) {
            return SLOT_B_ADC_MAPPING_ADDR;
        }
    }
    
    return 0;
}

void DualSlot_JumpToApplication(FirmwareSlot slot) {
    uint32_t app_address = DualSlot_GetSlotAddress("application", slot);
    if (app_address == 0) {
        print_debug_info("Invalid application address for slot: %d", slot);
        return;
    }
    
    print_debug_info("Jumping to application: Slot=%d, Address=0x%08X", slot, app_address);
    
    // 检查应用程序是否存在（检查栈指针是否有效）
    uint32_t* app_vector = (uint32_t*)app_address;
    uint32_t stack_pointer = app_vector[0];
    uint32_t reset_handler = app_vector[1];
    
    // 简单的有效性检查
    if ((stack_pointer & 0xFFF00000) != 0x20000000 || // 栈指针应该在SRAM区域
        (reset_handler & 0xFF000000) != 0x90000000) {  // 复位向量应该在Flash区域
        print_debug_info("Invalid application: SP=0x%08X, PC=0x%08X", stack_pointer, reset_handler);
        return;
    }
    
    // 禁用中断
    __disable_irq();
    
    // 设置栈指针
    __set_MSP(stack_pointer);
    
    // 跳转到应用程序
    void (*app_reset_handler)(void) = (void (*)(void))reset_handler;
    app_reset_handler();
}

bool DualSlot_IsSlotValid(FirmwareSlot slot) {
    uint32_t app_address = DualSlot_GetSlotAddress("application", slot);
    if (app_address == 0) {
        return false;
    }
    
    // 检查应用程序向量表
    uint32_t* app_vector = (uint32_t*)app_address;
    uint32_t stack_pointer = app_vector[0];
    uint32_t reset_handler = app_vector[1];
    
    // 简单的有效性检查
    return ((stack_pointer & 0xFFF00000) == 0x20000000) && 
           ((reset_handler & 0xFF000000) == 0x90000000);
}

void DualSlot_PrintMetadata(const FirmwareMetadata* metadata) {
    if (!metadata) {
        print_debug_info("Metadata is null");
        return;
    }
    
    print_debug_info("=== Firmware Metadata Information ===");
    print_debug_info("Magic Number: 0x%08X", metadata->magic);
    print_debug_info("Metadata Version: %d.%d", metadata->metadata_version_major, metadata->metadata_version_minor);
    print_debug_info("Firmware Version: %s", metadata->firmware_version);
    print_debug_info("Target Slot: %d", metadata->target_slot);
    print_debug_info("Build Date: %s", metadata->build_date);
    print_debug_info("Device Model: %s", metadata->device_model);
    print_debug_info("Hardware Version: 0x%08X", metadata->hardware_version);
    print_debug_info("Component Count: %d", metadata->component_count);
    print_debug_info("CRC32: 0x%08X", metadata->metadata_crc32);
    
    for (uint32_t i = 0; i < metadata->component_count && i < FIRMWARE_COMPONENT_COUNT; i++) {
        const FirmwareComponent* comp = &metadata->components[i];
        print_debug_info("Component[%d]: %s, Address=0x%08X, Size=%d, Active=%d", 
                         i, comp->name, comp->address, comp->size, comp->active);
    }
    print_debug_info("=====================================");
} 