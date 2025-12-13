#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
STM32 HBox ADC Mapping 数据提取工具

从STM32设备Flash中提取ADC通道映射数据
"""

import struct
import os
import sys
import subprocess
import json
from typing import List, Tuple, NamedTuple, Optional
from pathlib import Path

# 常量定义
SLOT_A_ADC_MAPPING_BASE = 0x90280000  # 槽A ADC Mapping 虚拟地址
SLOT_A_ADC_MAPPING_SIZE = 128 * 1024  # 128KB

NUM_ADC_VALUES_MAPPING = 8  # 最大8个映射
MAX_ADC_VALUES_LENGTH = 40  # 每个映射最大40个值
NUM_ADC_BUTTONS = 18  # 总按钮数 (6+6+6)

class ADCCalibrationValues(NamedTuple):
    """ADC校准值"""
    top_value: int      # 完全按下时的值
    bottom_value: int   # 完全释放时的值

class ADCValuesMapping(NamedTuple):
    """ADC值映射结构"""
    id: str                                     # 映射ID (16字节)
    name: str                                   # 映射名称 (16字节)  
    length: int                                 # 映射长度 (size_t - 在STM32H7上通常是4字节)
    step: float                                 # 步长 (float - 4字节)
    sampling_noise: int                         # 噪声阈值 (uint16_t - 2字节)
    sampling_frequency: int                     # 采样频率 (uint16_t - 2字节)
    original_values: List[int]                  # 采集原始值 (40个uint32_t)
    auto_calibration_values: List[ADCCalibrationValues]    # 自动校准值 (17个按钮)
    manual_calibration_values: List[ADCCalibrationValues]  # 手动校准值 (17个按钮)

class ADCValuesMappingStore(NamedTuple):
    """ADC值映射存储结构"""
    version: int                                # 版本号 (uint32_t - 4字节)
    num: int                                    # 映射数量 (uint8_t - 1字节)
    default_id: str                             # 默认映射ID (16字节)
    mappings: List[ADCValuesMapping]            # 映射数组 (最多8个)

class ADCMappingExtractor:
    """ADC Mapping数据提取器"""
    
    def __init__(self):
        self.workspace_root = Path(__file__).parent.parent
        
    def extract_from_device(self) -> bytes:
        """从设备提取ADC Mapping数据"""
        try:
            print("从设备Flash读取ADC Mapping数据...")
            print(f"地址: 0x{SLOT_A_ADC_MAPPING_BASE:08X}")
            print(f"大小: {SLOT_A_ADC_MAPPING_SIZE} 字节")
            
            # 使用简短的文件名
            temp_file = "adc_mapping.bin"
            
            # OpenOCD命令 - 使用项目的QSPI配置
            cmd = [
                "openocd",
                "-d0",
                "-f", "openocd_configs/ST-LINK-QSPIFLASH.cfg",
                "-c", "init",
                "-c", "halt",
                "-c", "reset init",
                "-c", f"dump_image {temp_file} 0x{SLOT_A_ADC_MAPPING_BASE:08X} {SLOT_A_ADC_MAPPING_SIZE}",
                "-c", "shutdown"
            ]
            
            print(f"执行命令: {' '.join(cmd)}")
            result = subprocess.run(
                cmd,
                capture_output=True,
                text=True,
                timeout=30,
            )
            
            if result.returncode == 0:
                # 检查文件是否生成
                temp_path = Path(temp_file)
                if temp_path.exists():
                    with open(temp_path, 'rb') as f:
                        data = f.read()
                    temp_path.unlink()  # 删除临时文件
                    print(f"成功读取 {len(data)} 字节数据")
                    return data
                else:
                    print("错误: OpenOCD未生成输出文件")
                    return None
            else:
                print(f"OpenOCD执行失败:")
                print(f"返回码: {result.returncode}")
                if result.stdout:
                    print(f"输出: {result.stdout}")
                if result.stderr:
                    print(f"错误: {result.stderr}")
                return None
                
        except subprocess.TimeoutExpired:
            print("错误: OpenOCD执行超时")
            return None
        except FileNotFoundError:
            print("错误: 未找到OpenOCD工具，请确保已安装并在PATH中")
            return None
        except Exception as e:
            print(f"执行异常: {e}")
            return None
    
    def parse_adc_mapping_data(self, raw_data: bytes) -> ADCValuesMappingStore:
        """解析ADC Mapping数据"""
        if len(raw_data) < 24:  # 最小长度
            raise ValueError("数据长度不足")
            
        offset = 0
        
        # 读取版本号 (4字节)
        version = struct.unpack('<I', raw_data[offset:offset+4])[0]
        offset += 4
        
        # 读取映射数量 (1字节)
        num = struct.unpack('<B', raw_data[offset:offset+1])[0]
        offset += 1
        
        # ARM编译器4字节对齐，所以在uint8_t后面会有3字节的填充
        offset += 3  # 跳过填充字节
        
        # 读取默认ID (16字节)
        default_id_bytes = raw_data[offset:offset+16]
        default_id = default_id_bytes.decode('utf-8', errors='ignore').strip('\x00')
        offset += 16
        
        print(f"版本: 0x{version:08X}, 映射数量: {num}, 默认ID: '{default_id}'")
        
        # 验证数据合理性
        if num > NUM_ADC_VALUES_MAPPING:
            print(f"警告: 映射数量 {num} 超过最大值，调整为 {NUM_ADC_VALUES_MAPPING}")
            num = NUM_ADC_VALUES_MAPPING
        
        # 读取映射数组
        mappings = []
        for i in range(num):
            try:
                mapping = self._parse_single_mapping(raw_data, offset)
                if mapping:
                    mappings.append(mapping)
                    print(f"解析映射 {i+1}: '{mapping.name}' (ID: '{mapping.id}')")
                offset += self._get_mapping_size()
            except Exception as e:
                print(f"解析映射 {i+1} 失败: {e}")
                break
            
        return ADCValuesMappingStore(
            version=version,
            num=len(mappings),
            default_id=default_id,
            mappings=mappings
        )
    
    def _parse_single_mapping(self, data: bytes, offset: int) -> ADCValuesMapping:
        """解析单个映射"""
        start_offset = offset
        
        # 读取ID (16字节)
        id_bytes = data[offset:offset+16]
        id_str = id_bytes.decode('utf-8', errors='ignore').strip('\x00')
        offset += 16
        
        # 读取名称 (16字节)  
        name_bytes = data[offset:offset+16]
        name = name_bytes.decode('utf-8', errors='ignore').strip('\x00')
        offset += 16
        
        # 读取长度 (4字节)
        length = struct.unpack('<I', data[offset:offset+4])[0]
        offset += 4
        
        # 读取步长 (4字节)
        step = struct.unpack('<f', data[offset:offset+4])[0]
        offset += 4
        
        # 读取采样噪声 (2字节)
        sampling_noise = struct.unpack('<H', data[offset:offset+2])[0]
        offset += 2
        
        # 读取采样频率 (2字节)
        sampling_frequency = struct.unpack('<H', data[offset:offset+2])[0]
        offset += 2
        
        # 验证长度
        if length > MAX_ADC_VALUES_LENGTH:
            length = MAX_ADC_VALUES_LENGTH
            
        # 读取原始值数组 (40个uint32_t)
        original_values = []
        for i in range(MAX_ADC_VALUES_LENGTH):
            value = struct.unpack('<I', data[offset:offset+4])[0]
            original_values.append(value)
            offset += 4
            
        # 读取自动校准值 (17个按钮)
        auto_calibration_values = []
        for i in range(NUM_ADC_BUTTONS):
            top = struct.unpack('<H', data[offset:offset+2])[0]
            offset += 2
            bottom = struct.unpack('<H', data[offset:offset+2])[0]
            offset += 2
            auto_calibration_values.append(ADCCalibrationValues(top, bottom))
            
        # 读取手动校准值 (17个按钮)
        manual_calibration_values = []
        for i in range(NUM_ADC_BUTTONS):
            top = struct.unpack('<H', data[offset:offset+2])[0]
            offset += 2
            bottom = struct.unpack('<H', data[offset:offset+2])[0]
            offset += 2
            manual_calibration_values.append(ADCCalibrationValues(top, bottom))
            
        return ADCValuesMapping(
            id=id_str,
            name=name,
            length=length,
            step=step,
            sampling_noise=sampling_noise,
            sampling_frequency=sampling_frequency,
            original_values=original_values,
            auto_calibration_values=auto_calibration_values,
            manual_calibration_values=manual_calibration_values
        )
    
    def _get_mapping_size(self) -> int:
        """获取单个映射的大小"""
        base_size = (
            16 +  # id[16]
            16 +  # name[16]
            4  +  # length (size_t, 4 bytes)
            4  +  # step (float, 4 bytes)
            2  +  # samplingNoise (uint16_t)
            2  +  # samplingFrequency (uint16_t)
            (40 * 4) +  # originalValues (40 * uint32_t)
            (NUM_ADC_BUTTONS * 2 * 2) +  # autoCalibrationValues (top,bottom)
            (NUM_ADC_BUTTONS * 2 * 2)    # manualCalibrationValues (top,bottom)
        )
        # ARM编译器4字节对齐
        aligned_size = ((base_size + 3) // 4) * 4
        return aligned_size
    
    def save_binary_data(self, data: bytes, filename: str = "slot_a_adc_mapping.bin"):
        """保存原始二进制数据（清空校准值）"""
        # 保存到resources目录
        resources_dir = self.workspace_root / "resources"
        resources_dir.mkdir(exist_ok=True)  # 确保resources目录存在
        output_path = resources_dir / filename
        
        try:
            # 解析数据以获取结构信息
            store = self.parse_adc_mapping_data(data)
            
            # 创建新的二进制数据，清空校准值
            new_data = bytearray()
            
            # 写入头部信息
            new_data.extend(struct.pack('<I', store.version))  # 版本号
            new_data.extend(struct.pack('<B', store.num))      # 映射数量
            new_data.extend(b'\x00\x00\x00')                  # 3字节填充
            new_data.extend(store.default_id.encode('utf-8').ljust(16, b'\x00'))  # 默认ID
            
            # 写入每个映射
            for mapping in store.mappings:
                # ID (16字节)
                new_data.extend(mapping.id.encode('utf-8').ljust(16, b'\x00'))
                
                # 名称 (16字节)
                new_data.extend(mapping.name.encode('utf-8').ljust(16, b'\x00'))
                
                # 长度 (4字节)
                new_data.extend(struct.pack('<I', mapping.length))
                
                # 步长 (4字节)
                new_data.extend(struct.pack('<f', mapping.step))
                
                # 采样噪声 (2字节)
                new_data.extend(struct.pack('<H', mapping.sampling_noise))
                
                # 采样频率 (2字节)
                new_data.extend(struct.pack('<H', mapping.sampling_frequency))
                
                # 原始值数组 (40个uint32_t)
                for value in mapping.original_values:
                    new_data.extend(struct.pack('<I', value))
                
                # 自动校准值 (17个按钮) - 清空为0
                for _ in range(NUM_ADC_BUTTONS):
                    new_data.extend(struct.pack('<HH', 0, 0))  # top=0, bottom=0
                
                # 手动校准值 (17个按钮) - 清空为0
                for _ in range(NUM_ADC_BUTTONS):
                    new_data.extend(struct.pack('<HH', 0, 0))  # top=0, bottom=0
            
            # 写入文件
            with open(output_path, 'wb') as f:
                f.write(new_data)
            print(f"ADC Mapping已保存到: {output_path}")
            print(f"文件大小: {len(new_data)} 字节")
            print("注意: 校准值已被清空")
            return True
        except Exception as e:
            print(f"保存文件失败: {e}")
            return False
    
    def print_mapping_info(self, store: ADCValuesMappingStore):
        """打印映射信息"""
        print(f"\n=== ADC Mapping 信息 ===")
        print(f"版本: 0x{store.version:08X}")
        print(f"映射数量: {store.num}")
        print(f"默认映射ID: {store.default_id}")
        
        for i, mapping in enumerate(store.mappings):
            print(f"\n--- 映射 {i+1} ---")
            print(f"ID: '{mapping.id}'")
            print(f"名称: '{mapping.name}'")
            print(f"长度: {mapping.length}")
            print(f"步长: {mapping.step}")
            print(f"采样噪声: {mapping.sampling_noise}")
            print(f"采样频率: {mapping.sampling_frequency}")
            
            # 显示有效的原始值
            if mapping.original_values and mapping.length > 0:
                valid_values = []
                for j in range(min(mapping.length, len(mapping.original_values))):
                    value = mapping.original_values[j]
                    if 0 <= value <= 65535:
                        valid_values.append(value)
                
                if valid_values:
                    print(f"有效原始值: {valid_values[:5]}...")
                    print(f"值范围: {min(valid_values)} - {max(valid_values)}")
                else:
                    print("原始值: 无有效数据")
    
    def add_hex_dump_info(self, raw_data: bytes, max_lines: int = 10):
        """显示原始数据的十六进制转储"""
        print(f"\n=== 原始数据十六进制转储 (前{max_lines*16}字节) ===")
        for i in range(min(max_lines, len(raw_data) // 16)):
            offset = i * 16
            hex_bytes = ' '.join(f'{b:02X}' for b in raw_data[offset:offset+16])
            ascii_chars = ''.join(chr(b) if 32 <= b <= 126 else '.' for b in raw_data[offset:offset+16])
            print(f"{offset:08X}: {hex_bytes:<48} |{ascii_chars}|")

    def save_json_data(self, store: ADCValuesMappingStore, filename: str = "slot_a_adc_mapping.json"):
        """保存解析后的ADC mapping数据为JSON格式"""
        # 保存到resources目录
        resources_dir = self.workspace_root / "resources"
        resources_dir.mkdir(exist_ok=True)  # 确保resources目录存在
        output_path = resources_dir / filename
        
        try:
            data = {
                "version": store.version,
                "num": store.num,
                "default_id": store.default_id,
                "mappings": [
                    {
                        "id": mapping.id,
                        "name": mapping.name,
                        "length": mapping.length,
                        "step": mapping.step,
                        "sampling_noise": mapping.sampling_noise,
                        "sampling_frequency": mapping.sampling_frequency,
                        "original_values": mapping.original_values,
                        "auto_calibration_values": [],  # 清空自动校准值
                        "manual_calibration_values": []  # 清空手动校准值
                    }
                    for mapping in store.mappings
                ]
            }
            with open(output_path, 'w', encoding='utf-8') as f:
                json.dump(data, f, indent=2, ensure_ascii=False)
            print(f"ADC Mapping已保存到: {output_path}")
            return True
        except Exception as e:
            print(f"保存文件失败: {e}")
            return False

def main():
    """主函数"""
    extractor = ADCMappingExtractor()
    
    print("=== STM32 HBox ADC Mapping 数据提取工具 ===")
    print("从设备Flash直接读取ADC mapping数据")
    print()
    
    # 从设备提取数据
    raw_data = extractor.extract_from_device()
    
    if raw_data is None:
        print("\n[ERROR] 无法从设备读取数据!")
        print("请检查:")
        print("1. 设备是否正确连接")
        print("2. ST-Link驱动是否正常")
        print("3. OpenOCD是否能正常连接设备")
        sys.exit(1)
    
    # 保存原始数据
    success = extractor.save_binary_data(raw_data)
    if not success:
        sys.exit(1)
    
    # 显示原始数据
    extractor.add_hex_dump_info(raw_data)
    
    # 解析并显示数据
    try:
        store = extractor.parse_adc_mapping_data(raw_data)
        extractor.print_mapping_info(store)
        
        print(f"\n[OK] ADC Mapping数据提取完成!")
        print(f"输出文件: {extractor.workspace_root}/resources/slot_a_adc_mapping.bin")
        
        # 保存JSON数据
        success = extractor.save_json_data(store)
        if not success:
            sys.exit(1)
        
    except Exception as e:
        print(f"\n解析数据失败: {e}")
        print("原始二进制数据已保存，但无法解析结构")
        print("这可能是由于数据结构不匹配或数据未初始化")

if __name__ == "__main__":
    main() 
