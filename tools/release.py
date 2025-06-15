#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
STM32 HBox Release 管理工具

集成了发版打包和刷写功能：
1. 自动构建双槽release包（集成auto_release_packager功能）
2. 自动解压release包并刷写到设备
3. 支持槽A和槽B的完整管理
4. 包含版本管理、元数据和完整性校验
"""

import os
import sys
import subprocess
import json
import zipfile
import tempfile
import shutil
import struct
import hashlib
import argparse
import re
import locale
import time
import requests
import zlib
from pathlib import Path
from datetime import datetime
from typing import Dict, List, Optional, NamedTuple, Tuple, Any

# 元数据结构常量（与C代码保持一致）
FIRMWARE_MAGIC = 0x48424F58  # "HBOX"
METADATA_VERSION_MAJOR = 1
METADATA_VERSION_MINOR = 0
DEVICE_MODEL_STRING = "STM32H750_HBOX"
BOOTLOADER_VERSION = 0x00010000  # 1.0.0
HARDWARE_VERSION = 0x00010000    # 1.0.0

# 结构体大小常量（与C结构体对齐）
COMPONENT_SIZE = 170  # FirmwareComponent packed结构体大小: 32+64+4+4+65+1 = 170字节
FIRMWARE_COMPONENT_COUNT = 3
METADATA_SIZE = 807   # FirmwareMetadata总大小: 20+32+1+32+4+32+4+4+4+(170*3)+32+64+4+64 = 807字节

def calculate_crc32(data, skip_offset=None, skip_size=None):
    """计算CRC32校验和，可以跳过指定区域"""
    if skip_offset is None:
        return zlib.crc32(data) & 0xffffffff
    
    # 跳过指定区域计算CRC32
    before_skip = data[:skip_offset]
    after_skip = data[skip_offset + skip_size:]
    
    crc = zlib.crc32(before_skip) & 0xffffffff
    crc = zlib.crc32(after_skip, crc) & 0xffffffff
    return crc

def create_metadata_binary(version, slot, build_date, components):
    """创建与C结构体对齐的元数据二进制数据"""
    
    print(f"创建元数据二进制: 版本={version}, 槽位={slot}, 组件数={len(components)}")
    
    # 准备二进制数据缓冲区
    metadata_buffer = bytearray(METADATA_SIZE)
    offset = 0
    
    # === 安全校验区域 ===
    # magic (uint32_t)
    struct.pack_into('<I', metadata_buffer, offset, FIRMWARE_MAGIC)
    offset += 4
    
    # metadata_version_major (uint32_t)
    struct.pack_into('<I', metadata_buffer, offset, METADATA_VERSION_MAJOR)
    offset += 4
    
    # metadata_version_minor (uint32_t)
    struct.pack_into('<I', metadata_buffer, offset, METADATA_VERSION_MINOR)
    offset += 4
    
    # metadata_size (uint32_t)
    struct.pack_into('<I', metadata_buffer, offset, METADATA_SIZE)
    offset += 4
    
    # metadata_crc32 (uint32_t) - 先填0，最后计算
    crc32_offset = offset
    struct.pack_into('<I', metadata_buffer, offset, 0)
    offset += 4
    
    # === 固件信息区域 ===
    # firmware_version (char[32])
    version_bytes = version.encode('utf-8')[:31]
    metadata_buffer[offset:offset+len(version_bytes)] = version_bytes
    offset += 32
    
    # target_slot (uint8_t) - 1字节，无padding
    slot_value = 0 if slot.upper() == 'A' else 1
    struct.pack_into('<B', metadata_buffer, offset, slot_value)
    offset += 1
    
    # build_date (char[32]) - 紧接着target_slot，无padding
    build_date_bytes = build_date.encode('utf-8')[:31]
    metadata_buffer[offset:offset+len(build_date_bytes)] = build_date_bytes
    offset += 32
    
    # build_timestamp (uint32_t)
    timestamp = int(time.time())
    struct.pack_into('<I', metadata_buffer, offset, timestamp)
    offset += 4
    
    # === 设备兼容性区域 ===
    # device_model (char[32])
    device_model_bytes = DEVICE_MODEL_STRING.encode('utf-8')[:31]
    metadata_buffer[offset:offset+len(device_model_bytes)] = device_model_bytes
    offset += 32
    
    # hardware_version (uint32_t)
    struct.pack_into('<I', metadata_buffer, offset, HARDWARE_VERSION)
    offset += 4
    
    # bootloader_min_version (uint32_t)
    struct.pack_into('<I', metadata_buffer, offset, BOOTLOADER_VERSION)
    offset += 4
    
    # === 组件信息区域 ===
    # component_count (uint32_t)
    struct.pack_into('<I', metadata_buffer, offset, len(components))
    offset += 4
    
    # components array - 每个组件170字节，无padding（因为使用__attribute__((packed))）
    for i in range(FIRMWARE_COMPONENT_COUNT):
        component_start = offset
        
        if i < len(components):
            comp = components[i]
            
            # name (char[32])
            name_bytes = comp['name'].encode('utf-8')[:31]
            metadata_buffer[offset:offset+len(name_bytes)] = name_bytes
            offset += 32
            
            # file (char[64])
            file_bytes = comp['file'].encode('utf-8')[:63]
            metadata_buffer[offset:offset+len(file_bytes)] = file_bytes
            offset += 64
            
            # address (uint32_t)
            address = int(comp['address'], 16) if isinstance(comp['address'], str) else comp['address']
            struct.pack_into('<I', metadata_buffer, offset, address)
            offset += 4
            
            # size (uint32_t)
            struct.pack_into('<I', metadata_buffer, offset, comp['size'])
            offset += 4
            
            # sha256 (char[65])
            sha256_bytes = comp['sha256'].encode('utf-8')[:64]
            metadata_buffer[offset:offset+len(sha256_bytes)] = sha256_bytes
            offset += 65
            
            # active (bool/uint8_t)
            struct.pack_into('<B', metadata_buffer, offset, 1 if comp.get('active', True) else 0)
            offset += 1
            
        else:
            # 未使用的组件槽位填充为0
            offset += 32 + 64 + 4 + 4 + 65 + 1  # 170字节
        
        # 确保每个组件占用170字节（包括编译器padding）
        # 但由于使用了__attribute__((packed))，实际上是170字节
        component_actual_size = 32 + 64 + 4 + 4 + 65 + 1  # 170字节
        component_end = component_start + component_actual_size
        if offset < component_end:
            offset = component_end
    
    # === 安全签名区域 ===
    # firmware_hash (uint8_t[32]) - 预留，填充为0
    offset += 32
    
    # signature (uint8_t[64]) - 预留，填充为0
    offset += 64
    
    # signature_algorithm (uint32_t) - 预留，填充为0
    struct.pack_into('<I', metadata_buffer, offset, 0)
    offset += 4
    
    # === 预留区域 ===
    # reserved (uint8_t[64]) - 填充为0
    offset += 64
    
    # 验证总大小
    if offset != METADATA_SIZE:
        print(f"警告: 元数据大小不匹配! 期望={METADATA_SIZE}, 实际={offset}")
        # 调整缓冲区大小以匹配实际计算的大小
        if offset < METADATA_SIZE:
            # 如果计算的大小小于期望大小，保持原大小（已经用0填充）
            pass
        else:
            # 如果计算的大小大于期望大小，截断到期望大小
            metadata_buffer = metadata_buffer[:METADATA_SIZE]
    
    # 计算并设置CRC32校验和（跳过CRC字段本身）
    crc32_value = calculate_crc32(metadata_buffer, crc32_offset, 4)
    struct.pack_into('<I', metadata_buffer, crc32_offset, crc32_value)
    
    # 添加调试输出，显示关键字段的偏移
    print(f"Python字段偏移计算:")
    print(f"  - magic: 0")
    print(f"  - metadata_version_major: 4")
    print(f"  - metadata_version_minor: 8") 
    print(f"  - metadata_size: 12")
    print(f"  - metadata_crc32: 16")
    print(f"  - firmware_version: 20")
    print(f"  - target_slot: 52 (1字节)")
    print(f"  - build_date: 53")
    print(f"  - build_timestamp: 85")
    print(f"  - device_model: 89")
    print(f"  - hardware_version: 121")
    print(f"  - bootloader_min_version: 125")
    print(f"  - component_count: 129")
    print(f"  - components[0]: 133")
    
    print(f"元数据生成完成:")
    print(f"  - 魔数: 0x{FIRMWARE_MAGIC:08X}")
    print(f"  - 版本: {version}")
    print(f"  - 槽位: {slot}")
    print(f"  - 设备型号: {DEVICE_MODEL_STRING}")
    print(f"  - 组件数量: {len(components)}")
    print(f"  - CRC32: 0x{crc32_value:08X}")
    print(f"  - 总大小: {len(metadata_buffer)} 字节")
    
    return bytes(metadata_buffer)

class ProgressBar:
    """简单的进度条显示"""
    def __init__(self, total_steps, width=50):
        self.total_steps = total_steps
        self.current_step = 0
        self.width = width
        self.start_time = time.time()
    
    def update(self, step_name=""):
        self.current_step += 1
        progress = self.current_step / self.total_steps
        filled = int(self.width * progress)
        bar = '█' * filled + '░' * (self.width - filled)
        percent = progress * 100
        elapsed = time.time() - self.start_time
        
        print(f"\r[{bar}] {percent:6.1f}% ({self.current_step}/{self.total_steps}) {step_name}", end='', flush=True)
        if self.current_step == self.total_steps:
            print(f"\n完成! 总耗时: {elapsed:.1f}秒")

class ReleaseFlasher:
    """Release包刷写器"""
    
    def __init__(self, project_root: Path):
        self.project_root = project_root
        self.tools_dir = project_root / "tools"
        self.application_dir = project_root / "application"  # 添加application目录
        self.openocd_configs_dir = self.tools_dir / "openocd_configs"
        self.temp_dir = None
        
        # OpenOCD配置文件 - 使用tools目录下的配置
        self.openocd_config = self.openocd_configs_dir / "ST-LINK-QSPIFLASH.cfg"
        
        # 槽地址映射
        self.slot_addresses = {
            'A': {
                'application': '0x90000000',
                'webresources': '0x90100000',
                'adc_mapping': '0x90280000'
            },
            'B': {
                'application': '0x902B0000',
                'webresources': '0x903B0000',
                'adc_mapping': '0x90530000'
            }
        }
        
        # 元数据地址
        self.metadata_address = '0x90570000'
    
    def extract_release_package(self, package_path: str) -> Tuple[Dict, Path]:
        """解压release包并返回manifest和临时目录"""
        package_file = Path(package_path)
        if not package_file.exists():
            raise FileNotFoundError(f"Release包不存在: {package_path}")
        
        # 创建临时目录
        self.temp_dir = Path(tempfile.mkdtemp(prefix="release_flash_"))
        print(f"解压到临时目录: {self.temp_dir}")
        
        try:
            # 解压ZIP文件
            with zipfile.ZipFile(package_file, 'r') as zf:
                zf.extractall(self.temp_dir)
            
            # 读取manifest
            manifest_file = self.temp_dir / "manifest.json"
            if not manifest_file.exists():
                raise FileNotFoundError("Release包中缺少manifest.json文件")
            
            with open(manifest_file, 'r', encoding='utf-8') as f:
                manifest = json.load(f)
            
            print(f"成功解压Release包: {package_file.name}")
            print(f"版本: {manifest.get('version', 'Unknown')}")
            print(f"槽位: {manifest.get('slot', 'Unknown')}")
            print(f"组件数量: {len(manifest.get('components', []))}")
            
            return manifest, self.temp_dir
            
        except Exception as e:
            self.cleanup_temp_dir()
            raise Exception(f"解压Release包失败: {e}")
    
    def cleanup_temp_dir(self):
        """清理临时目录"""
        if self.temp_dir and self.temp_dir.exists():
            try:
                shutil.rmtree(self.temp_dir)
                print(f"已清理临时目录: {self.temp_dir}")
            except Exception as e:
                print(f"清理临时目录失败: {e}")
            finally:
                self.temp_dir = None
    
    def get_slot_address(self, component_name: str, slot: str) -> str:
        """根据组件名和槽位获取地址"""
        slot = slot.upper()
        if slot not in self.slot_addresses:
            raise ValueError(f"不支持的槽位: {slot}")
        
        if component_name not in self.slot_addresses[slot]:
            raise ValueError(f"槽位{slot}不支持组件: {component_name}")
        
        return self.slot_addresses[slot][component_name]
    
    def flash_component(self, component_file: Path, target_address: str, component_name: str) -> bool:
        """刷写单个组件"""
        if not component_file.exists():
            print(f"错误: 组件文件不存在: {component_file}")
            return False
        
        if not self.openocd_config.exists():
            print(f"错误: OpenOCD配置文件不存在: {self.openocd_config}")
            return False
        
        print(f"正在刷写 {component_name}: {component_file.name} -> {target_address}")
        
        # 根据文件类型确定偏移地址
        # HEX文件包含地址信息，使用0x00000000偏移
        # BIN文件需要指定目标地址
        if component_file.suffix.lower() == '.hex':
            flash_offset = "0x00000000"
        else:
            flash_offset = target_address
        
        # 构建OpenOCD命令 - 使用tools目录下的配置
        # 组件文件使用绝对路径，但转换为正斜杠格式
        component_path = str(component_file).replace('\\', '/')
        
        cmd = [
            "openocd",
            "-d0",
            "-f", "openocd_configs/ST-LINK-QSPIFLASH.cfg",  # 相对于tools目录的路径
            "-c", "init",
            "-c", "halt",
            "-c", "reset init",
            "-c", f"flash write_image erase {component_path} {flash_offset}",
            "-c", f"flash verify_image {component_path} {flash_offset}",
            "-c", "reset run",
            "-c", "shutdown"
        ]
        
        try:
            result = subprocess.run(
                cmd,
                cwd=self.tools_dir,  # 在tools目录下运行
                capture_output=True,
                text=True,
                timeout=120
            )
            
            if result.returncode == 0:
                # 从输出中提取写入信息
                wrote_info = ""
                if result.stdout and "wrote" in result.stdout:
                    for line in result.stdout.split('\n'):
                        if "wrote" in line and "bytes" in line:
                            # 提取写入的字节数信息
                            wrote_info = f" ({line.strip().split()[-2]} {line.strip().split()[-1]})"
                            break
                
                print(f"✓ {component_name} 刷写成功{wrote_info}")
                return True
            else:
                print(f"✗ {component_name} 刷写失败")
                # 只在失败时显示关键错误信息
                if result.stderr:
                    error_lines = result.stderr.split('\n')
                    for line in error_lines:
                        if any(keyword in line.lower() for keyword in ['error:', 'failed', 'timeout', 'not found']):
                            print(f"  错误: {line.strip()}")
                            break
                return False
                
        except subprocess.TimeoutExpired:
            print(f"✗ {component_name} 刷写超时")
            return False
        except FileNotFoundError:
            print(f"✗ 未找到OpenOCD工具，请确保已安装并在PATH中")
            return False
        except Exception as e:
            print(f"✗ {component_name} 刷写异常: {e}")
            return False
    
    def flash_release_package(self, package_path: str, target_slot: str = None, 
                            components: List[str] = None) -> bool:
        """刷写release包到设备"""
        try:
            # 解压release包
            manifest, temp_dir = self.extract_release_package(package_path)
            
            # 确定目标槽位
            if target_slot is None:
                target_slot = manifest.get('slot', 'A').upper()
            
            print(f"开始刷写release包: {Path(package_path).name}")
            print(f"目标槽位: {target_slot}")
            
            # 确定要刷写的组件 - 处理components为列表的情况
            manifest_components = manifest.get('components', [])
            if isinstance(manifest_components, list):
                # components是列表格式，转换为字典格式以便后续处理
                components_dict = {}
                for comp in manifest_components:
                    components_dict[comp['name']] = comp
                available_components = list(components_dict.keys())
            else:
                # components是字典格式（旧格式兼容）
                components_dict = manifest_components
                available_components = list(components_dict.keys())
            
            if components is None:
                components = available_components
            else:
                # 验证指定的组件是否存在
                invalid_components = [c for c in components if c not in available_components]
                if invalid_components:
                    print(f"错误: 指定的组件不存在: {invalid_components}")
                    print(f"可用组件: {available_components}")
                    return False
            
            print(f"将刷写组件: {components}")
            
            # 刷写各个组件
            success_count = 0
            total_count = len(components)
            
            for component_name in components:
                component_info = components_dict[component_name]
                component_file = temp_dir / component_info['file']
                
                # 根据目标槽位调整地址
                target_address = self.get_slot_address(component_name, target_slot)
                
                print(f"\n[{success_count + 1}/{total_count}] 刷写组件: {component_name}")
                
                if self.flash_component(component_file, target_address, component_name):
                    success_count += 1
                else:
                    print(f"组件 {component_name} 刷写失败")
                    return False
            
            # 生成并刷写元数据
            print(f"\n[{success_count + 1}/{total_count + 1}] 生成并刷写元数据...")
            
            # 使用新的create_metadata_binary函数
            components_list = []
            for comp_name in components:
                comp_info = components_dict[comp_name]
                components_list.append({
                    'name': comp_name,
                    'file': comp_info['file'],
                    'address': self.get_slot_address(comp_name, target_slot),
                    'size': comp_info['size'],
                    'sha256': comp_info['sha256'],
                    'active': True
                })
            
            # 生成元数据二进制
            metadata_binary = create_metadata_binary(
                version=manifest.get('version', '1.0.0'),
                slot=target_slot,
                build_date=manifest.get('build_date', datetime.now().strftime('%Y-%m-%d %H:%M:%S')),
                components=components_list
            )
            
            # 写入临时文件
            metadata_file = temp_dir / "metadata.bin"
            with open(metadata_file, 'wb') as f:
                f.write(metadata_binary)
            
            # 刷写元数据
            if self.flash_metadata(metadata_file):
                print(f"✓ 所有组件和元数据刷写成功!")
                return True
            else:
                print(f"✗ 元数据刷写失败")
                return False
                
        except Exception as e:
            print(f"刷写失败: {e}")
            return False
        finally:
            # 清理临时目录
            self.cleanup_temp_dir()

    def flash_metadata(self, metadata_file: Path) -> bool:
        """刷写元数据到Flash"""
        if not metadata_file.exists():
            print(f"错误: 元数据文件不存在: {metadata_file}")
            return False
        
        if not self.openocd_config.exists():
            print(f"错误: OpenOCD配置文件不存在: {self.openocd_config}")
            return False
        
        # 元数据地址转换：从虚拟地址转换为QSPI Flash物理偏移
        # 虚拟地址: 0x90570000
        # 物理偏移: 0x90570000 - 0x90000000 = 0x570000
        metadata_physical_address = "0x570000"
        
        print(f"正在刷写元数据: {metadata_file.name} -> {self.metadata_address} (物理偏移: {metadata_physical_address})")
        
        # 构建OpenOCD命令 - 使用物理偏移地址
        metadata_path = str(metadata_file).replace('\\', '/')
        
        cmd = [
            "openocd",
            "-d0",
            "-f", "openocd_configs/ST-LINK-QSPIFLASH.cfg",
            "-c", "init",
            "-c", "halt",
            "-c", "reset init",
            "-c", f"flash write_image erase {metadata_path} {self.metadata_address}",
            "-c", f"flash verify_image {metadata_path} {self.metadata_address}",
            "-c", "reset run",
            "-c", "shutdown"
        ]
        
        try:
            result = subprocess.run(
                cmd,
                cwd=self.tools_dir,
                capture_output=True,
                text=True,
                timeout=60
            )
            
            if result.returncode == 0:
                # 从输出中提取写入信息
                wrote_info = ""
                if result.stdout and "wrote" in result.stdout:
                    for line in result.stdout.split('\n'):
                        if "wrote" in line and "bytes" in line:
                            wrote_info = f" ({line.strip().split()[-2]} {line.strip().split()[-1]})"
                            break
                
                print(f"✓ 元数据刷写成功{wrote_info}")
                return True
            else:
                print(f"✗ 元数据刷写失败")
                if result.stderr:
                    error_lines = result.stderr.split('\n')
                    for line in error_lines:
                        if any(keyword in line.lower() for keyword in ['error:', 'failed', 'timeout', 'not found']):
                            print(f"  错误: {line.strip()}")
                            break
                return False
                
        except subprocess.TimeoutExpired:
            print(f"✗ 元数据刷写超时")
            return False
        except Exception as e:
            print(f"✗ 元数据刷写异常: {e}")
            return False

class ReleaseManager:
    """Release管理器 - 集成打包和刷写功能"""
    
    def __init__(self):
        self.project_root = Path(__file__).parent.parent
        self.application_dir = self.project_root / "application"
        self.tools_dir = self.project_root / "tools"
        self.resources_dir = self.project_root / "resources"
        self.releases_dir = self.project_root / "releases"
        self.openocd_configs_dir = self.tools_dir / "openocd_configs"
        self.temp_dir = None
        
        # OpenOCD配置
        self.openocd_config = self.openocd_configs_dir / "ST-LINK-QSPIFLASH.cfg"
        
        # 槽地址映射（用于刷写）
        self.slot_addresses = {
            'A': {
                'application': '0x90000000',
                'webresources': '0x90100000',
                'adc_mapping': '0x90280000'
            },
            'B': {
                'application': '0x902B0000',
                'webresources': '0x903B0000',
                'adc_mapping': '0x90530000'
            }
        }
        
        # 创建目录
        self.ensure_directories()
        
        # 初始化刷写器
        self.flasher = ReleaseFlasher(self.project_root)
    
    def ensure_directories(self):
        """确保必要目录存在"""
        self.resources_dir.mkdir(exist_ok=True)
        self.releases_dir.mkdir(exist_ok=True)
        print(f"发版目录: {self.releases_dir}")
    
    def calculate_checksum(self, data: bytes) -> str:
        """计算SHA256校验和"""
        return hashlib.sha256(data).hexdigest()
    
    def run_cmd(self, cmd, cwd=None, check=True):
        """执行命令的辅助方法"""
        print(f"[CMD] {' '.join(map(str, cmd))}")
        result = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, encoding='utf-8', errors='ignore')
        if result.returncode != 0 and check:
            print("STDOUT:")
            print(result.stdout)
            print("STDERR:")
            print(result.stderr)
            raise RuntimeError(f"命令执行失败: {' '.join(cmd)}")
        return result
    
    def sha256sum_file(self, path):
        """计算文件的SHA256校验和"""
        h = hashlib.sha256()
        with open(path, 'rb') as f:
            while True:
                chunk = f.read(8192)
                if not chunk:
                    break
                h.update(chunk)
        return h.hexdigest()

    # ==================== 自动构建功能（集成auto_release_packager） ====================
    
    def build_app_with_build_py(self, slot, progress=None):
        """使用build.py构建Application"""
        if progress:
            progress.update(f"构建槽{slot} Application...")
        else:
            print(f"正在构建槽{slot}的Application...")
        
        try:
            result = self.run_cmd([sys.executable, str(self.tools_dir/'build.py'), 'build', 'app', slot], 
                                cwd=self.tools_dir, check=False)
            # 检查是否生成了目标文件，而不是依赖返回码
            hex_file = self.application_dir / 'build' / f'application_slot_{slot}.hex'
            if not hex_file.exists():
                print(f"\n构建可能失败，未找到HEX文件: {hex_file}")
                print("STDOUT:")
                print(result.stdout)
                print("STDERR:")
                print(result.stderr)
                raise FileNotFoundError(f"未找到HEX文件: {hex_file}")
            
            if not progress:
                print(f"成功构建槽{slot}的Application: {hex_file}")
            return hex_file
        except Exception as e:
            print(f"\n构建槽{slot}失败: {e}")
            raise

    def copy_adc_mapping_from_resources(self, out_dir, progress=None):
        """从resources目录复制ADC映射文件"""
        if progress:
            progress.update("复制ADC映射...")
        else:
            print("正在复制ADC映射...")
        
        # 从resources目录查找ADC映射文件
        adc_source = self.resources_dir / "slot_a_adc_mapping.bin"
        
        if not adc_source.exists():
            raise FileNotFoundError(f"未找到ADC映射文件: {adc_source}")
        
        # 复制到输出目录
        adc_dest = out_dir / "slot_a_adc_mapping.bin"
        shutil.copy2(adc_source, adc_dest)
        
        if not progress:
            print(f"成功复制ADC映射: {adc_source} -> {adc_dest}")
        
        return adc_dest

    def copy_webresources_for_auto(self, out_dir, progress=None):
        """复制WebResources文件"""
        if progress:
            progress.update("复制WebResources...")
        else:
            print("正在复制WebResources...")
        
        # 查找webresources文件
        web_sources = [
            self.resources_dir / "webresources.bin",
            self.application_dir / "Libs" / "httpd" / "ex_fsdata.bin"
        ]
        
        for src in web_sources:
            if src.exists():
                dst = out_dir / "webresources.bin"
                shutil.copy2(src, dst)
                if not progress:
                    print(f"成功复制WebResources: {src} -> {dst}")
                return dst
        
        raise FileNotFoundError("未找到WebResources文件")

    def make_manifest_for_auto(self, slot, hex_file, adc_file, web_file, version):
        """生成manifest文件"""
        manifest = {
            "version": version,
            "slot": slot,
            "build_date": datetime.now().strftime('%Y-%m-%d %H:%M:%S'),
            "components": []
        }
        
        # Application组件
        manifest["components"].append({
            "name": "application",
            "file": hex_file.name,
            "address": "0x90000000" if slot == "A" else "0x902B0000",
            "size": hex_file.stat().st_size,
            "sha256": self.sha256sum_file(hex_file)
        })
        
        # WebResources组件
        manifest["components"].append({
            "name": "webresources",
            "file": web_file.name,
            "address": "0x90100000" if slot == "A" else "0x903B0000",
            "size": web_file.stat().st_size,
            "sha256": self.sha256sum_file(web_file)
        })
        
        # ADC Mapping组件
        manifest["components"].append({
            "name": "adc_mapping",
            "file": adc_file.name,
            "address": "0x90280000" if slot == "A" else "0x90530000",
            "size": adc_file.stat().st_size,
            "sha256": self.sha256sum_file(adc_file)
        })
        
        return manifest

    def make_auto_release_pkg(self, slot, version, out_dir, build_timestamp):
        """生成单个槽的release包"""
        print(f"\n=== 开始生成槽{slot} release包 ===")
        
        # 创建进度条
        progress = ProgressBar(6)
        
        try:
            # 1. 构建Application
            hex_file = self.build_app_with_build_py(slot, progress)
            
            # 2. 复制ADC映射
            adc_file = self.copy_adc_mapping_from_resources(out_dir, progress)
            
            # 3. 复制WebResources
            web_file = self.copy_webresources_for_auto(out_dir, progress)
            
            # 4. 复制hex文件到输出目录
            progress.update("复制Application文件...")
            out_hex_file = out_dir / f'application_slot_{slot.lower()}.hex'
            shutil.copy2(hex_file, out_hex_file)
            
            # 5. 生成manifest
            progress.update("生成manifest...")
            manifest = self.make_manifest_for_auto(slot, out_hex_file, adc_file, web_file, version)
            manifest_file = out_dir / 'manifest.json'
            with open(manifest_file, 'w', encoding='utf-8') as f:
                json.dump(manifest, f, indent=2, ensure_ascii=False)
            
            # 6. 打包并移动到releases目录
            progress.update("移动到releases目录...")
            package_name = f'hbox_firmware_{version}_{slot.lower()}_{build_timestamp}.zip'
            temp_package_path = out_dir / package_name
            final_package_path = self.releases_dir / package_name
            
            # 创建ZIP包
            with zipfile.ZipFile(temp_package_path, 'w', zipfile.ZIP_DEFLATED) as zf:
                zf.write(out_hex_file, out_hex_file.name)
                zf.write(web_file, web_file.name)
                zf.write(adc_file, adc_file.name)
                zf.write(manifest_file, manifest_file.name)
            
            # 移动到最终位置
            shutil.move(temp_package_path, final_package_path)
            
            # 显示结果
            package_size = final_package_path.stat().st_size
            print(f"\n[OK] 生成release包: {final_package_path.name}")
            print(f"     包大小: {package_size:,} 字节")
            
            return final_package_path
            
        except Exception as e:
            print(f"\n[ERROR] 生成槽{slot}失败: {e}")
            raise

    def create_auto_release(self, version: str) -> List[str]:
        """自动构建双槽release包（集成auto_release_packager功能）"""
        print("=== STM32 HBox 双槽Release包生成工具 ===")
        print(f"工作目录: {self.project_root}")
        print(f"输出目录: {self.releases_dir}")
        print(f"版本号: {version}")
        
        # 创建临时目录
        out_dir = self.tools_dir / 'release_temp'
        out_dir.mkdir(exist_ok=True)
        
        # 显示总体进度
        print(f"\n开始生成双槽release包...")
        total_start_time = time.time()
        
        # 生成统一的构建时间戳
        build_timestamp = datetime.now().strftime('%Y%m%d_%H%M%S')
        print(f"构建时间戳: {build_timestamp}")
        
        success_count = 0
        generated_packages = []
        
        try:
            for slot in ['A', 'B']:
                try:
                    package_path = self.make_auto_release_pkg(slot, version, out_dir, build_timestamp)
                    success_count += 1
                    generated_packages.append(package_path)
                except Exception as e:
                    print(f"\n[ERROR] 生成槽{slot}失败: {e}")
                    continue
            
            total_elapsed = time.time() - total_start_time
            
            print(f"\n" + "="*60)
            print(f"=== 生成完成 ===")
            print(f"成功生成: {success_count}/2 个release包")
            print(f"总耗时: {total_elapsed:.1f}秒")
            
            if success_count > 0:
                print(f"\n生成的release包:")
                for package in generated_packages:
                    print(f"  - {package.name}")
                print(f"\n所有包已保存到: {self.releases_dir}")
            else:
                print("所有包生成失败，请检查错误信息")
                return []
        
        finally:
            # 清理临时目录
            if out_dir.exists():
                print(f"\n清理临时目录: {out_dir}")
                try:
                    shutil.rmtree(out_dir)
                    print("临时目录清理完成")
                except Exception as e:
                    print(f"清理临时目录失败: {e}")
        
        return generated_packages

    # ==================== 发版包管理功能 ====================
    
    def list_available_packages(self) -> list:
        """列出可用的release包"""
        if not self.releases_dir.exists():
            return []
        
        packages = []
        for file_path in self.releases_dir.glob("*.zip"):
            if file_path.name.startswith("hbox_firmware_"):
                packages.append(file_path)
        
        return sorted(packages, key=lambda x: x.stat().st_mtime, reverse=True)
    
    def list_releases(self):
        """列出所有发版包"""
        print("可用的发版包:")
        print("-" * 50)
        
        if not self.releases_dir.exists():
            print("无发版包")
            return
        
        release_files = list(self.releases_dir.glob("*.zip"))
        release_files.sort(key=lambda x: x.stat().st_mtime, reverse=True)
        
        if not release_files:
            print("无发版包")
            return
        
        for file in release_files:
            stat = file.stat()
            size_mb = stat.st_size / (1024 * 1024)
            mtime = datetime.fromtimestamp(stat.st_mtime)
            print(f"{file.name}")
            print(f"  大小: {size_mb:.1f} MB")
            print(f"  创建时间: {mtime.strftime('%Y-%m-%d %H:%M:%S')}")
            print()
    
    def verify_release_package(self, package_file: str) -> bool:
        """验证发版包完整性"""
        print(f"验证发版包: {package_file}")
        
        try:
            with zipfile.ZipFile(package_file, 'r') as zf:
                # 读取manifest
                with zf.open('manifest.json') as f:
                    manifest = json.load(f)
                
                # 验证每个组件
                for comp_info in manifest["components"]:
                    comp_name = comp_info["name"]
                    comp_file = comp_info["file"]
                    expected_checksum = comp_info["sha256"]
                    
                    # 读取组件数据
                    with zf.open(comp_file) as f:
                        data = f.read()
                    
                    # 计算校验和
                    actual_checksum = self.calculate_checksum(data)
                    
                    if actual_checksum == expected_checksum:
                        print(f"  ✓ {comp_name}: 校验通过")
                    else:
                        print(f"  ✗ {comp_name}: 校验失败")
                        print(f"    期望: {expected_checksum}")
                        print(f"    实际: {actual_checksum}")
                        return False
                
                print("✓ 发版包验证通过")
                return True
                
        except Exception as e:
            print(f"✗ 验证失败: {e}")
            return False

    # ==================== 刷写功能 ====================
    
    def resolve_package_path(self, package_input: str) -> Optional[str]:
        """智能解析包路径，支持包名或完整路径"""
        package_path = Path(package_input)
        
        # 如果是绝对路径或相对路径且文件存在，直接返回
        if package_path.exists():
            return str(package_path.resolve())
        
        # 如果只是包名，在releases目录中查找
        if not package_path.is_absolute() and '/' not in package_input and '\\' not in package_input:
            # 这是一个包名，在releases目录中查找
            releases_path = self.releases_dir / package_input
            if releases_path.exists():
                return str(releases_path)
            
            # 如果没有.zip后缀，尝试添加
            if not package_input.endswith('.zip'):
                releases_path_with_zip = self.releases_dir / (package_input + '.zip')
                if releases_path_with_zip.exists():
                    return str(releases_path_with_zip)
            
            # 模糊匹配：查找包含该名称的包
            available_packages = self.list_available_packages()
            matching_packages = [p for p in available_packages if package_input.lower() in p.name.lower()]
            
            if len(matching_packages) == 1:
                print(f"找到匹配的包: {matching_packages[0].name}")
                return str(matching_packages[0])
            elif len(matching_packages) > 1:
                print(f"找到多个匹配的包:")
                for i, pkg in enumerate(matching_packages, 1):
                    print(f"  {i}. {pkg.name}")
                print(f"请使用更具体的包名")
                return None
        
        # 如果是相对路径但文件不存在，尝试相对于releases目录
        if not package_path.is_absolute():
            releases_relative_path = self.releases_dir / package_input
            if releases_relative_path.exists():
                return str(releases_relative_path)
        
        return None
    
    def flash_release_package(self, package_path: str, target_slot: str = None, 
                            components: List[str] = None) -> bool:
        """刷写release包"""
        return self.flasher.flash_release_package(package_path, target_slot, components)
    
    def select_release_package(self) -> Optional[str]:
        """交互式选择release包"""
        packages = self.list_available_packages()
        if not packages:
            print("没有可用的release包")
            return None
        
        print("\n可用的release包:")
        for i, package in enumerate(packages, 1):
            stat = package.stat()
            size_mb = stat.st_size / (1024 * 1024)
            mtime = datetime.fromtimestamp(stat.st_mtime)
            print(f"{i:2d}. {package.name}")
            print(f"     大小: {size_mb:.1f} MB, 时间: {mtime.strftime('%Y-%m-%d %H:%M:%S')}")
        
        while True:
            try:
                choice = input(f"\n请选择要刷写的包 (1-{len(packages)}, 或按Enter取消): ").strip()
                if not choice:
                    return None
                
                index = int(choice) - 1
                if 0 <= index < len(packages):
                    return str(packages[index])
                else:
                    print(f"请输入1到{len(packages)}之间的数字")
            except ValueError:
                print("请输入有效的数字")
            except KeyboardInterrupt:
                print("\n操作已取消")
                return None

    # ==================== 上传功能 ====================
    
    def parse_package_info(self, package_path: str) -> Optional[Dict]:
        """从包名解析版本和槽位信息"""
        package_file = Path(package_path)
        package_name = package_file.name
        
        # 解析包名格式: hbox_firmware_{version}_{slot}_{timestamp}.zip
        pattern = r'hbox_firmware_(.+)_([ab])_(\d{8}_\d{6})\.zip'
        match = re.match(pattern, package_name)
        
        if not match:
            return None
        
        version, slot, timestamp = match.groups()
        
        return {
            'version': version,
            'slot': slot.upper(),
            'timestamp': timestamp,
            'filename': package_name
        }
    
    def upload_firmware_to_server(self, slot_a_path: str = None, slot_b_path: str = None, 
                                 server_url: str = "http://localhost:3000", 
                                 desc: str = None) -> bool:
        """上传固件包到服务器"""
        
        if not slot_a_path and not slot_b_path:
            print("错误: 至少需要指定一个槽的固件包")
            return False
        
        # 解析包信息
        package_info = None
        if slot_a_path:
            package_info = self.parse_package_info(slot_a_path)
            if not package_info:
                print(f"错误: 无法解析包名格式: {slot_a_path}")
                return False
        elif slot_b_path:
            package_info = self.parse_package_info(slot_b_path)
            if not package_info:
                print(f"错误: 无法解析包名格式: {slot_b_path}")
                return False
        
        version = package_info['version']
        if not desc:
            desc = f"自动上传的固件包，版本 {version}"
        
        print("=" * 60)
        print("上传固件包到服务器")
        print("=" * 60)
        print(f"服务器地址: {server_url}")
        print(f"版本号: {version}")
        print(f"描述: {desc}")
        
        # 检查文件是否存在
        files_to_upload = {}
        if slot_a_path:
            slot_a_file = Path(slot_a_path)
            if not slot_a_file.exists():
                print(f"错误: 槽A文件不存在: {slot_a_path}")
                return False
            files_to_upload['slotA'] = (slot_a_file.name, open(slot_a_file, 'rb'), 'application/zip')
            print(f"槽A包: {slot_a_file.name} ({slot_a_file.stat().st_size / 1024 / 1024:.1f} MB)")
        
        if slot_b_path:
            slot_b_file = Path(slot_b_path)
            if not slot_b_file.exists():
                print(f"错误: 槽B文件不存在: {slot_b_path}")
                return False
            files_to_upload['slotB'] = (slot_b_file.name, open(slot_b_file, 'rb'), 'application/zip')
            print(f"槽B包: {slot_b_file.name} ({slot_b_file.stat().st_size / 1024 / 1024:.1f} MB)")
        
        print()
        
        try:
            # 准备上传数据
            form_data = {
                'version': version,
                'desc': desc
            }
            
            upload_url = f"{server_url}/api/firmwares/upload"
            print(f"正在上传到: {upload_url}")
            
            # 发送POST请求
            response = requests.post(
                upload_url,
                data=form_data,
                files=files_to_upload,
                timeout=300,  # 5分钟超时
                proxies={'http': None, 'https': None} if 'localhost' in server_url or '127.0.0.1' in server_url else None
            )
            
            # 关闭文件句柄
            for file_tuple in files_to_upload.values():
                file_tuple[1].close()
            
            if response.status_code == 200:
                result = response.json()
                if result.get('success'):
                    print("✓ 上传成功!")
                    print(f"固件ID: {result['data']['id']}")
                    print(f"创建时间: {result['data']['createTime']}")
                    
                    # 显示下载链接
                    data = result['data']
                    if 'slotA' in data and data['slotA']:
                        print(f"槽A下载链接: {data['slotA']['downloadUrl']}")
                    if 'slotB' in data and data['slotB']:
                        print(f"槽B下载链接: {data['slotB']['downloadUrl']}")
                    
                    return True
                else:
                    print(f"✗ 上传失败: {result.get('message', '未知错误')}")
                    return False
            else:
                print(f"✗ 上传失败: HTTP {response.status_code}")
                try:
                    error_info = response.json()
                    print(f"错误信息: {error_info.get('message', response.text)}")
                except:
                    print(f"响应内容: {response.text}")
                return False
                
        except requests.exceptions.ConnectionError:
            print("✗ 连接失败: 无法连接到服务器")
            print("请确保服务器已启动并且地址正确")
            return False
        except requests.exceptions.Timeout:
            print("✗ 上传超时: 请检查网络连接和文件大小")
            return False
        except Exception as e:
            print(f"✗ 上传异常: {e}")
            return False
        finally:
            # 确保文件句柄被关闭
            for file_tuple in files_to_upload.values():
                try:
                    file_tuple[1].close()
                except:
                    pass
    
    def upload_latest_packages(self, version: str = None, server_url: str = "http://localhost:3000", 
                             desc: str = None) -> bool:
        """上传最新的双槽包到服务器"""
        
        # 如果指定了版本，查找该版本的包
        if version:
            packages = self.list_available_packages()
            slot_a_path = None
            slot_b_path = None
            
            for package in packages:
                info = self.parse_package_info(str(package))
                if info and info['version'] == version:
                    if info['slot'] == 'A':
                        slot_a_path = str(package)
                    elif info['slot'] == 'B':
                        slot_b_path = str(package)
            
            if not slot_a_path and not slot_b_path:
                print(f"错误: 未找到版本 {version} 的固件包")
                return False
        else:
            # 查找最新的包
            packages = self.list_available_packages()
            if not packages:
                print("错误: 没有可用的固件包")
                return False
            
            # 按时间排序，找最新的
            latest_packages = {}
            for package in packages:
                info = self.parse_package_info(str(package))
                if info:
                    key = f"{info['version']}_{info['timestamp']}"
                    if key not in latest_packages:
                        latest_packages[key] = {}
                    latest_packages[key][info['slot']] = str(package)
            
            if not latest_packages:
                print("错误: 没有有效的固件包")
                return False
            
            # 选择最新的版本
            latest_key = sorted(latest_packages.keys())[-1]
            latest_set = latest_packages[latest_key]
            
            slot_a_path = latest_set.get('A')
            slot_b_path = latest_set.get('B')
            
            if not slot_a_path and not slot_b_path:
                print("错误: 最新版本没有可用的固件包")
                return False
            
            # 从最新包解析版本信息
            if slot_a_path:
                info = self.parse_package_info(slot_a_path)
                version = info['version']
            elif slot_b_path:
                info = self.parse_package_info(slot_b_path)
                version = info['version']
        
        print(f"准备上传版本 {version} 的固件包:")
        if slot_a_path:
            print(f"  槽A: {Path(slot_a_path).name}")
        if slot_b_path:
            print(f"  槽B: {Path(slot_b_path).name}")
        
        return self.upload_firmware_to_server(
            slot_a_path=slot_a_path,
            slot_b_path=slot_b_path,
            server_url=server_url,
            desc=desc
        )

    # ==================== 固件删除功能 ====================
    
    def delete_firmware_from_server(self, firmware_id: str, server_url: str = "http://localhost:3000") -> bool:
        """从服务器删除指定ID的固件"""
        
        print("=" * 60)
        print("删除服务器固件")
        print("=" * 60)
        print(f"服务器地址: {server_url}")
        print(f"固件ID: {firmware_id}")
        print()
        
        try:
            # 先查询固件信息
            query_url = f"{server_url}/api/firmwares/{firmware_id}"
            print(f"正在查询固件信息: {query_url}")
            
            response = requests.get(
                query_url,
                timeout=30,
                proxies={'http': None, 'https': None} if 'localhost' in server_url or '127.0.0.1' in server_url else None
            )
            
            if response.status_code == 404:
                print(f"✗ 固件不存在: ID {firmware_id}")
                return False
            elif response.status_code != 200:
                print(f"✗ 查询固件信息失败: HTTP {response.status_code}")
                return False
            
            # 解析固件信息
            result = response.json()
            if not result.get('success'):
                print(f"✗ 查询固件信息失败: {result.get('message', '未知错误')}")
                return False
            
            firmware = result['data']
            print(f"找到固件: {firmware['name']} v{firmware['version']}")
            
            # 显示要删除的内容
            slots_info = []
            if firmware.get('slotA'):
                slots_info.append(f"槽A ({firmware['slotA']['originalName']})")
            if firmware.get('slotB'):
                slots_info.append(f"槽B ({firmware['slotB']['originalName']})")
            
            if slots_info:
                print(f"包含: {', '.join(slots_info)}")
            
            print(f"创建时间: {firmware['createTime']}")
            print()
            
            # 确认删除
            confirm = input("确认删除此固件？这个操作不可恢复！(y/N): ").strip().lower()
            if confirm not in ['y', 'yes', '是']:
                print("操作已取消")
                return False
            
            print()
            
            # 执行删除
            delete_url = f"{server_url}/api/firmwares/{firmware_id}"
            print(f"正在删除: {delete_url}")
            
            response = requests.delete(
                delete_url,
                timeout=30,
                proxies={'http': None, 'https': None} if 'localhost' in server_url or '127.0.0.1' in server_url else None
            )
            
            if response.status_code == 200:
                result = response.json()
                if result.get('success'):
                    deleted_info = result['data']
                    print("✓ 删除成功!")
                    print(f"已删除固件: {deleted_info['name']} v{deleted_info['version']}")
                    print(f"固件ID: {deleted_info['id']}")
                    return True
                else:
                    print(f"✗ 删除失败: {result.get('message', '未知错误')}")
                    return False
            elif response.status_code == 404:
                print(f"✗ 固件不存在: ID {firmware_id}")
                return False
            else:
                print(f"✗ 删除失败: HTTP {response.status_code}")
                try:
                    error_info = response.json()
                    print(f"错误信息: {error_info.get('message', response.text)}")
                except:
                    print(f"响应内容: {response.text}")
                return False
                
        except requests.exceptions.ConnectionError:
            print("✗ 连接失败: 无法连接到服务器")
            print("请确保服务器已启动并且地址正确")
            return False
        except requests.exceptions.Timeout:
            print("✗ 操作超时: 请检查网络连接")
            return False
        except KeyboardInterrupt:
            print("\n操作已取消")
            return False
        except Exception as e:
            print(f"✗ 删除异常: {e}")
            return False

def main():
    parser = argparse.ArgumentParser(
        description="STM32 HBox Release 管理工具 - 集成打包和刷写功能",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
使用示例:

自动构建双槽release包:
  自动构建并打包:
    python release.py auto --version 1.0.2
  
  交互式输入版本号:
    python release.py auto

发版包管理:
  列出可用的release包:
    python release.py list
  
  详细列出release包信息:
    python release.py list --verbose
  
  按时间倒序列出所有包名:
    python release.py list --time-sorted
  
  验证发版包:
    python release.py verify package.zip

刷写release包:
  直接使用包名刷写:
    python release.py flash hbox_firmware_1.0.0_a_20250613_112625.zip
  
  使用部分包名（模糊匹配）:
    python release.py flash 0.0.1_a    # 匹配版本0.0.1的槽A包
    python release.py flash 1.2.3_b    # 匹配版本1.2.3的槽B包
    python release.py flash 2.0_a      # 匹配版本2.0.x的槽A包
    python release.py flash _a_         # 匹配任意版本的槽A包
    python release.py flash 20250613   # 匹配特定日期的包
  
  使用完整路径刷写:
    python release.py flash ../releases/package.zip
  
  刷写到指定槽位:
    python release.py flash 0.0.1_a --slot A
  
  只刷写特定组件:
    python release.py flash 0.0.1_a --components application webresources
  
  交互式选择并刷写:
    python release.py flash

上传固件包到服务器:
  上传最新版本的双槽包:
    python release.py upload
  
  上传指定版本的双槽包:
    python release.py upload --version 1.0.0
  
  上传到指定服务器:
    python release.py upload --server http://192.168.1.100:3000
  
  指定固件描述:
    python release.py upload --version 1.0.0 --desc "修复网络连接问题"
  
  上传指定的固件包:
    python release.py upload --slot-a hbox_firmware_1.0.0_a_20250613_112625.zip --slot-b hbox_firmware_1.0.0_b_20250613_112625.zip

删除服务器固件:
  删除指定ID的固件:
    python release.py delete abc123def456...
  
  删除指定服务器上的固件:
    python release.py delete abc123def456... --server http://192.168.1.100:3000

支持的组件:
  application   - 应用程序固件
  webresources  - Web界面资源
  adc_mapping   - ADC通道映射数据

支持的槽位:
  A             - 主槽 (0x90000000起始)
  B             - 备用槽 (0x902B0000起始)

注意: 槽B发版包会自动重新编译Application以适配槽B地址空间
        """
    )
    
    subparsers = parser.add_subparsers(dest='command', help='可用命令')
    
    # 自动构建命令（集成auto_release_packager功能）
    auto_parser = subparsers.add_parser('auto', help='自动构建双槽release包')
    auto_parser.add_argument("--version", help="发版版本号（可选，不指定则交互式输入）")
    
    # 列出发版包命令
    list_parser = subparsers.add_parser('list', help='列出发版包')
    list_parser.add_argument("--verbose", action="store_true", help="显示详细信息")
    list_parser.add_argument("--time-sorted", action="store_true", help="按时间倒序列出所有包名")
    
    # 验证发版包命令
    verify_parser = subparsers.add_parser('verify', help='验证发版包')
    verify_parser.add_argument("package", help="要验证的发版包路径")
    
    # 刷写发版包命令
    flash_parser = subparsers.add_parser('flash', help='刷写release包')
    flash_parser.add_argument("package", nargs="?", help="要刷写的release包路径（可选，不指定则交互式选择）")
    flash_parser.add_argument("--slot", choices=["A", "B"], help="目标槽位（可选，默认使用包中指定的槽位）")
    flash_parser.add_argument("--components", nargs="+", 
                            choices=["application", "webresources", "adc_mapping"],
                            help="要刷写的组件（可选，默认刷写所有组件）")
    
    # 上传命令
    upload_parser = subparsers.add_parser('upload', help='上传固件包到服务器')
    upload_parser.add_argument("--version", help="指定版本号（可选）")
    upload_parser.add_argument("--server", help="指定服务器地址（可选）")
    upload_parser.add_argument("--desc", help="指定固件描述（可选）")
    upload_parser.add_argument("--slot-a", help="指定槽A的固件包路径（可选）")
    upload_parser.add_argument("--slot-b", help="指定槽B的固件包路径（可选）")
    
    # 删除命令
    delete_parser = subparsers.add_parser('delete', help='删除服务器固件')
    delete_parser.add_argument("firmware_id", help="要删除的固件ID")
    delete_parser.add_argument("--server", help="指定服务器地址（可选）")
    
    args = parser.parse_args()
    
    if not args.command:
        parser.print_help()
        return 1
    
    manager = ReleaseManager()
    
    try:
        if args.command == 'auto':
            # 自动构建双槽release包
            version = args.version
            if not version:
                version = input('请输入版本号（如1.0.0）: ').strip()
            
            if not version:
                print("错误: 版本号不能为空")
                return 1
            
            packages = manager.create_auto_release(version)
            if packages:
                print(f"\n✓ 成功生成 {len(packages)} 个发版包")
                return 0
            else:
                print("\n✗ 发版包生成失败")
                return 1
        
        elif args.command == 'list':
            manager.list_releases()
            return 0
        
        elif args.command == 'verify':
            if manager.verify_release_package(args.package):
                return 0
            else:
                return 1
        
        elif args.command == 'flash':
            # 刷写release包
            package_input = args.package
            
            # 如果没有指定包，交互式选择
            if not package_input:
                package_path = manager.select_release_package()
                if not package_path:
                    print("未选择release包")
                    return 1
            else:
                # 智能解析包路径
                package_path = manager.resolve_package_path(package_input)
                if not package_path:
                    print(f"错误: 找不到Release包: {package_input}")
                    print("提示: 你可以使用以下方式指定包:")
                    print("  - 直接使用包名: hbox_firmware_1.0.0_a_20250613_112625.zip")
                    print("  - 使用部分包名: 0.0.1_a, 1.2.3_b, 2.0_a, _a_, 20250613")
                    print("  - 使用相对路径: ../releases/package.zip")
                    print("  - 使用绝对路径: /path/to/package.zip")
                    print("\n可用的包:")
                    manager.list_releases()
                    return 1
            
            # 刷写包
            if manager.flash_release_package(package_path, args.slot, args.components):
                print("\n✓ Release包刷写成功")
                return 0
            else:
                print("\n✗ Release包刷写失败")
                return 1
        
        elif args.command == 'upload':
            # 上传固件包到服务器
            if args.slot_a or args.slot_b:
                # 使用指定的固件包上传
                if manager.upload_firmware_to_server(
                    slot_a_path=args.slot_a,
                    slot_b_path=args.slot_b,
                    server_url=args.server or "http://localhost:3000",
                    desc=args.desc
                ):
                    print("\n✓ 固件包上传成功")
                    return 0
                else:
                    print("\n✗ 固件包上传失败")
                    return 1
            else:
                # 上传指定版本或最新版本的双槽包
                if manager.upload_latest_packages(
                    version=args.version,
                    server_url=args.server or "http://localhost:3000",
                    desc=args.desc
                ):
                    print("\n✓ 固件包上传成功")
                    return 0
                else:
                    print("\n✗ 固件包上传失败")
                    return 1
        
        elif args.command == 'delete':
            # 删除服务器固件
            firmware_id = args.firmware_id
            server_url = args.server or "http://localhost:3000"
            
            if manager.delete_firmware_from_server(firmware_id, server_url):
                print("\n✓ 固件删除成功")
                return 0
            else:
                print("\n✗ 固件删除失败")
                return 1
        
        else:
            parser.print_help()
            return 1
    
    except KeyboardInterrupt:
        print("\n用户中断操作")
        return 1
    except Exception as e:
        print(f"错误: {e}")
        return 1

if __name__ == "__main__":
    sys.exit(main())
