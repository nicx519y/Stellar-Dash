#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
STM32 HBox Release 管理工具

集成了发版打包和刷写功能：
1. 自动构建双槽release包（使用Intel HEX分割处理）
2. 自动解压release包并刷写到设备
3. 支持槽A和槽B的完整管理
4. 包含版本管理、元数据和完整性校验
5. 支持Intel HEX文件解析和分割
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
from intelhex import IntelHex  # 添加Intel HEX处理库

# 导入统一的固件元数据常量
sys.path.append(os.path.join(os.path.dirname(__file__), '..', 'common'))
from firmware_metadata import *
from firmware_signing import (
    FirmwareSigningError,
    canonical_metadata,
    raw_signature_to_der,
    sign_metadata,
)

# 使用统一的常量定义，移除重复定义
# FIRMWARE_MAGIC, METADATA_VERSION_MAJOR, METADATA_VERSION_MINOR 等
# 现在都从 firmware_metadata.py 导入

# 保留release.py特有的常量
OPENOCD_CONFIG = "interface/stlink.cfg -f target/stm32h7x.cfg"
# Compatibility exports: existing tools and mock call sites keep using release.py.
from release_support.constants import (
    STM32_OTA_COMPONENTS, STM32_OTA_LAYOUT, LATEST_HARDWARE_VERSION,
    HARDWARE_VERSION_CODE_V2, SIGNED_METADATA_FILENAME, _SEMVER_RE,
    _SHA256_RE, _P256_SIGNATURE_RE, _TRUST_HEADER_MARKERS,
)
from release_support.metadata import (
    calculate_crc32, create_metadata_binary, verify_signed_metadata,
)
from release_support.package_validation import (
    require_v2_trust_bundle, load_firmware_release_public_key_from_trust_header,
    _parse_manifest_address, validate_stm32_ota_manifest,
    _decode_fixed_metadata_text, validate_signed_metadata_matches_manifest,
    validate_stm32_ota_package,
)
from release_support.hex_segments import HexSegmenter
from release_support.package_info import parse_package_info
from release_support.service_request import (
    get_service_token, service_token_headers, upload_form_data,
    clear_versions_request,
)


class ReleaseConfig:
    """Release工具的统一配置管理类"""
    
    def __init__(self, config_file: Path = None):
        """
        初始化配置管理器
        
        Args:
            config_file: 配置文件路径，默认为 tools/release_config.json
        """
        if config_file is None:
            # 默认配置文件路径
            self.config_file = Path(__file__).parent / "release_config.json"
        else:
            self.config_file = Path(config_file)
        
        # 默认配置
        self.default_config = {
            "server": {
                "default_url": "https://firmware.st-dash.com",
                "timeout": 300,
                "retry_count": 3
            },
            "build": {
                "default_version": "1.0.0",
                "auto_timestamp": True
            },
            "flash": {
                "default_slot": "A",
                "verify_after_flash": True
            }
        }
        
        # 加载配置
        self.config = self.load_config()
    
    def load_config(self) -> dict:
        """加载配置文件，如果不存在则创建默认配置"""
        try:
            if self.config_file.exists():
                with open(self.config_file, 'r', encoding='utf-8') as f:
                    config = json.load(f)
                print(f"✓ 已加载配置文件: {self.config_file}")
                merged = self.merge_config(self.default_config, config)
                merged.pop("admin", None)
                return merged
            else:
                # 创建默认配置文件
                self.save_config(self.default_config)
                print(f"✓ 已创建默认配置文件: {self.config_file}")
                return self.default_config.copy()
        except Exception as e:
            print(f"⚠️  加载配置文件失败: {e}，使用默认配置")
            return self.default_config.copy()
    
    def save_config(self, config: dict = None):
        """保存配置到文件"""
        if config is None:
            config = self.config
        
        try:
            # 确保配置目录存在
            self.config_file.parent.mkdir(parents=True, exist_ok=True)
            
            with open(self.config_file, 'w', encoding='utf-8') as f:
                json.dump(config, f, indent=2, ensure_ascii=False)
            
            print(f"✓ 配置已保存到: {self.config_file}")
        except Exception as e:
            print(f"✗ 保存配置文件失败: {e}")
    
    def merge_config(self, default: dict, user: dict) -> dict:
        """合并默认配置和用户配置"""
        result = default.copy()
        
        def merge_dict(base, update):
            for key, value in update.items():
                if key in base and isinstance(base[key], dict) and isinstance(value, dict):
                    merge_dict(base[key], value)
                else:
                    base[key] = value
        
        merge_dict(result, user)
        return result
    
    def get_server_url(self, custom_url: str = None) -> str:
        """获取服务器URL，优先使用自定义URL，其次使用配置的默认URL"""
        if custom_url:
            return custom_url
        return self.config["server"]["default_url"]
    
    def get_timeout(self) -> int:
        """获取请求超时时间"""
        return self.config["server"]["timeout"]
    
    def get_retry_count(self) -> int:
        """获取重试次数"""
        return self.config["server"]["retry_count"]
    
    def get_default_slot(self) -> str:
        """获取默认槽位"""
        return self.config["flash"]["default_slot"]
    
    def get_default_version(self) -> str:
        """获取默认版本号"""
        return self.config["build"]["default_version"]
    
    def should_verify_after_flash(self) -> bool:
        """是否在刷写后验证"""
        return self.config["flash"]["verify_after_flash"]
    
    def update_config(self, section: str, key: str, value: Any):
        """更新配置项"""
        if section not in self.config:
            self.config[section] = {}
        
        self.config[section][key] = value
        self.save_config()
        print(f"✓ 已更新配置: {section}.{key} = {value}")
    
    def show_config(self):
        """显示当前配置"""
        print("=" * 60)
        print("当前配置:")
        print("=" * 60)
        
        for section, items in self.config.items():
            print(f"\n[{section.upper()}]")
            for key, value in items.items():
                print(f"  {key}: {value}")
        
        print(f"\n配置文件: {self.config_file}")
        print("=" * 60)
    
    def reset_config(self):
        """重置为默认配置"""
        self.config = self.default_config.copy()
        self.save_config()
        print("✓ 配置已重置为默认值")
    
    def edit_config_interactive(self):
        """交互式编辑配置"""
        print("=" * 60)
        print("交互式配置编辑器")
        print("=" * 60)
        
        while True:
            print("\n当前配置:")
            self.show_config()
            
            print("\n可编辑的配置项:")
            print("1. 服务器默认地址")
            print("2. 请求超时时间")
            print("3. 重试次数")
            print("4. 默认槽位")
            print("5. 默认版本号")
            print("6. 刷写后验证")
            print("7. 重置为默认配置")
            print("0. 退出")
            
            try:
                choice = input("\n请选择要编辑的配置项 (0-7): ").strip()
                
                if choice == "0":
                    break
                elif choice == "1":
                    new_url = input(f"当前服务器地址: {self.get_server_url()}\n新的服务器地址: ").strip()
                    if new_url:
                        self.update_config("server", "default_url", new_url)
                elif choice == "2":
                    current_timeout = self.get_timeout()
                    new_timeout = input(f"当前超时时间: {current_timeout}秒\n新的超时时间: ").strip()
                    if new_timeout and new_timeout.isdigit():
                        self.update_config("server", "timeout", int(new_timeout))
                elif choice == "3":
                    current_retry = self.get_retry_count()
                    new_retry = input(f"当前重试次数: {current_retry}\n新的重试次数: ").strip()
                    if new_retry and new_retry.isdigit():
                        self.update_config("server", "retry_count", int(new_retry))
                elif choice == "4":
                    current_slot = self.get_default_slot()
                    new_slot = input(f"当前默认槽位: {current_slot}\n新的默认槽位 (A/B): ").strip().upper()
                    if new_slot in ["A", "B"]:
                        self.update_config("flash", "default_slot", new_slot)
                elif choice == "5":
                    current_version = self.get_default_version()
                    new_version = input(f"当前默认版本: {current_version}\n新的默认版本: ").strip()
                    if new_version:
                        self.update_config("build", "default_version", new_version)
                elif choice == "6":
                    current_verify = self.should_verify_after_flash()
                    new_verify = input(f"当前刷写后验证: {current_verify}\n是否启用刷写后验证 (y/n): ").strip().lower()
                    if new_verify in ["y", "yes", "是"]:
                        self.update_config("flash", "verify_after_flash", True)
                    elif new_verify in ["n", "no", "否"]:
                        self.update_config("flash", "verify_after_flash", False)
                elif choice == "7":
                    confirm = input("确认重置为默认配置？(y/N): ").strip().lower()
                    if confirm in ["y", "yes", "是"]:
                        self.reset_config()
                else:
                    print("无效选择，请重新输入")
                    
            except KeyboardInterrupt:
                print("\n配置编辑已取消")
                break
            except Exception as e:
                print(f"配置编辑出错: {e}")
        
        print("配置编辑完成")

# 全局配置实例
_config_instance = None

def get_config() -> ReleaseConfig:
    """获取全局配置实例"""
    global _config_instance
    if _config_instance is None:
        _config_instance = ReleaseConfig()
    return _config_instance


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
    
    def set_step(self, step, step_name=""):
        """直接设置当前步骤（不自动+1）"""
        self.current_step = step
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
        self.trust_bundle_sha256 = None
        self.firmware_release_public_key = None
        
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
        trust_header, trust_bundle_sha256 = require_v2_trust_bundle()
        release_public_key = (
            load_firmware_release_public_key_from_trust_header(
                trust_header
            )
        )
        manifest = validate_stm32_ota_package(
            package_file, release_public_key
        )
        if manifest["trust_bundle_sha256"] != trust_bundle_sha256:
            raise ValueError(
                "release包trust_bundle_sha256与当前批准bundle不一致"
            )
        
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
                extracted_manifest = json.load(f)
            if extracted_manifest != manifest:
                raise ValueError("解压后的manifest与已验证内容不一致")
            
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
        """获取组件在指定槽位的地址"""
        try:
            # 使用统一的地址映射函数
            if component_name == 'sys_assets':
                return f"0x{SYS_IMAGE_RESOURCES_ADDR:08X}"
            if component_name == 'sysbg':
                return f"0x{USER_IMAGE_RESOURCES_ADDR:08X}"
            address = get_slot_address(component_name, slot)
            return f"0x{address:08X}"
        except ValueError as e:
            raise ValueError(f"获取槽位地址失败: {e}")
    
    def flash_component(self, component_file: Path, target_address: str, component_name: str, file_type: str = None) -> bool:
        """刷写单个组件"""
        if not component_file.exists():
            print(f"错误: 组件文件不存在: {component_file}")
            return False
        
        if not self.openocd_config.exists():
            print(f"错误: OpenOCD配置文件不存在: {self.openocd_config}")
            return False
        
        # 确定文件类型
        if file_type is None:
            # 从文件扩展名判断
            file_type = "hex" if component_file.suffix.lower() == '.hex' else "bin"
        
        # 根据文件类型确定偏移地址
        if file_type.lower() == "hex":
            # HEX文件包含地址信息，使用0x00000000偏移
            flash_offset = "0x00000000"
            print(f"正在刷写 {component_name}: {component_file.name} -> {target_address} [HEX, 偏移: {flash_offset}]")
        else:
            # BIN文件需要指定目标地址作为偏移
            flash_offset = target_address
            print(f"正在刷写 {component_name}: {component_file.name} -> {target_address} [BIN, 偏移: {flash_offset}]")
        
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
        """原样刷写已验证release包；禁止改槽、删组件或现场重签。"""
        try:
            # 解压release包
            manifest, temp_dir = self.extract_release_package(package_path)

            package_slot = manifest['slot'].upper()
            if target_slot is not None and target_slot.upper() != package_slot:
                print(
                    "错误: 已签名release包禁止改写目标槽位; "
                    f"package={package_slot}, requested={target_slot.upper()}"
                )
                return False
            target_slot = package_slot
            
            print(f"开始刷写release包: {Path(package_path).name}")
            print(f"目标槽位: {target_slot}")
            
            manifest_components = manifest.get('components', [])
            components_dict = {
                component['name']: component
                for component in manifest_components
            }
            required_components = [
                component['name']
                for component in manifest_components
                if component.get('active', True)
                and component.get('size', 0) > 0
            ]
            if components is not None and (
                len(components) != len(required_components)
                or set(components) != set(required_components)
            ):
                print(
                    "错误: 已签名release包禁止遗漏或增加安全组件; "
                    f"required={required_components}, requested={components}"
                )
                return False
            components = required_components

            print(f"将原样刷写已签名组件: {components}")
            
            # 刷写各个组件
            success_count = 0
            total_count = len(components)
            
            for component_name in components:
                component_info = components_dict[component_name]
                component_file = temp_dir / component_info['file']
                
                target_address = f"0x{_parse_manifest_address(component_info['address'], component_name + '.address'):08X}"
                
                # 获取文件类型信息（从manifest或文件扩展名）
                file_type = component_info.get('file_type', None)
                
                print(f"\n[{success_count + 1}/{total_count}] 刷写组件: {component_name}")
                
                if self.flash_component(component_file, target_address, component_name, file_type):
                    success_count += 1
                else:
                    print(f"组件 {component_name} 刷写失败")
                    return False
            print(
                f"\n[{success_count + 1}/{total_count + 1}] "
                "刷写包内exact signed metadata.bin..."
            )
            metadata_file = temp_dir / manifest['metadata']['file']
            
            # 刷写元数据
            if self.flash_metadata(metadata_file):
                print("[OK] 所有组件和exact signed metadata刷写成功!")
                return True
            else:
                print("[ERROR] metadata.bin刷写失败")
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
        self.trust_bundle_sha256 = None
        self.firmware_release_public_key = None
        
        # 加载统一配置
        self.config = get_config()
        
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
    
    def get_service_token(self, token_file: str = None) -> str | None:
        """Read an administrator service token from a dedicated file."""
        return get_service_token(token_file)

    def generate_service_token_headers(self, token_file: str = None) -> dict | None:
        return service_token_headers(self.get_service_token(token_file))
    
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
    
    def build_app_with_build_py(self, slot, progress=None, progress_step=None):
        """使用build.py构建Application"""
        if progress and progress_step:
            progress.set_step(progress_step, f"构建槽{slot} Application...")
        elif not progress:
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

    def copy_adc_mapping_from_resources(self, out_dir, progress=None, progress_step=None):
        """从resources目录复制ADC映射文件"""
        if progress and progress_step:
            progress.set_step(progress_step, "复制ADC映射...")
        elif not progress:
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

    def copy_webresources_for_auto(self, out_dir, progress=None, progress_step=None):
        """复制WebResources文件"""
        if progress and progress_step:
            progress.set_step(progress_step, "复制WebResources...")
        elif not progress:
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

        makefs = self.application_dir / "www" / "makefsdata.js"
        if makefs.exists():
            self.run_cmd(["node", str(makefs)], cwd=self.application_dir / "www", check=False)
            for src in web_sources:
                if src.exists():
                    dst = out_dir / "webresources.bin"
                    shutil.copy2(src, dst)
                    if not progress:
                        print(f"成功复制WebResources: {src} -> {dst}")
                    return dst

        raise FileNotFoundError("未找到WebResources文件")

    def copy_system_assets_for_auto(self, out_dir, progress=None, progress_step=None):
        if progress and progress_step:
            progress.set_step(progress_step, "生成/复制系统图片资源...")
        elif not progress:
            print("正在生成/复制系统图片资源...")

        assets_dir = self.application_dir / "assets" / "sysicons"
        if not assets_dir.exists():
            raise FileNotFoundError(f"未找到 assets 目录: {assets_dir}")

        packer = self.tools_dir / "pack_assets.py"
        if not packer.exists():
            raise FileNotFoundError(f"未找到 assets 打包脚本: {packer}")

        assets_fix = self.tools_dir / "assets_fix.py"
        if assets_fix.exists():
            fit_arg = "320x170"
            dither_flag = True
            cfg_path = self.tools_dir / "assets_config.json"
            try:
                if cfg_path.exists():
                    with open(cfg_path, "r", encoding="utf-8") as f:
                        data = json.load(f)
                        mw = int(data.get("max_width", 320))
                        mh = int(data.get("max_height", 170))
                        fit_arg = f"{mw}x{mh}"
                        dither_flag = bool(data.get("dither", True))
            except Exception:
                pass
            args = [
                sys.executable,
                str(assets_fix),
                "--dir", str(assets_dir),
                "--out", str(assets_dir),
                "--fit", fit_arg,
                "--inplace",
            ]
            if dither_flag:
                args.append("--dither")
            self.run_cmd(args, cwd=self.project_root, check=False)

        out_bin = self.application_dir / "build" / "system_assets.bin"
        out_bin.parent.mkdir(exist_ok=True)

        cmd = [
            sys.executable,
            str(packer),
            "--icons-dir", str(assets_dir),
            "--icons-output", str(out_bin),
            "--icons-max-size", hex(SYS_IMAGE_RESOURCES_SIZE),
        ]

        result = self.run_cmd(cmd, cwd=self.project_root, check=False)
        if result.returncode != 0:
            raise RuntimeError("系统图片资源打包失败")

        if not out_bin.exists():
            raise FileNotFoundError(f"未生成 system_assets.bin: {out_bin}")

        dst = out_dir / "system_assets.bin"
        shutil.copy2(out_bin, dst)
        if not progress:
            print(f"成功复制系统图片资源: {out_bin} -> {dst}")
        return dst

    def copy_sysbg_for_auto(self, out_dir, progress=None, progress_step=None):
        if progress and progress_step:
            progress.set_step(progress_step, "生成/复制系统背景图片(sysbg)...")
        elif not progress:
            print("正在生成/复制系统背景图片(sysbg)...")

        packer = self.tools_dir / "pack_assets.py"
        if not packer.exists():
            raise FileNotFoundError(f"未找到 assets 打包脚本: {packer}")

        out_bin = self.application_dir / "build" / "sysbg.bin"
        out_bin.parent.mkdir(exist_ok=True)

        cmd = [
            sys.executable,
            str(packer),
            "--empty-sysbg-output", str(out_bin),
        ]
        result = self.run_cmd(cmd, cwd=self.project_root, check=False)
        if result.returncode != 0:
            raise RuntimeError("系统背景图片(sysbg)打包失败")

        if not out_bin.exists():
            raise FileNotFoundError(f"未生成 sysbg.bin: {out_bin}")

        dst = out_dir / "sysbg.bin"
        shutil.copy2(out_bin, dst)
        if not progress:
            print(f"成功复制系统背景图片: {out_bin} -> {dst}")
        return dst

    def make_manifest_for_auto_with_bin(self, slot, app_bin_file, adc_file, web_file, version, app_component):
        """生成manifest和与其逐字段一致的已签名metadata.bin。"""
        build_date = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
        build_timestamp = int(time.time())
        manifest = {
            "version": version,
            "slot": slot,
            "build_date": build_date,
            "build_timestamp": build_timestamp,
            "hardware_version": "2.0.0",
            "hardware_version_code": HARDWARE_VERSION,
            "ota_scope": "STM32_ONLY",
            "ch585_update": "MANUAL_INDEPENDENT_FLASH",
            "security_version": FIRMWARE_SECURITY_VERSION,
            "webresources_optional": True,
            "trust_bundle_sha256": self.trust_bundle_sha256,
            "components": []
        }
        
        # Application组件（使用从HEX分割出的BIN文件）
        manifest["components"].append({
            "name": "application",
            "file": app_bin_file.name,
            "address": app_component['address'],
            "size": app_bin_file.stat().st_size,
            "sha256": self.sha256sum_file(app_bin_file),
            "file_type": "bin",
            "active": True,
        })
        
        # V2页面由HTTPS服务器托管；保留逻辑槽位但不再发布固件资源。
        manifest["components"].append({
            "name": "webresources",
            "file": "",
            "address": "0x90100000" if slot == "A" else "0x903B0000",
            "size": 0,
            "sha256": "0" * 64,
            "file_type": "none",
            "active": False,
        })
        
        # ADC Mapping组件
        manifest["components"].append({
            "name": "adc_mapping",
            "file": adc_file.name,
            "address": "0x90280000" if slot == "A" else "0x90530000",
            "size": adc_file.stat().st_size,
            "sha256": self.sha256sum_file(adc_file),
            "file_type": "bin",
            "active": True,
        })

        metadata = create_metadata_binary(
            version=version,
            slot=slot,
            build_date=build_date,
            build_timestamp=build_timestamp,
            components=manifest["components"],
            security_version=manifest["security_version"],
            webresources_optional=True,
        )
        manifest["signature_algorithm"] = struct.unpack_from(
            "<I", metadata, FIRMWARE_SIGNATURE_ALGORITHM_OFFSET
        )[0]
        manifest["firmware_hash"] = metadata[
            FIRMWARE_HASH_OFFSET : FIRMWARE_HASH_OFFSET + 32
        ].hex()
        manifest["signature"] = metadata[
            FIRMWARE_SIGNATURE_OFFSET : FIRMWARE_SIGNATURE_OFFSET + 64
        ].hex()
        manifest["metadata"] = {
            "file": SIGNED_METADATA_FILENAME,
            "size": len(metadata),
            "sha256": hashlib.sha256(metadata).hexdigest(),
        }

        validate_stm32_ota_manifest(manifest)
        validate_signed_metadata_matches_manifest(metadata, manifest)
        return manifest, metadata

    def make_auto_release_pkg(self, slot, version, out_dir, build_timestamp, progress=None, start_step=0):
        """生成单个槽的release包（使用Intel HEX分割处理）"""
        if not progress:
            print(f"\n=== 开始生成槽{slot} release包 ===")
            print(f"使用Intel HEX分割处理模式")
        
        try:
            # 1. 构建Application
            hex_file = self.build_app_with_build_py(slot, progress, start_step + 1)
            
            # 2. 处理Intel HEX文件 - 解析和分割
            if progress:
                progress.set_step(start_step + 2, f"槽{slot}: 解析Intel HEX文件...")
            hex_segmenter = HexSegmenter()
            hex_manifest = hex_segmenter.process_hex_file(
                hex_file, 
                out_dir / "hex_components", 
                version
            )
            
            # 3. 查找application组件
            if progress:
                progress.set_step(start_step + 3, f"槽{slot}: 验证HEX组件...")
            app_component = None
            for comp in hex_manifest['components']:
                if comp['name'] == 'application' or comp['name'].startswith('application'):
                    app_component = comp
                    break
            
            if not app_component:
                print("警告: HEX文件中未找到application组件")
                raise ValueError("HEX文件中未找到有效的application组件")
            
            if not progress:
                print(f"发现application组件: {app_component['name']} ({app_component['size']:,} 字节)")
            
            # 4. 复制application BIN文件到主输出目录
            if progress:
                progress.set_step(start_step + 4, f"槽{slot}: 复制application BIN文件...")
            hex_comp_dir = out_dir / "hex_components"
            src_bin_file = hex_comp_dir / app_component['file']
            app_bin_file = out_dir / f"application_slot_{slot.lower()}.bin"
            shutil.copy2(src_bin_file, app_bin_file)
            if not progress:
                print(f"复制application BIN文件: {src_bin_file} -> {app_bin_file}")
            
            # 5. 复制其他必要的组件
            adc_file = self.copy_adc_mapping_from_resources(out_dir, progress, start_step + 5)
            web_file = None
            # 6. 生成标准manifest（使用BIN文件）
            if progress:
                progress.set_step(start_step + 7, f"槽{slot}: 生成manifest...")
            manifest, signed_metadata = self.make_manifest_for_auto_with_bin(
                slot, app_bin_file, adc_file, web_file, version, app_component
            )
            
            # 添加HEX处理信息
            manifest["hex_processed"] = True
            manifest["hex_info"] = {
                "original_hex_file": hex_file.name,
                "hex_segments": len(hex_manifest.get('components', [])) + len(hex_manifest.get('ram_segments', [])),
                "flash_components": len(hex_manifest.get('components', [])),
                "ram_segments": len(hex_manifest.get('ram_segments', [])),
                "hex_build_date": hex_manifest['build_date']
            }
            
            # 最后步骤：生成包文件
            manifest_file = out_dir / 'manifest.json'
            with open(manifest_file, 'w', encoding='utf-8') as f:
                json.dump(manifest, f, indent=2, ensure_ascii=False)
            metadata_file = out_dir / SIGNED_METADATA_FILENAME
            metadata_file.write_bytes(signed_metadata)
            
            # 7. 打包并移动到releases目录
            if progress:
                progress.set_step(start_step + 8, f"槽{slot}: 打包并移动到releases目录...")
            package_name = f'hbox_firmware_{version}_{slot.lower()}_{build_timestamp}.zip'
            temp_package_path = out_dir / package_name
            final_package_path = self.releases_dir / package_name
            
            # 创建ZIP包
            with zipfile.ZipFile(temp_package_path, 'w', zipfile.ZIP_DEFLATED) as zf:
                # 添加BIN文件
                zf.write(app_bin_file, app_bin_file.name)
                
                # 共同文件
                zf.write(adc_file, adc_file.name)
                zf.write(metadata_file, metadata_file.name)
                zf.write(manifest_file, manifest_file.name)

            # ZIP是实际发布/上传边界；移动到releases前重新从ZIP读取并严格核验。
            if self.firmware_release_public_key is None:
                raise RuntimeError(
                    "未加载批准的firmware release公钥，禁止生成正式包"
                )
            validate_stm32_ota_package(
                temp_package_path,
                self.firmware_release_public_key,
            )
            
            # 移动到最终位置
            shutil.move(temp_package_path, final_package_path)
            
            # 显示结果
            package_size = final_package_path.stat().st_size
            if not progress:
                print(f"\n[OK] 生成release包: {final_package_path.name}")
                print(f"     包大小: {package_size:,} 字节")
            
            return final_package_path
            
        except Exception as e:
            print(f"\n[ERROR] 生成槽{slot}失败: {e}")
            raise

    def create_auto_release(self, version: str) -> List[str]:
        """自动构建双槽release包（使用Intel HEX分割处理）"""
        trust_header, self.trust_bundle_sha256 = (
            require_v2_trust_bundle()
        )
        self.firmware_release_public_key = (
            load_firmware_release_public_key_from_trust_header(
                trust_header
            )
        )
        print("=== STM32 HBox 双槽Release包生成工具 ===")
        print(f"工作目录: {self.project_root}")
        print(f"输出目录: {self.releases_dir}")
        print(f"版本号: {version}")
        print(f"模式: Intel HEX分割处理")
        
        # 创建临时目录
        out_dir = self.tools_dir / 'release_temp'
        out_dir.mkdir(exist_ok=True)
        
        # 显示总体进度
        print(f"\n开始生成双槽release包...")
        total_start_time = time.time()
        
        # 生成统一的构建时间戳
        build_timestamp = datetime.now().strftime('%Y%m%d_%H%M%S')
        print(f"构建时间戳: {build_timestamp}")
        
        # 每个槽8步；OTA固定为 application/webresources/adc_mapping 三组件。
        progress = ProgressBar(16)
        
        success_count = 0
        generated_packages = []
        
        try:
            for i, slot in enumerate(['A', 'B']):
                try:
                    start_step = i * 8
                    package_path = self.make_auto_release_pkg(slot, version, out_dir, build_timestamp, progress, start_step)
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
            trust_header, trust_bundle_sha256 = require_v2_trust_bundle()
            release_public_key = (
                load_firmware_release_public_key_from_trust_header(
                    trust_header
                )
            )
            manifest = validate_stm32_ota_package(
                Path(package_file), release_public_key
            )
            if manifest["trust_bundle_sha256"] != trust_bundle_sha256:
                raise ValueError(
                    "release包trust_bundle_sha256与当前批准bundle不一致"
                )
            print("[OK] release包、组件和exact signed metadata验证通过")
            return True
                
        except Exception as e:
            print(f"[ERROR] 验证失败: {e}")
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
        result = self.flasher.flash_release_package(package_path, target_slot, components)
        
        # 如果刷写成功，尝试注册设备ID
        if result:
            print("\n" + "="*60)
            print("🔐 正在注册设备ID到服务器...")
            print("="*60)
            try:
                # 尝试注册设备ID，失败不影响整体流程
                self.register_device_id()
            except Exception as e:
                print(f"⚠️  设备ID注册出现异常: {str(e)}")
                print("   烧录已成功完成，但设备注册失败")
            print("="*60)
        
        return result
    
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
        """Parse a legacy package filename; manifests remain authoritative."""
        return parse_package_info(package_path)
    
    def upload_firmware_to_server(self, slot_a_path: str = None, slot_b_path: str = None, 
                                 server_url: str = None, 
                                 desc: str = None, 
                                 service_token_file: str = None) -> bool:
        """上传固件包到服务器"""
        
        if not slot_a_path and not slot_b_path:
            print("错误: 至少需要指定一个槽的固件包")
            return False

        # 获取服务器URL（优先使用参数，其次使用配置）
        server_url = self.config.get_server_url(server_url)

        auth_headers = self.generate_service_token_headers(service_token_file)
        if not auth_headers:
            return False

        try:
            trust_header, self.trust_bundle_sha256 = (
                require_v2_trust_bundle()
            )
            release_public_key = (
                load_firmware_release_public_key_from_trust_header(
                    trust_header
                )
            )
        except (OSError, RuntimeError, ValueError) as exc:
            print(f"错误: 无法加载批准的release信任bundle: {exc}")
            return False
        
        # 上传元数据只能来自包内manifest；文件名和表单都不是信任来源。
        package_paths = {}
        if slot_a_path:
            package_paths["slotA"] = Path(slot_a_path)
        if slot_b_path:
            package_paths["slotB"] = Path(slot_b_path)

        package_manifests = {}
        for field_name, package_file in package_paths.items():
            slot_label = "槽A" if field_name == "slotA" else "槽B"
            if not package_file.exists():
                print(f"错误: {slot_label}文件不存在: {package_file}")
                return False
            try:
                manifest = validate_stm32_ota_package(
                    package_file, release_public_key
                )
                if (
                    manifest["trust_bundle_sha256"]
                    != self.trust_bundle_sha256
                ):
                    raise ValueError(
                        "包内trust_bundle_sha256与当前批准bundle不一致"
                    )
                package_manifests[field_name] = manifest
            except (OSError, ValueError, zipfile.BadZipFile, json.JSONDecodeError) as exc:
                print(f"错误: {slot_label}包不符合STM32三组件OTA边界: {exc}")
                return False

        expected_slot_by_field = {"slotA": "A", "slotB": "B"}
        for field_name, manifest in package_manifests.items():
            expected_slot = expected_slot_by_field[field_name]
            if manifest["slot"].upper() != expected_slot:
                print(
                    f"错误: {field_name}上传字段包含槽{manifest['slot']}包，"
                    f"必须是槽{expected_slot}"
                )
                return False

        versions = {manifest["version"] for manifest in package_manifests.values()}
        hardware_versions = {
            manifest["hardware_version"] for manifest in package_manifests.values()
        }
        if len(versions) != 1 or len(hardware_versions) != 1:
            print("错误: 双槽包的version和hardware_version必须完全一致")
            return False

        version = next(iter(versions))
        hardware_version = next(iter(hardware_versions))
        if hardware_version != LATEST_HARDWARE_VERSION:
            print(f"错误: 本发布工具只允许上传V2硬件包 ({LATEST_HARDWARE_VERSION})")
            return False

        if not desc:
            desc = f"自动上传的固件包，版本 {version}"

        print("=" * 60)
        print("上传固件包到服务器")
        print("=" * 60)
        print(f"服务器地址: {server_url}")
        print(f"硬件版本: {hardware_version}")
        print(f"版本号: {version}")
        print(f"信任bundle: {trust_header}")
        print(f"信任bundle SHA-256: {self.trust_bundle_sha256}")
        print(f"描述: {desc}")
        print("🔐 认证方式: scoped service token")

        files_to_upload = {}
        for field_name, package_file in package_paths.items():
            files_to_upload[field_name] = (
                package_file.name,
                open(package_file, "rb"),
                "application/zip",
            )
            slot_label = "槽A" if field_name == "slotA" else "槽B"
            print(f"{slot_label}包: {package_file.name} ({package_file.stat().st_size / 1024 / 1024:.1f} MB)")
        
        print()
        
        try:
            # 准备上传数据
            form_data = upload_form_data(version, desc, hardware_version)
            
            upload_url = f"{server_url}/api/firmwares/upload"
            print(f"正在上传到: {upload_url}")
            
            # 发送POST请求
            response = requests.post(
                upload_url,
                data=form_data,
                files=files_to_upload,
                headers=auth_headers,
                timeout=self.config.get_timeout(),  # 使用配置的超时时间
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
    
    def upload_latest_packages(self, version: str = None, server_url: str = None, 
                             desc: str = None, service_token_file: str = None) -> bool:
        """上传最新的双槽包到服务器"""
        
        # 获取服务器URL（优先使用参数，其次使用配置）
        server_url = self.config.get_server_url(server_url)
        
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
            desc=desc,
            service_token_file=service_token_file
        )

    # ==================== 固件删除功能 ====================
    
    def delete_firmware_from_server(self, firmware_id: str, server_url: str = None,
                                    service_token_file: str = None) -> bool:
        """从服务器删除指定ID的固件"""
        
        # 获取服务器URL（优先使用参数，其次使用配置）
        server_url = self.config.get_server_url(server_url)
        
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
                timeout=self.config.get_timeout(),
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
            
            auth_headers = self.generate_service_token_headers(
                service_token_file
            )
            if not auth_headers:
                return False
            
            response = requests.delete(
                delete_url,
                timeout=30,
                headers=auth_headers,
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
            elif response.status_code == 401:
                print("✗ 删除失败: 需要管理员认证或认证失败 (401 Unauthorized)")
                try:
                    error_info = response.json()
                    print(f"错误信息: {error_info.get('message', response.text)}")
                except:
                    print(f"响应内容: {response.text}")
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

    def clear_firmware_versions_from_server(self, target_version: str, server_url: str = None, 
                                           service_token_file: str = None) -> bool:
        """仅清空V2硬件上指定版本及之前的固件。"""
        
        # 获取服务器URL（优先使用参数，其次使用配置）
        server_url = self.config.get_server_url(server_url)
        
        print("=" * 60)
        print("清空服务器固件版本")
        print("=" * 60)
        print(f"服务器地址: {server_url}")
        print(f"硬件版本: {LATEST_HARDWARE_VERSION}")
        print(f"目标版本: {target_version} (包含此版本及之前的所有版本)")
        
        auth_headers = self.generate_service_token_headers(service_token_file)
        if not auth_headers:
            return False
        print("🔐 认证方式: scoped service token")
        print()
        
        try:
            # 先获取当前所有固件列表，以便用户确认
            list_url = f"{server_url}/api/firmwares"
            print(f"正在获取固件列表: {list_url}")
            
            response = requests.get(
                list_url,
                timeout=self.config.get_timeout(),
                proxies={'http': None, 'https': None} if 'localhost' in server_url or '127.0.0.1' in server_url else None
            )
            
            if response.status_code != 200:
                print(f"✗ 获取固件列表失败: HTTP {response.status_code}")
                return False
            
            result = response.json()
            if not result.get('success'):
                print(f"✗ 获取固件列表失败: {result.get('message', '未知错误')}")
                return False
            
            firmwares = [
                firmware for firmware in result['data']
                if firmware.get('hardwareVersion') == LATEST_HARDWARE_VERSION
            ]
            if not firmwares:
                print(f"服务器上没有硬件 {LATEST_HARDWARE_VERSION} 的固件，无需清空")
                return True
            
            # 调试：显示第一个固件的结构（如果存在）
            if firmwares and len(firmwares) > 0:
                print(f"调试: 第一个固件的数据结构: {list(firmwares[0].keys())}")
            
            # 显示当前所有固件
            print("当前服务器上的固件:")
            firmware_versions = []
            for firmware in firmwares:
                # 安全地获取固件信息，处理可能缺失的字段
                name = firmware.get('name', 'Unknown')
                version = firmware.get('version', 'Unknown')
                firmware_id = firmware.get('id', firmware.get('_id', 'Unknown'))
                
                # 安全地截取ID的前8位
                id_display = str(firmware_id)[:8] if firmware_id != 'Unknown' else 'Unknown'
                print(f"  - {name} v{version} (ID: {id_display}...)")
                firmware_versions.append(version)
            
            print()
            
            # 显示将要删除的版本范围
            print(f"将要删除版本 <= {target_version} 的所有固件")
            print("注意: 这个操作会删除指定版本及所有更早的版本!")
            print()
            
            # 二次确认
            confirm1 = input(f"确认要清空版本 <= {target_version} 的所有固件吗？(y/N): ").strip().lower()
            if confirm1 not in ['y', 'yes', '是']:
                print("操作已取消")
                return False
            
            confirm2 = input("这个操作不可恢复！请再次确认 (yes/N): ").strip().lower()
            if confirm2 != 'yes':
                print("操作已取消")
                return False
            
            print()
            
            # 调用清空接口
            clear_url = f"{server_url}/api/firmwares/clear-up-to-version"
            print(f"正在清空固件: {clear_url}")
            
            request_data = clear_versions_request(target_version, LATEST_HARDWARE_VERSION)
            
            # 合并认证请求头和Content-Type
            headers = {
                'Content-Type': 'application/json',
                **auth_headers
            }
            
            response = requests.post(
                clear_url,
                json=request_data,
                headers=headers,
                timeout=self.config.get_timeout(),
                proxies={'http': None, 'https': None} if 'localhost' in server_url or '127.0.0.1' in server_url else None
            )
            
            if response.status_code == 200:
                result = response.json()
                if result.get('success'):
                    data = result['data']
                    print("✓ 清空成功!")
                    print(f"目标版本: {data['targetVersion']}")
                    print(f"删除数量: {data['deletedCount']} 个固件")
                    
                    if data['deletedFirmwares']:
                        print("已删除的固件:")
                        for deleted in data['deletedFirmwares']:
                            # 安全地获取删除的固件信息
                            name = deleted.get('name', 'Unknown')
                            version = deleted.get('version', 'Unknown')
                            deleted_id = deleted.get('id', deleted.get('_id', 'Unknown'))
                            
                            # 安全地截取ID的前8位
                            id_display = str(deleted_id)[:8] if deleted_id != 'Unknown' else 'Unknown'
                            print(f"  - {name} v{version} (ID: {id_display}...)")
                    
                    print(f"清空时间: {data['clearTime']}")
                    return True
                else:
                    print(f"✗ 清空失败: {result.get('message', '未知错误')}")
                    return False
            elif response.status_code == 401:
                print("✗ 清空失败: 管理员认证失败")
                try:
                    error_info = response.json()
                    print(f"错误信息: {error_info.get('message', 'Authentication failed')}")
                except:
                    print("请检查服务令牌文件及令牌有效期")
                return False
            else:
                print(f"✗ 清空失败: HTTP {response.status_code}")
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
            print(f"✗ 清空异常: {e}")
            return False

    # ==================== 快速构建和刷写功能 ====================
    
    def build_and_flash_slot_a_app(self) -> bool:
        """快速构建槽A的application并直接刷写到设备"""
        print(
            "错误: V2 quick现场构建/重签/刷写已禁用；请先用auto生成"
            "exact signed release包，再使用flash原样刷写"
        )
        return False

        print("=== 快速构建并刷写槽A Application ===")
        print(f"工作目录: {self.project_root}")
        
        try:
            # 1. 构建槽A的Application
            print("\n[1/4] 构建槽A Application...")
            hex_file = self.build_app_with_build_py('A')
            print(f"✓ 构建完成: {hex_file}")
            
            # 2. 使用Intel HEX分割处理，提取application组件
            print("\n[2/4] 解析Intel HEX文件...")
            
            # 创建临时目录
            temp_dir = self.tools_dir / 'quick_flash_temp'
            temp_dir.mkdir(exist_ok=True)
            
            try:
                # 使用HEX分割器解析文件
                hex_segmenter = HexSegmenter()
                hex_manifest = hex_segmenter.process_hex_file(
                    hex_file, 
                    temp_dir / "hex_components", 
                    "dev-build"
                )
                
                # 查找application组件
                app_component = None
                for comp in hex_manifest['components']:
                    if comp['name'] == 'application' or comp['name'].startswith('application'):
                        app_component = comp
                        break
                
                if not app_component:
                    print("错误: HEX文件中未找到application组件")
                    return False
                
                print(f"✓ 发现application组件: {app_component['name']} ({app_component['size']:,} 字节)")
                print(f"  地址: {app_component.get('original_address', 'N/A')} -> {app_component['address']}")
                
                # 3. 直接刷写application组件
                print("\n[3/4] 刷写application到槽A...")
                
                # application组件文件路径
                component_file = temp_dir / "hex_components" / app_component['file']
                target_address = "0x90000000"  # 槽A的application地址
                
                if not self.flasher.flash_component(component_file, target_address, "application", "bin"):
                    print("\n✗ Application刷写失败")
                    return False
                
                print(f"✓ Application刷写成功 ({app_component['size']:,} 字节)")
                
                # 4. 生成并刷写元数据
                print("\n[4/4] 生成并刷写槽A元数据...")
                
                # 准备元数据组件信息
                components_list = [{
                    'name': 'application',
                    'file': app_component['file'],
                    'address': target_address,
                    'size': app_component['size'],
                    'sha256': app_component['sha256'],
                    'active': True
                }]
                
                # 生成元数据二进制
                metadata_binary = create_metadata_binary(
                    version="dev-build",
                    slot="A",
                    build_date=datetime.now().strftime('%Y-%m-%d %H:%M:%S'),
                    components=components_list
                )
                
                # 写入临时元数据文件
                metadata_file = temp_dir / "metadata.bin"
                with open(metadata_file, 'wb') as f:
                    f.write(metadata_binary)
                
                print(f"✓ 生成元数据文件: {len(metadata_binary):,} 字节")
                
                # 刷写元数据
                if self.flasher.flash_metadata(metadata_file):
                    print("✓ 元数据刷写成功")
                    
                    print("\n" + "="*50)
                    print("✓ 槽A Application构建并刷写成功!")
                    print(f"刷写内容:")
                    print(f"  - Application: {target_address} ({app_component['size']:,} 字节)")
                    print(f"  - 元数据: 0x90570000 ({len(metadata_binary):,} 字节)")
                    print(f"  - 版本: dev-build (槽A)")
                    print("="*50)
                    return True
                else:
                    print("✗ 元数据刷写失败")
                    return False
                    
            finally:
                # 清理临时目录
                if temp_dir.exists():
                    try:
                        shutil.rmtree(temp_dir)
                        print(f"\n已清理临时目录: {temp_dir}")
                    except Exception as e:
                        print(f"清理临时目录失败: {e}")
                        
        except Exception as e:
            print(f"\n✗ 操作失败: {e}")
            return False

    # ==================== 设备ID读取功能 ====================
    
    def read_device_ids(self) -> bool:
        """读取STM32设备唯一ID并计算设备ID哈希"""
        print("=== STM32 设备ID读取工具 ===")
        print(f"工作目录: {self.project_root}")
        
        if not self.openocd_config.exists():
            print(f"错误: OpenOCD配置文件不存在: {self.openocd_config}")
            return False
        
        try:
            print("\n正在连接设备并读取唯一ID...")
            
            # STM32H750的唯一ID存储地址
            unique_id_address = "0x1FF1E800"
            unique_id_size = 12  # 96位 = 12字节
            
            # 构建OpenOCD命令读取唯一ID
            cmd = [
                "openocd",
                "-d0",
                "-f", "openocd_configs/ST-LINK-QSPIFLASH.cfg",
                "-c", "init",
                "-c", "halt",
                "-c", "reset init",
                "-c", f"dump_image unique_id_temp.bin {unique_id_address} {unique_id_size}",
                "-c", "shutdown"
            ]
            
            print(f"执行命令: {' '.join(cmd)}")
            
            # 执行OpenOCD命令
            result = subprocess.run(
                cmd,
                cwd=self.tools_dir,
                capture_output=True,
                text=True,
                timeout=30
            )
            
            if result.returncode != 0:
                print("✗ OpenOCD命令执行失败")
                if result.stderr:
                    print(f"错误信息: {result.stderr}")
                return False
            
            # 读取导出的唯一ID数据
            unique_id_file = self.tools_dir / "unique_id_temp.bin"
            if not unique_id_file.exists():
                print("✗ 未找到导出的唯一ID数据文件")
                return False
            
            try:
                with open(unique_id_file, 'rb') as f:
                    uid_data = f.read()
                
                if len(uid_data) != 12:
                    print(f"✗ 唯一ID数据长度错误: 期望12字节，实际{len(uid_data)}字节")
                    return False
                
                # 解析96位唯一ID (3个32位字)
                uid_word0 = struct.unpack('<I', uid_data[0:4])[0]
                uid_word1 = struct.unpack('<I', uid_data[4:8])[0]
                uid_word2 = struct.unpack('<I', uid_data[8:12])[0]
                
                # 格式化原始唯一ID (格式: XXXXXXXX-XXXXXXXX-XXXXXXXX)
                raw_unique_id = f"{uid_word0:08X}-{uid_word1:08X}-{uid_word2:08X}"
                
                # 计算设备ID哈希 (与固件中的算法一致)
                device_id_hash = self.calculate_device_id_hash(uid_word0, uid_word1, uid_word2)
                
                # 显示结果
                print("\n" + "="*60)
                print("✓ STM32 设备ID读取成功!")
                print("="*60)
                print(f"设备型号: STM32H750")
                print(f"唯一ID地址: {unique_id_address}")
                print()
                print("原始唯一ID (96位):")
                print(f"  Word0: 0x{uid_word0:08X}")
                print(f"  Word1: 0x{uid_word1:08X}")
                print(f"  Word2: 0x{uid_word2:08X}")
                print(f"  格式化: {raw_unique_id}")
                print()
                print("设备ID哈希 (用于网络认证):")
                print(f"  Device ID: {device_id_hash}")
                print()
                print("数据解读:")
                print(f"  - 原始ID用于内部调试和硬件识别")
                print(f"  - 设备ID哈希用于网络安全验证")
                print(f"  - 设备ID通过多轮哈希算法生成，无法逆推原始ID")
                print("="*60)
                
                return True
                
            finally:
                # 清理临时文件
                if unique_id_file.exists():
                    try:
                        unique_id_file.unlink()
                        print(f"\n已清理临时文件: {unique_id_file}")
                    except Exception as e:
                        print(f"清理临时文件失败: {e}")
                        
        except subprocess.TimeoutExpired:
            print("✗ OpenOCD操作超时")
            return False
        except FileNotFoundError:
            print("✗ 未找到OpenOCD工具，请确保已安装并在PATH中")
            return False
        except Exception as e:
            print(f"✗ 读取设备ID失败: {e}")
            return False
    
    def calculate_device_id_hash(self, uid_word0: int, uid_word1: int, uid_word2: int) -> str:
        """计算设备ID哈希 (与固件中的算法保持一致)"""
        
        # 与 utils.c 中相同的哈希算法
        # 盐值常量
        salt1 = 0x48426F78  # "HBox"
        salt2 = 0x32303234  # "2024"
        
        # 质数常量
        prime1 = 0x9E3779B9  # 黄金比例的32位表示
        prime2 = 0x85EBCA6B  # 另一个质数
        prime3 = 0xC2B2AE35  # 第三个质数
        
        # 第一轮哈希
        hash1 = (uid_word0 ^ salt1) & 0xFFFFFFFF
        hash1 = ((hash1 << 13) | ((hash1 & 0xFFFFFFFF) >> 19)) & 0xFFFFFFFF  # 左循环移位13位
        hash1 = (hash1 * prime1) & 0xFFFFFFFF
        hash1 = (hash1 ^ uid_word1) & 0xFFFFFFFF
        
        # 第二轮哈希
        hash2 = (uid_word1 ^ salt2) & 0xFFFFFFFF
        hash2 = ((hash2 << 17) | ((hash2 & 0xFFFFFFFF) >> 15)) & 0xFFFFFFFF  # 左循环移位17位
        hash2 = (hash2 * prime2) & 0xFFFFFFFF
        hash2 = (hash2 ^ uid_word2) & 0xFFFFFFFF
        
        # 第三轮哈希
        hash3 = (uid_word2 ^ ((salt1 + salt2) & 0xFFFFFFFF)) & 0xFFFFFFFF
        hash3 = ((hash3 << 21) | ((hash3 & 0xFFFFFFFF) >> 11)) & 0xFFFFFFFF  # 左循环移位21位
        hash3 = (hash3 * prime3) & 0xFFFFFFFF
        hash3 = (hash3 ^ hash1) & 0xFFFFFFFF
        
        # 最终组合
        final_hash1 = (hash1 ^ hash2) & 0xFFFFFFFF
        final_hash2 = (hash2 ^ hash3) & 0xFFFFFFFF
        
        # 转换为16位十六进制字符串 (64位哈希)
        device_id = f"{final_hash1:08X}{final_hash2:08X}"
        
        return device_id
    
    def flash_web_resources(self, slot: str = "A") -> bool:
        """只刷写web资源到指定槽位"""
        try:
            print(f"正在准备刷写Web资源到槽{slot}...")
            
            # 查找webresources文件
            web_sources = [
                self.resources_dir / "webresources.bin",
                self.application_dir / "Libs" / "httpd" / "ex_fsdata.bin"
            ]
            
            web_file = None
            for src in web_sources:
                if src.exists():
                    web_file = src
                    break
            
            if not web_file:
                print("错误: 未找到Web资源文件")
                print("请确保以下文件之一存在:")
                for src in web_sources:
                    print(f"  - {src}")
                return False
            
            print(f"找到Web资源文件: {web_file}")
            print(f"文件大小: {web_file.stat().st_size:,} 字节")
            
            # 确定目标地址
            target_address = "0x90100000" if slot == "A" else "0x903B0000"
            print(f"目标地址: {target_address}")
            
            # 创建刷写器并刷写
            flasher = ReleaseFlasher(self.project_root)
            
            print("开始刷写Web资源...")
            if flasher.flash_component(web_file, target_address, "webresources", "bin"):
                print(f"✓ Web资源刷写到槽{slot}成功!")
                return True
            else:
                print(f"✗ Web资源刷写到槽{slot}失败!")
                return False
                
        except Exception as e:
            print(f"刷写Web资源时发生错误: {e}")
            return False

    def flash_system_assets(self) -> bool:
        try:
            icons_dir = self.application_dir / "assets" / "sysicons"
            if not icons_dir.exists():
                print(f"错误: 未找到 sysicons 目录: {icons_dir}")
                return False

            packer = self.tools_dir / "pack_assets.py"
            if not packer.exists():
                print(f"错误: 未找到 assets 打包脚本: {packer}")
                return False

            out_file = self.application_dir / "build" / "system_assets.bin"
            out_file.parent.mkdir(exist_ok=True)

            cmd = [
                sys.executable,
                str(packer),
                "--icons-dir", str(icons_dir),
                "--icons-output", str(out_file),
                "--icons-max-size", hex(SYS_IMAGE_RESOURCES_SIZE),
            ]
            result = subprocess.run(cmd, cwd=self.project_root, capture_output=True, text=True)
            if result.returncode != 0:
                print("错误: assets 打包失败")
                if result.stdout:
                    print(result.stdout)
                if result.stderr:
                    print(result.stderr)
                return False

            if not out_file.exists():
                print(f"错误: 未生成 system_assets.bin: {out_file}")
                return False

            print(f"找到系统图片资源包: {out_file} ({out_file.stat().st_size:,} 字节)")
            target_address = f"0x{SYS_IMAGE_RESOURCES_ADDR:08X}"
            print(f"目标地址: {target_address}")

            flasher = ReleaseFlasher(self.project_root)
            return flasher.flash_component(out_file, target_address, "sys_assets", "bin")

        except Exception as e:
            print(f"刷写系统图片资源时发生错误: {e}")
            return False

    def register_device_id(self, server_url: str = None,
                           service_token_file: str = None) -> bool:
        """
        注册设备ID到服务器（使用管理员认证）
        
        Args:
            server_url: 服务器URL
            service_token_file: 服务令牌文件路径
        
        Returns:
            bool: 注册是否成功
        """
        import requests
        import subprocess
        
        try:
            # 获取服务器URL（优先使用参数，其次使用配置）
            server_url = self.config.get_server_url(server_url)
            
            auth_headers = self.generate_service_token_headers(
                service_token_file
            )
            if not auth_headers:
                return False
            print("🔐 认证方式: scoped service token")
            
            # 直接读取设备唯一ID
            if not self.openocd_config.exists():
                print(f"❌ OpenOCD配置文件不存在: {self.openocd_config}")
                return False
            
            # STM32H750的唯一ID存储地址
            unique_id_address = "0x1FF1E800"
            unique_id_size = 12  # 96位 = 12字节
            
            # 构建OpenOCD命令读取唯一ID
            temp_file = self.tools_dir / "device_id_temp.bin"
            cmd = [
                "openocd",
                "-d0",
                "-f", "openocd_configs/ST-LINK-QSPIFLASH.cfg",
                "-c", "init",
                "-c", "halt",
                "-c", "reset init",
                "-c", f"dump_image {temp_file.name} {unique_id_address} {unique_id_size}",
                "-c", "shutdown"
            ]
            
            print("🔌 正在连接设备...")
            
            # 执行OpenOCD命令
            result = subprocess.run(
                cmd,
                cwd=self.tools_dir,
                capture_output=True,
                text=True,
                timeout=self.config.get_timeout()
            )
            
            if result.returncode != 0:
                print("❌ 设备连接失败")
                if result.stderr:
                    print(f"   错误: {result.stderr}")
                return False
            
            # 检查临时文件是否生成
            if not temp_file.exists():
                print("❌ 未找到设备唯一ID数据")
                return False
            
            # 读取12字节的唯一ID数据
            with open(temp_file, 'rb') as f:
                data = f.read()
            
            if len(data) != 12:
                print(f"❌ 设备ID数据长度错误: {len(data)} 字节，期望 12 字节")
                return False
            
            # 解析三个32位字
            uid_word0 = int.from_bytes(data[0:4], byteorder='little')
            uid_word1 = int.from_bytes(data[4:8], byteorder='little')
            uid_word2 = int.from_bytes(data[8:12], byteorder='little')
            
            # 格式化原始唯一ID
            raw_unique_id = f"{uid_word0:08X}-{uid_word1:08X}-{uid_word2:08X}"
            
            # 计算设备ID哈希
            device_id_hash = self.calculate_device_id_hash(uid_word0, uid_word1, uid_word2)
            
            print(f"📱 设备原始唯一ID: {raw_unique_id}")
            print(f"🔐 设备哈希ID: {device_id_hash}")
            print(f"🌐 注册到服务器: {server_url}")
            
            # 构建注册数据
            register_data = {
                "rawUniqueId": raw_unique_id,
                "deviceId": device_id_hash,
                "deviceName": f"HBox-{device_id_hash[:8]}"
            }
            
            # 生成认证请求头
            headers = {
                'Content-Type': 'application/json',
                **auth_headers
            }
            
            # 发送注册请求
            register_url = f"{server_url}/api/device/register"
            
            print("📡 正在发送注册请求...")
            response = requests.post(register_url, json=register_data, headers=headers, timeout=self.config.get_timeout())
            
            if response.status_code in [200, 201]:
                result = response.json()
                if result.get('success'):
                    if result.get('data', {}).get('existed'):
                        print("✅ 设备已存在于服务器，无需重复注册")
                    else:
                        print("✅ 设备注册成功！")
                    
                    device_info = result.get('data', {})
                    print(f"   设备名称: {device_info.get('deviceName', 'N/A')}")
                    print(f"   设备ID: {device_info.get('deviceId', 'N/A')}")
                    print(f"   注册时间: {device_info.get('registerTime', 'N/A')}")
                    print(f"   注册管理员: {device_info.get('registeredBy', 'N/A')}")
                    return True
                else:
                    print(f"❌ 注册失败: {result.get('message', 'Unknown error')}")
                    if result.get('errNo') == 1:
                        print(f"   详细错误: {result.get('errorMessage', 'Unknown error')}")
                    return False
            elif response.status_code == 401:
                print("❌ 管理员认证失败")
                try:
                    error_info = response.json()
                    print(f"   错误信息: {error_info.get('message', 'Authentication failed')}")
                except:
                    print("   请检查服务令牌文件及令牌有效期")
                return False
            else:
                print(f"❌ 服务器响应错误: HTTP {response.status_code}")
                try:
                    error_info = response.json()
                    print(f"   错误信息: {error_info.get('message', 'Unknown error')}")
                except:
                    print(f"   响应内容: {response.text}")
                return False
                
        except requests.exceptions.ConnectionError:
            print(f"❌ 无法连接到服务器: {server_url}")
            print("   请确保服务器已启动并且网络连接正常")
            return False
        except subprocess.TimeoutExpired:
            print("❌ 设备连接超时")
            print("   请检查设备是否已连接并进入烧录模式")
            return False
        except Exception as e:
            print(f"❌ 注册异常: {e}")
            return False
        finally:
            # 清理临时文件
            temp_file = self.tools_dir / "device_id_temp.bin"
            if temp_file.exists():
                try:
                    temp_file.unlink()
                except:
                    pass

    def build_and_flash_app_and_web(self, slot: str = "A") -> bool:
        """快捷编译 application 并烧录 application 和 web resource"""
        print(
            "错误: V2 appweb现场构建/重签/刷写已禁用；Hosted WebConfig"
            "不写入QSPI，请使用auto + flash"
        )
        return False

        print(f"=== 快捷编译并烧录 Application + Web Resource (槽{slot}) ===")
        print(f"工作目录: {self.project_root}")
        try:
            # 1. 编译 application
            print(f"\n[1/4] 编译槽{slot} Application...")
            hex_file = self.build_app_with_build_py(slot)
            print(f"✓ 构建完成: {hex_file}")

            # 2. 解析 HEX 并烧录 application
            print(f"\n[2/4] 解析HEX并烧录 Application 到槽{slot}...")
            temp_dir = self.tools_dir / f'appweb_flash_temp_{slot}'; temp_dir.mkdir(exist_ok=True)
            try:
                hex_segmenter = HexSegmenter()
                hex_manifest = hex_segmenter.process_hex_file(hex_file, temp_dir / "hex_components", "dev-build")
                app_component = None
                for comp in hex_manifest['components']:
                    if comp['name'] == 'application' or comp['name'].startswith('application'):
                        app_component = comp
                        break
                if not app_component:
                    print("错误: HEX文件中未找到application组件")
                    return False
                print(f"✓ 发现application组件: {app_component['name']} ({app_component['size']:,} 字节)")
                component_file = temp_dir / "hex_components" / app_component['file']
                target_address = "0x90000000" if slot == "A" else "0x902B0000"
                if not self.flasher.flash_component(component_file, target_address, "application", "bin"):
                    print("\n✗ Application刷写失败")
                    return False
                print(f"✓ Application刷写成功 ({app_component['size']:,} 字节)")
            finally:
                if temp_dir.exists():
                    try:
                        shutil.rmtree(temp_dir)
                        print(f"\n已清理临时目录: {temp_dir}")
                    except Exception as e:
                        print(f"清理临时目录失败: {e}")

            # 3. 烧录 web resource
            print(f"\n[3/4] 烧录Web资源到槽{slot}...")
            if not self.flash_web_resources(slot):
                print(f"✗ Web资源刷写到槽{slot}失败!")
                return False

            # 4. 烧录系统图片资源
            print(f"\n[4/4] 烧录系统图片资源(assets)...")
            if not self.flash_system_assets():
                print("✗ 系统图片资源刷写失败!")
                return False

            print(f"\n=== Application + Web Resource + Sys Assets (槽{slot}) 编译和烧录全部成功! ===")
            return True
        except Exception as e:
            print(f"\n✗ 操作失败: {e}")
            return False

def main():
    parser = argparse.ArgumentParser(
        description="STM32 HBox Release 管理工具 - 集成打包和刷写功能",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
使用示例:

自动构建双槽release包:
  自动构建并打包（传统模式）:
    python release.py auto --version 1.0.2
  
  自动构建并打包（HEX增强模式）:
    python release.py auto --version 1.0.2 --process-hex
  
  交互式输入版本号:
    python release.py auto

快速开发调试:
  快速构建槽A的application并直接刷写到设备:
    python release.py quick
  
  说明:
    - 自动构建槽A的application
    - 使用Intel HEX分割处理，只提取application组件
    - 直接刷写到槽A地址(0x90000000)
    - 生成并刷写槽A的元数据到0x90570000
    - 适合开发调试阶段快速验证代码

  只刷写Web资源到设备:
    python release.py web
    python release.py web --slot A
    python release.py web --slot B
  
  说明:
    - 直接刷写Web资源文件到指定槽位
    - 槽A地址: 0x90100000
    - 槽B地址: 0x903B0000
    - 适合Web界面更新，无需重新编译固件
    - 自动查找webresources.bin或ex_fsdata.bin文件

设备信息读取:
  读取STM32设备唯一ID并计算设备ID哈希:
    python release.py device-id
  
  说明:
    - 通过OpenOCD连接设备读取96位硬件唯一ID
    - 显示原始唯一ID的3个32位字和格式化字符串
    - 计算安全的设备ID哈希值(用于网络认证)
    - 哈希算法与固件中utils.c的get_device_id_hash()保持一致
    - 需要设备通过ST-Link连接并处于可访问状态

设备注册:
  注册设备ID到服务器（需要管理员认证）:
    python release.py register
  
  注册到指定服务器:
    python release.py register --server http://192.168.1.100:3000
  
  指定服务令牌文件:
    python release.py register --service-token-file /run/secrets/hbox-service-token
  
  使用环境变量:
    export HBOX_ADMIN_SERVICE_TOKEN_FILE=/run/secrets/hbox-service-token
    python release.py register
  
  说明:
    - 需要具有 device.manage 范围的服务令牌
    - 服务令牌只从专用文件读取

配置管理:
  显示当前配置:
    python release.py config
    python release.py config --show
  
  交互式编辑配置:
    python release.py config --edit
  
  重置为默认配置:
    python release.py config --reset
  
  快速设置配置项:
    python release.py config --set-server http://192.168.1.100:3000
    python release.py config --set-timeout 300
    python release.py config --set-retry 3
  
  说明:
    - 配置文件保存在 tools/release_config.json
    - 支持服务器地址、超时时间等配置
    - 所有网络相关命令都会使用配置的默认值
    - 命令行参数优先级高于配置文件

Intel HEX文件处理（测试功能）:
  处理HEX文件并分割为多个组件:
    python release.py hex application_slot_a.hex
    python release.py hex application_slot_a.hex --output ./hex_out --version 1.0.0
  
  分析HEX文件结构:
    python release.py hex build/application_slot_a.hex -o analysis

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

  指定服务令牌文件:
    python release.py upload --service-token-file /run/secrets/hbox-service-token
  
  使用环境变量指定服务令牌文件:
    export HBOX_ADMIN_SERVICE_TOKEN_FILE=/run/secrets/hbox-service-token
    python release.py upload
  
  说明:
    - 上传固件需要具有 firmware.manage 范围的服务令牌

删除服务器固件:
  删除指定ID的固件:
    python release.py delete abc123def456...
  
  删除指定服务器上的固件:
    python release.py delete abc123def456... --server http://192.168.1.100:3000

清空服务器固件版本:
  清空指定版本及之前的所有固件:
    python release.py clear 1.0.5
  
  清空指定服务器上的固件:
    python release.py clear 1.0.5 --server http://192.168.1.100:3000
  
  指定服务令牌文件:
    python release.py clear 1.0.5 --service-token-file /run/secrets/hbox-service-token
  
  使用环境变量指定服务令牌文件:
    export HBOX_ADMIN_SERVICE_TOKEN_FILE=/run/secrets/hbox-service-token
    python release.py clear 1.0.5
  
  说明: 
    - 清空操作需要管理员认证
    - 会删除版本号 <= 指定版本的所有固件
    - 例如: clear 1.0.5 会删除 1.0.0, 1.0.1, 1.0.2, 1.0.3, 1.0.4, 1.0.5 等版本
    - 操作前会显示所有固件列表并要求二次确认
    - 优先使用命令行参数，其次环境变量，最后交互式输入

Intel HEX增强模式说明:
  传统模式: 
    - 保持HEX文件完整，由固件管理器在运行时处理
    - 兼容现有的刷写逻辑
  
  HEX增强模式:
    - 在打包时将HEX文件解析并分割为多个二进制组件
    - 每个地址段独立存储，支持精确的地址映射
    - 生成详细的组件清单和地址信息
    - 便于调试和地址冲突分析

支持的组件:
  application   - 应用程序固件
  webresources  - Web界面资源
  adc_mapping   - ADC通道映射数据

支持的槽位:
  A             - 主槽 (0x90000000起始)
  B             - 备用槽 (0x902B0000起始)

注意: 槽B发版包会自动重新编译Application以适配槽B地址空间
      Intel HEX增强模式为实验性功能，建议先使用hex命令测试
        """
    )
    
    subparsers = parser.add_subparsers(dest='command', help='可用命令')
    
    # 自动构建命令（使用Intel HEX分割处理）
    auto_parser = subparsers.add_parser('auto', help='自动构建双槽release包（使用Intel HEX分割处理）')
    auto_parser.add_argument("--version", help="发版版本号（可选，不指定则交互式输入）")
    
    # 快速构建并刷写命令
    quick_parser = subparsers.add_parser('quick', help='快速构建槽A的application并直接刷写到设备')
    
    # 刷写Web资源命令
    web_parser = subparsers.add_parser('web', help='只刷写Web资源到设备')
    web_parser.add_argument("--slot", choices=["A", "B"], default="A", help="目标槽位（可选，默认: A）")
    
    # 设备ID读取命令
    device_id_parser = subparsers.add_parser('device-id', help='读取STM32设备唯一ID并计算设备ID哈希')
    
    # 注册设备ID命令
    register_parser = subparsers.add_parser('register', help='注册设备ID到服务器')
    register_parser.add_argument("--server", help="指定服务器地址（可选，使用配置的默认地址）")
    register_parser.add_argument("--service-token-file", help="服务令牌文件（或 HBOX_ADMIN_SERVICE_TOKEN_FILE）")
    
    # 配置管理命令
    config_parser = subparsers.add_parser('config', help='配置管理')
    config_parser.add_argument("--show", action="store_true", help="显示当前配置")
    config_parser.add_argument("--edit", action="store_true", help="交互式编辑配置")
    config_parser.add_argument("--reset", action="store_true", help="重置为默认配置")
    config_parser.add_argument("--set-server", help="设置默认服务器地址")
    config_parser.add_argument("--set-timeout", type=int, help="设置请求超时时间（秒）")
    config_parser.add_argument("--set-retry", type=int, help="设置重试次数")
    
    # Intel HEX处理命令（独立测试）
    hex_parser = subparsers.add_parser('hex', help='Intel HEX文件处理和分割（测试功能）')
    hex_parser.add_argument("hex_file", help="要处理的Intel HEX文件路径")
    hex_parser.add_argument("--output", "-o", help="输出目录（可选，默认为当前目录下的hex_output）")
    hex_parser.add_argument("--version", help="版本号（可选，默认为1.0.0）")
    
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
    flash_parser.add_argument(
        "--slot",
        choices=["A", "B"],
        help="槽位一致性检查（若提供，必须与已签名包完全相同）",
    )
    flash_parser.add_argument("--components", nargs="+", 
                            choices=["application", "webresources", "adc_mapping", "sys_assets", "sysbg"],
                            help="组件一致性检查（若提供，必须包含包内全部active安全组件）")
    
    # 上传命令
    upload_parser = subparsers.add_parser('upload', help='上传固件包到服务器')
    upload_parser.add_argument("--version", help="指定版本号（可选）")
    upload_parser.add_argument("--server", help="指定服务器地址（可选，使用配置的默认地址）")
    upload_parser.add_argument("--desc", help="指定固件描述（可选）")
    upload_parser.add_argument("--slot-a", help="指定槽A的固件包路径（可选）")
    upload_parser.add_argument("--slot-b", help="指定槽B的固件包路径（可选）")
    upload_parser.add_argument("--service-token-file", help="服务令牌文件（或 HBOX_ADMIN_SERVICE_TOKEN_FILE）")
    
    # 删除命令
    delete_parser = subparsers.add_parser('delete', help='删除服务器固件')
    delete_parser.add_argument("firmware_id", help="要删除的固件ID")
    delete_parser.add_argument("--server", help="指定服务器地址（可选，使用配置的默认地址）")
    delete_parser.add_argument("--service-token-file", help="服务令牌文件（或 HBOX_ADMIN_SERVICE_TOKEN_FILE）")
    
    # 清空命令
    clear_parser = subparsers.add_parser('clear', help='清空指定版本及之前的所有固件')
    clear_parser.add_argument("target_version", help="目标版本号（将删除此版本及之前的所有固件）")
    clear_parser.add_argument("--server", help="指定服务器地址（可选，使用配置的默认地址）")
    clear_parser.add_argument("--service-token-file", help="服务令牌文件（或 HBOX_ADMIN_SERVICE_TOKEN_FILE）")
    
    appweb_parser = subparsers.add_parser('appweb', help='快捷编译 application 并烧录 application 和 web resource')
    appweb_parser.add_argument("--slot", choices=["A", "B"], default="A", help="目标槽位（可选，默认: A）")
    
    args = parser.parse_args()
    
    if not args.command:
        parser.print_help()
        return 1
    
    manager = ReleaseManager()
    
    try:
        if args.command == 'auto':
            # 自动构建双槽release包（使用Intel HEX分割处理）
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
        
        elif args.command == 'quick':
            # 快速构建槽A的application并直接刷写到设备
            if manager.build_and_flash_slot_a_app():
                print("\n✓ 槽A Application快速构建并刷写成功!")
                return 0
            else:
                print("\n✗ 槽A Application快速构建失败")
                return 1
        
        elif args.command == 'web':
            # 只刷写Web资源到设备
            slot = args.slot
            if manager.flash_web_resources(slot):
                print(f"\n✓ Web资源刷写到槽{slot}成功!")
                return 0
            else:
                print(f"\n✗ Web资源刷写到槽{slot}失败!")
                return 1
        
        elif args.command == 'device-id':
            # 读取STM32设备唯一ID并计算设备ID哈希
            if manager.read_device_ids():
                print("\n✓ 设备ID读取成功")
                return 0
            else:
                print("\n✗ 设备ID读取失败")
                return 1
        
        elif args.command == 'register':
            # 注册设备ID到服务器
            server_url = args.server
            if manager.register_device_id(server_url, args.service_token_file):
                print("\n✓ 设备ID注册成功")
                return 0
            else:
                print("\n✗ 设备ID注册失败")
                return 1
        
        elif args.command == 'hex':
            # Intel HEX文件处理和分割
            hex_file_path = Path(args.hex_file)
            if not hex_file_path.exists():
                print(f"错误: HEX文件不存在: {hex_file_path}")
                return 1
            
            output_dir = Path(args.output) if args.output else Path("hex_output")
            version = args.version or "1.0.0"
            
            print(f"=== Intel HEX文件处理测试 ===")
            print(f"输入文件: {hex_file_path}")
            print(f"输出目录: {output_dir}")
            print(f"版本: {version}")
            
            try:
                hex_segmenter = HexSegmenter()
                manifest = hex_segmenter.process_hex_file(hex_file_path, output_dir, version)
                
                print(f"\n✓ HEX文件处理完成")
                print(f"生成了 {len(manifest['components'])} 个组件:")
                for comp in manifest['components']:
                    print(f"  - {comp['name']}: {comp['file']} ({comp['size']:,} 字节)")
                    print(f"    地址: {comp.get('original_address', 'N/A')} -> {comp['address']}")
                
                print(f"\n输出文件保存在: {output_dir}")
                return 0
                
            except Exception as e:
                print(f"✗ HEX文件处理失败: {e}")
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
                    server_url=args.server,
                    desc=args.desc,
                    service_token_file=args.service_token_file
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
                    server_url=args.server,
                    desc=args.desc,
                    service_token_file=args.service_token_file
                ):
                    print("\n✓ 固件包上传成功")
                    return 0
                else:
                    print("\n✗ 固件包上传失败")
                    return 1
        
        elif args.command == 'delete':
            # 删除服务器固件
            firmware_id = args.firmware_id
            server_url = args.server
            if manager.delete_firmware_from_server(
                firmware_id, server_url, args.service_token_file
            ):
                print("\n✓ 固件删除成功")
                return 0
            else:
                print("\n✗ 固件删除失败")
                return 1
        
        elif args.command == 'clear':
            # 清空服务器固件
            target_version = args.target_version
            server_url = args.server
            if manager.clear_firmware_versions_from_server(
                target_version, server_url, args.service_token_file
            ):
                print("\n✓ 固件清空成功")
                return 0
            else:
                print("\n✗ 固件清空失败")
                return 1
        
        elif args.command == 'config':
            # 配置管理
            config = get_config()
            
            if args.show:
                config.show_config()
                return 0
            elif args.edit:
                config.edit_config_interactive()
                return 0
            elif args.reset:
                config.reset_config()
                return 0
            elif args.set_server:
                config.update_config("server", "default_url", args.set_server)
                return 0
            elif args.set_timeout:
                config.update_config("server", "timeout", args.set_timeout)
                return 0
            elif args.set_retry:
                config.update_config("server", "retry_count", args.set_retry)
                return 0
            else:
                # 默认显示配置
                config.show_config()
                return 0
        
        elif args.command == 'appweb':
            slot = args.slot
            if manager.build_and_flash_app_and_web(slot):
                print(f"\n✓ Application + Web Resource (槽{slot}) 编译和烧录全部成功!")
                return 0
            else:
                print(f"\n✗ Application + Web Resource (槽{slot}) 编译或烧录失败!")
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
