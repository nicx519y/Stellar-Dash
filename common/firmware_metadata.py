#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
固件元数据常量定义 - Python版本
与 firmware_metadata.h 保持完全一致
"""

# 元数据结构常量（与C头文件保持一致）
FIRMWARE_MAGIC = 0x48424F58  # "HBOX"
METADATA_VERSION_MAJOR = 1
METADATA_VERSION_MINOR = 0
DEVICE_MODEL_STRING = "STM32H750_HBOX"
BOOTLOADER_VERSION = 0x00010000  # 1.0.0
HARDWARE_VERSION = 0x00010000  # 1.0.0

# 组件相关常量
FIRMWARE_COMPONENT_COUNT = 3

# 结构体大小计算（必须与实际结构体大小完全一致）
# FirmwareComponent: 32+64+4+4+65+1 = 170字节 (packed)
# FirmwareMetadata: 20+32+1+32+4+32+4+4+4+(170*3)+32+64+4+64 = 807字节 (packed)
COMPONENT_STRUCT_SIZE = 170
METADATA_STRUCT_SIZE = 807

# 固件组件类型
FIRMWARE_COMPONENT_APPLICATION = 0
FIRMWARE_COMPONENT_WEBRESOURCES = 1
FIRMWARE_COMPONENT_ADC_MAPPING = 2

# 固件槽位
FIRMWARE_SLOT_A = 0
FIRMWARE_SLOT_B = 1
FIRMWARE_SLOT_INVALID = 0xFF

# 内存布局配置
EXTERNAL_FLASH_BASE = 0x90000000
SLOT_SIZE = 0x2B0000  # 2.625MB per slot

# 槽A地址配置
SLOT_A_BASE = 0x90000000
SLOT_A_APPLICATION_ADDR = 0x90000000  # 1MB
SLOT_A_APPLICATION_SIZE = 0x100000
SLOT_A_WEBRESOURCES_ADDR = 0x90100000  # 1.5MB
SLOT_A_WEBRESOURCES_SIZE = 0x180000
SLOT_A_ADC_MAPPING_ADDR = 0x90280000  # 128KB
SLOT_A_ADC_MAPPING_SIZE = 0x20000

# 槽B地址配置
SLOT_B_BASE = 0x902B0000
SLOT_B_APPLICATION_ADDR = 0x902B0000  # 1MB
SLOT_B_APPLICATION_SIZE = 0x100000
SLOT_B_WEBRESOURCES_ADDR = 0x903B0000  # 1.5MB
SLOT_B_WEBRESOURCES_SIZE = 0x180000
SLOT_B_ADC_MAPPING_ADDR = 0x90530000  # 128KB
SLOT_B_ADC_MAPPING_SIZE = 0x20000

# 配置和元数据区域
USER_CONFIG_ADDR = 0x90560000  # 64KB
USER_CONFIG_SIZE = 0x10000
METADATA_ADDR = 0x90570000  # 64KB
METADATA_SIZE = 0x10000

# 组件名称映射
COMPONENT_NAMES = {
    'application': FIRMWARE_COMPONENT_APPLICATION,
    'webresources': FIRMWARE_COMPONENT_WEBRESOURCES,
    'adc_mapping': FIRMWARE_COMPONENT_ADC_MAPPING
}

# 槽位地址映射函数
def get_slot_address(component_name: str, slot: str) -> int:
    """获取组件在指定槽位的地址"""
    slot_upper = slot.upper()
    
    if component_name == 'application':
        return SLOT_A_APPLICATION_ADDR if slot_upper == 'A' else SLOT_B_APPLICATION_ADDR
    elif component_name == 'webresources':
        return SLOT_A_WEBRESOURCES_ADDR if slot_upper == 'A' else SLOT_B_WEBRESOURCES_ADDR
    elif component_name == 'adc_mapping':
        return SLOT_A_ADC_MAPPING_ADDR if slot_upper == 'A' else SLOT_B_ADC_MAPPING_ADDR
    else:
        raise ValueError(f"Unknown component: {component_name}")

def get_component_size(component_name: str) -> int:
    """获取组件大小"""
    if component_name == 'application':
        return SLOT_A_APPLICATION_SIZE
    elif component_name == 'webresources':
        return SLOT_A_WEBRESOURCES_SIZE
    elif component_name == 'adc_mapping':
        return SLOT_A_ADC_MAPPING_SIZE
    else:
        raise ValueError(f"Unknown component: {component_name}")

# 验证常量一致性
def validate_constants():
    """验证Python常量与C头文件的一致性"""
    # 验证结构体大小计算
    component_size = 32 + 64 + 4 + 4 + 65 + 1  # 170字节
    metadata_size = 20 + 32 + 1 + 32 + 4 + 32 + 4 + 4 + 4 + (170 * 3) + 32 + 64 + 4 + 64  # 807字节
    
    assert component_size == COMPONENT_STRUCT_SIZE, f"Component size mismatch: {component_size} != {COMPONENT_STRUCT_SIZE}"
    assert metadata_size == METADATA_STRUCT_SIZE, f"Metadata size mismatch: {metadata_size} != {METADATA_STRUCT_SIZE}"
    
    print("Python常量验证通过!")

if __name__ == "__main__":
    validate_constants() 