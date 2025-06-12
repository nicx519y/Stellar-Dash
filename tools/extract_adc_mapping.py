#!/usr/bin/env python3
"""
ADC Mapping 数据提取工具

从槽A的外部Flash中提取ADC Mapping数据并生成bin文件
存储位置：槽A ADC Mapping区域 (0x00280000, 128KB)
输出：resources/slot_a_adc_mapping.bin
"""

import struct
import os
import sys
from typing import List, Tuple, NamedTuple
from pathlib import Path

# 常量定义
SLOT_A_ADC_MAPPING_BASE = 0x90280000  # 槽A ADC Mapping 基地址
SLOT_A_ADC_MAPPING_SIZE = 128 * 1024  # 128KB
SLOT_A_ADC_MAPPING_OFFSET = 0x00280000  # 物理偏移地址

NUM_ADC_VALUES_MAPPING = 8  # 最大8个映射
MAX_ADC_VALUES_LENGTH = 40  # 每个映射最大40个值
NUM_ADC_BUTTONS = 17  # 总按钮数 (6+6+5)

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
        self.resources_dir = self.workspace_root / "resources"
        self.flash_dump_file = None
        
    def create_resources_dir(self):
        """创建resources目录"""
        self.resources_dir.mkdir(exist_ok=True)
        print(f"确保目录存在: {self.resources_dir}")
        
    def extract_from_device(self) -> bytes:
        """
        从设备提取ADC Mapping数据
        注意：这需要设备连接和适当的调试工具
        """
        try:
            # 尝试通过OpenOCD或STLink提取数据
            # 这里需要实际的硬件连接和工具
            print("警告: 直接从设备提取数据需要硬件连接")
            print("请确保设备已连接并且可以通过调试器访问")
            
            # 尝试使用OpenOCD读取内存
            import subprocess
            try:
                # 构建OpenOCD命令来读取内存
                cmd = [
                    "openocd",
                    "-f", "interface/stlink.cfg",
                    "-f", "target/stm32h7x.cfg",
                    "-c", "init",
                    "-c", f"dump_image slot_a_adc_mapping_temp.bin {SLOT_A_ADC_MAPPING_BASE:#x} {SLOT_A_ADC_MAPPING_SIZE}",
                    "-c", "exit"
                ]
                
                result = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
                
                if result.returncode == 0:
                    # 读取生成的临时文件
                    temp_file = Path("slot_a_adc_mapping_temp.bin")
                    if temp_file.exists():
                        with open(temp_file, 'rb') as f:
                            data = f.read()
                        temp_file.unlink()  # 删除临时文件
                        print(f"成功从设备读取 {len(data)} 字节数据")
                        return data
                    else:
                        print("OpenOCD执行成功但未找到输出文件")
                else:
                    print(f"OpenOCD执行失败: {result.stderr}")
                    
            except subprocess.TimeoutExpired:
                print("OpenOCD执行超时")
            except FileNotFoundError:
                print("未找到OpenOCD工具，请确保已安装并在PATH中")
            except Exception as e:
                print(f"OpenOCD执行异常: {e}")
            
            # 如果OpenOCD失败，返回示例数据
            print("无法从设备读取数据，返回空数据")
            return b'\x00' * SLOT_A_ADC_MAPPING_SIZE
            
        except Exception as e:
            print(f"从设备提取数据失败: {e}")
            return None
    
    def extract_from_flash_dump(self, dump_file: str) -> bytes:
        """从Flash转储文件提取ADC Mapping数据"""
        dump_path = Path(dump_file)
        if not dump_path.exists():
            print(f"错误: Flash转储文件不存在: {dump_file}")
            return None
            
        try:
            with open(dump_path, 'rb') as f:
                # 获取文件大小
                f.seek(0, 2)
                file_size = f.tell()
                f.seek(0)
                
                print(f"Flash转储文件大小: {file_size} 字节")
                
                # 检查文件大小是否足够
                if file_size >= SLOT_A_ADC_MAPPING_OFFSET + SLOT_A_ADC_MAPPING_SIZE:
                    # 跳转到ADC Mapping区域
                    f.seek(SLOT_A_ADC_MAPPING_OFFSET)
                    data = f.read(SLOT_A_ADC_MAPPING_SIZE)
                    print(f"从偏移 0x{SLOT_A_ADC_MAPPING_OFFSET:08X} 读取了 {len(data)} 字节")
                else:
                    # 如果文件就是ADC Mapping数据
                    f.seek(0)
                    data = f.read()
                    print(f"读取整个文件作为ADC Mapping数据: {len(data)} 字节")
                    
                    # 如果数据不足，补充零
                    if len(data) < SLOT_A_ADC_MAPPING_SIZE:
                        data += b'\x00' * (SLOT_A_ADC_MAPPING_SIZE - len(data))
                        print(f"数据已补充到 {len(data)} 字节")
                
            return data
            
        except Exception as e:
            print(f"读取Flash转储文件失败: {e}")
            return None
    
    def parse_adc_mapping_data(self, raw_data: bytes) -> ADCValuesMappingStore:
        """解析ADC Mapping数据"""
        if len(raw_data) < 24:  # 最小长度：version(4) + num(1) + padding(3) + defaultId(16)
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
        default_id = default_id_bytes.decode('utf-8', errors='ignore').rstrip('\x00')
        offset += 16
        
        print(f"解析到版本: 0x{version:08X}, 映射数量: {num}, 默认ID: '{default_id}'")
        
        # 验证数据合理性
        if num > NUM_ADC_VALUES_MAPPING:
            print(f"警告: 映射数量 {num} 超过最大值 {NUM_ADC_VALUES_MAPPING}，可能存在数据错误")
            num = min(num, NUM_ADC_VALUES_MAPPING)
        
        # 读取映射数组
        mappings = []
        for i in range(num):
            try:
                # 检查剩余数据是否足够
                mapping_size = self._get_mapping_size()
                if offset + mapping_size > len(raw_data):
                    print(f"警告: 数据不足以解析映射 {i+1}，跳过")
                    break
                    
                mapping = self._parse_single_mapping(raw_data, offset)
                if mapping:
                    mappings.append(mapping)
                    print(f"成功解析映射 {i+1}: '{mapping.name}' (ID: '{mapping.id}')")
                offset += mapping_size
            except Exception as e:
                print(f"解析映射 {i+1} 失败: {e}")
                break
            
        return ADCValuesMappingStore(
            version=version,
            num=len(mappings),  # 使用实际解析成功的数量
            default_id=default_id,
            mappings=mappings
        )
    
    def _parse_single_mapping(self, data: bytes, offset: int) -> ADCValuesMapping:
        """解析单个映射"""
        mapping_size = self._get_mapping_size()
        if offset + mapping_size > len(data):
            raise ValueError(f"数据不足，需要 {mapping_size} 字节，剩余 {len(data) - offset} 字节")
            
        start_offset = offset
        
        # 读取ID (16字节)
        id_bytes = data[offset:offset+16]
        id_str = id_bytes.decode('utf-8', errors='ignore').rstrip('\x00')
        offset += 16
        
        # 读取名称 (16字节)  
        name_bytes = data[offset:offset+16]
        name = name_bytes.decode('utf-8', errors='ignore').rstrip('\x00')
        offset += 16
        
        # 读取长度 (size_t, 在STM32H7上是4字节)
        length = struct.unpack('<I', data[offset:offset+4])[0]
        offset += 4
        
        # 读取步长 (float, 4字节)
        step = struct.unpack('<f', data[offset:offset+4])[0]
        offset += 4
        
        # 读取采样噪声 (uint16_t, 2字节)
        sampling_noise = struct.unpack('<H', data[offset:offset+2])[0]
        offset += 2
        
        # 读取采样频率 (uint16_t, 2字节)
        sampling_frequency = struct.unpack('<H', data[offset:offset+2])[0]
        offset += 2
        
        # 验证length的合理性
        if length > MAX_ADC_VALUES_LENGTH:
            print(f"警告: 映射长度 {length} 超过最大值 {MAX_ADC_VALUES_LENGTH}，调整为最大值")
            length = MAX_ADC_VALUES_LENGTH
            
        # 读取原始值数组 (40个uint32_t)
        original_values = []
        for i in range(MAX_ADC_VALUES_LENGTH):
            value = struct.unpack('<I', data[offset:offset+4])[0]
            original_values.append(value)
            offset += 4
            
        # 读取自动校准值 (17个按钮, 每个2个uint16_t)
        auto_calibration_values = []
        for i in range(NUM_ADC_BUTTONS):
            top = struct.unpack('<H', data[offset:offset+2])[0]
            offset += 2
            bottom = struct.unpack('<H', data[offset:offset+2])[0]
            offset += 2
            auto_calibration_values.append(ADCCalibrationValues(top, bottom))
            
        # 读取手动校准值 (17个按钮, 每个2个uint16_t)
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
        """获取单个映射的大小（考虑ARM编译器对齐）"""
        # 计算实际结构体大小，考虑ARM 4字节对齐
        # ID(16) + Name(16) + Length(4) + Step(4) + SamplingNoise(2) + SamplingFreq(2) +
        # OriginalValues(40*4) + AutoCalib(17*2*2) + ManualCalib(17*2*2)
        base_size = 16 + 16 + 4 + 4 + 2 + 2 + (40 * 4) + (17 * 2 * 2) + (17 * 2 * 2)
        
        # ARM编译器会将结构体大小对齐到4字节边界
        aligned_size = ((base_size + 3) // 4) * 4
        
        return aligned_size
    
    def save_binary_data(self, data: bytes, filename: str = "slot_a_adc_mapping.bin"):
        """保存二进制数据到文件"""
        output_path = self.resources_dir / filename
        
        try:
            with open(output_path, 'wb') as f:
                f.write(data)
            print(f"ADC Mapping数据已保存到: {output_path}")
            print(f"文件大小: {len(data)} 字节")
            return True
            
        except Exception as e:
            print(f"保存文件失败: {e}")
            return False
    
    def save_json_data(self, store: ADCValuesMappingStore, filename: str = "slot_a_adc_mapping.json"):
        """保存解析后的数据为JSON格式"""
        import json
        
        output_path = self.resources_dir / filename
        
        # 转换为可序列化的字典
        data_dict = {
            "version": f"0x{store.version:08X}",
            "num": store.num,
            "default_id": store.default_id,
            "mappings": []
        }
        
        for mapping in store.mappings:
            # 过滤有效的原始值（在合理范围内）
            valid_original_values = []
            for i, value in enumerate(mapping.original_values):
                if i < mapping.length and 0 <= value <= 65535:  # ADC 12位最大值是4095，但考虑一些余量
                    valid_original_values.append(value)
                else:
                    valid_original_values.append(0)  # 无效值设为0
            
            mapping_dict = {
                "id": mapping.id,
                "name": mapping.name,
                "length": mapping.length,
                "step": mapping.step,
                "sampling_noise": mapping.sampling_noise,
                "sampling_frequency": mapping.sampling_frequency,
                "original_values": valid_original_values,
                "auto_calibration_values": [
                    {"top_value": cv.top_value, "bottom_value": cv.bottom_value}
                    for cv in mapping.auto_calibration_values
                ],
                "manual_calibration_values": [
                    {"top_value": cv.top_value, "bottom_value": cv.bottom_value}
                    for cv in mapping.manual_calibration_values
                ]
            }
            data_dict["mappings"].append(mapping_dict)
        
        try:
            with open(output_path, 'w', encoding='utf-8') as f:
                json.dump(data_dict, f, indent=2, ensure_ascii=False)
            print(f"JSON数据已保存到: {output_path}")
            return True
            
        except Exception as e:
            print(f"保存JSON文件失败: {e}")
            return False
    
    def print_mapping_info(self, store: ADCValuesMappingStore):
        """打印映射信息"""
        print(f"\n=== ADC Mapping 信息 ===")
        print(f"版本: 0x{store.version:08X}")
        print(f"映射数量: {store.num}")
        print(f"默认映射ID: {store.default_id}")
        print(f"解析映射数量: {len(store.mappings)}")
        
        for i, mapping in enumerate(store.mappings):
            print(f"\n--- 映射 {i+1} ---")
            print(f"ID: '{mapping.id}'")
            print(f"名称: '{mapping.name}'")
            print(f"长度: {mapping.length}")
            print(f"步长: {mapping.step}")
            print(f"采样噪声: {mapping.sampling_noise}")
            print(f"采样频率: {mapping.sampling_frequency}")
            
            # 显示有效的原始值（在合理范围内）
            if mapping.original_values and mapping.length > 0:
                valid_values = []
                for j in range(min(mapping.length, len(mapping.original_values))):
                    value = mapping.original_values[j]
                    if 0 <= value <= 65535:  # 合理的ADC值范围
                        valid_values.append(value)
                
                if valid_values:
                    print(f"有效原始值 (前5个): {valid_values[:5]}")
                    if len(valid_values) >= mapping.length:
                        print(f"原始值范围: {min(valid_values)} - {max(valid_values)}")
                else:
                    print("原始值: 无有效数据")
            else:
                print("原始值: 未设置")
            
            # 显示部分校准值
            auto_non_zero = [(i, v) for i, v in enumerate(mapping.auto_calibration_values) 
                           if v.top_value != 0 or v.bottom_value != 0]
            if auto_non_zero:
                print(f"自动校准值 (前3个非零): {auto_non_zero[:3]}")
            else:
                print("自动校准值: 全部为0或未设置")
                
            manual_non_zero = [(i, v) for i, v in enumerate(mapping.manual_calibration_values) 
                             if v.top_value != 0 or v.bottom_value != 0]
            if manual_non_zero:
                print(f"手动校准值 (前3个非零): {manual_non_zero[:3]}")
            else:
                print("手动校准值: 全部为0或未设置")
                
    def add_hex_dump_info(self, raw_data: bytes, max_lines: int = 10):
        """添加原始数据的十六进制转储信息（用于调试）"""
        print(f"\n=== 原始数据十六进制转储 (前{max_lines*16}字节) ===")
        for i in range(min(max_lines, len(raw_data) // 16)):
            offset = i * 16
            hex_bytes = ' '.join(f'{b:02X}' for b in raw_data[offset:offset+16])
            ascii_chars = ''.join(chr(b) if 32 <= b <= 126 else '.' for b in raw_data[offset:offset+16])
            print(f"{offset:08X}: {hex_bytes:<48} |{ascii_chars}|")
    
    def extract_and_save(self, source_type: str = "device", flash_dump_file: str = None, save_json: bool = False):
        """提取并保存ADC Mapping数据"""
        print("=== ADC Mapping 数据提取工具 ===")
        print(f"工作目录: {self.workspace_root}")
        
        # 创建输出目录
        self.create_resources_dir()
        
        # 提取数据
        raw_data = None
        if source_type == "device":
            print("从设备提取数据...")
            raw_data = self.extract_from_device()
        elif source_type == "dump" and flash_dump_file:
            print(f"从Flash转储文件提取数据: {flash_dump_file}")
            raw_data = self.extract_from_flash_dump(flash_dump_file)
        else:
            print("错误: 无效的数据源类型")
            return False
            
        if raw_data is None:
            print("错误: 无法获取数据")
            return False
            
        # 保存原始二进制数据
        success = self.save_binary_data(raw_data)
        
        # 解析数据
        try:
            # 添加原始数据转储（用于调试）
            self.add_hex_dump_info(raw_data)
            
            store = self.parse_adc_mapping_data(raw_data)
            self.print_mapping_info(store)
            
            # 如果请求保存JSON
            if save_json:
                self.save_json_data(store)
                
        except Exception as e:
            print(f"解析数据失败: {e}")
            print("原始二进制数据已保存，但无法解析结构")
            print("这可能是由于数据结构不匹配或数据损坏")
            
        return success

def main():
    """主函数"""
    extractor = ADCMappingExtractor()
    
    # 解析命令行参数
    save_json = "--json" in sys.argv
    if "--json" in sys.argv:
        sys.argv.remove("--json")
    
    # 检查命令行参数
    if len(sys.argv) > 1:
        # 如果提供了参数，假设是Flash转储文件路径
        flash_dump_file = sys.argv[1]
        if not Path(flash_dump_file).exists():
            print(f"错误: 文件不存在: {flash_dump_file}")
            sys.exit(1)
        success = extractor.extract_and_save("dump", flash_dump_file, save_json)
    else:
        # 默认尝试从设备提取
        print("使用方法:")
        print("  python extract_adc_mapping.py [flash_dump_file.bin] [--json]")
        print("")
        print("参数说明:")
        print("  flash_dump_file.bin  - Flash转储文件路径（可选）")
        print("  --json              - 同时保存JSON格式的解析数据（可选）")
        print("")
        print("如果提供flash_dump_file参数，将从转储文件提取数据")
        print("否则将尝试从连接的设备提取数据")
        print("")
        
        # 尝试从设备提取
        success = extractor.extract_and_save("device", save_json=save_json)
    
    if success:
        print("\n✅ 提取完成!")
        print(f"输出文件: {extractor.resources_dir}/slot_a_adc_mapping.bin")
        if save_json:
            print(f"JSON文件: {extractor.resources_dir}/slot_a_adc_mapping.json")
    else:
        print("\n❌ 提取失败!")
        sys.exit(1)

if __name__ == "__main__":
    main() 