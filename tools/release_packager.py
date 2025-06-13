#!/usr/bin/env python3
"""
STM32 HBox 发版打包工具

基于双槽升级设计，提取槽A数据并生成标准发版包：
1. 从槽A提取 application、webresources、adc_mapping
2. 生成槽A和槽B的发版包
3. 包含版本管理、元数据和完整性校验
4. 支持槽B地址重映射和重新编译
"""

import os
import sys
import argparse
import struct
import json
import hashlib
import zipfile
import subprocess
from pathlib import Path
from datetime import datetime
from typing import Dict, List, Optional, NamedTuple, Tuple
import tempfile
import shutil
import re
import locale

class ReleaseComponent(NamedTuple):
    """发版组件"""
    name: str                   # 组件名称
    data: bytes                 # 二进制数据
    address: int               # 目标地址
    size: int                  # 数据大小
    checksum: str              # SHA256校验和

class SlotConfig(NamedTuple):
    """槽配置"""
    base_address: int          # 槽基地址
    application_address: int   # Application地址
    application_size: int      # Application大小
    webresources_address: int  # WebResources地址  
    webresources_size: int     # WebResources大小
    adc_mapping_address: int   # ADC Mapping地址
    adc_mapping_size: int      # ADC Mapping大小

class ReleasePackage(NamedTuple):
    """发版包"""
    version: str               # 版本号
    build_date: str           # 构建日期
    slot: str                 # 目标槽 (A/B)
    components: List[ReleaseComponent]  # 组件列表
    metadata: Dict            # 元数据

class ReleasePackager:
    """发版打包器"""
    
    def __init__(self):
        self.project_root = Path(__file__).parent.parent
        self.application_dir = self.project_root / "application"
        self.tools_dir = self.project_root / "tools"
        self.resources_dir = self.project_root / "resources"
        self.releases_dir = self.project_root / "releases"
        
        # 槽配置 - 与build.py保持完全一致
        self.slot_configs = {
            "A": SlotConfig(
                base_address=0x90000000,
                application_address=0x90000000,              # build.py中槽A地址
                application_size=1024 * 1024,
                webresources_address=0x90100000,
                webresources_size=1536 * 1024,
                adc_mapping_address=0x90280000,
                adc_mapping_size=128 * 1024
            ),
            "B": SlotConfig(
                base_address=0x902B0000,
                application_address=0x902B0000,              # build.py中槽B地址
                application_size=1024 * 1024,
                webresources_address=0x903B0000,
                webresources_size=1536 * 1024,
                adc_mapping_address=0x90530000,
                adc_mapping_size=128 * 1024
            )
        }
        
        # 创建目录
        self.ensure_directories()
        
    def ensure_directories(self):
        """确保必要目录存在"""
        self.resources_dir.mkdir(exist_ok=True)
        self.releases_dir.mkdir(exist_ok=True)
        print(f"发版目录: {self.releases_dir}")
    
    def calculate_checksum(self, data: bytes) -> str:
        """计算SHA256校验和"""
        return hashlib.sha256(data).hexdigest()
    
    def extract_from_device(self) -> Dict[str, bytes]:
        """从连接的设备提取槽A数据"""
        print("从设备提取槽A数据...")
        
        components = {}
        slot_config = self.slot_configs["A"]
        
        try:
            # 提取Application
            app_data = self._extract_memory_region(
                slot_config.application_address,
                slot_config.application_size,
                "application"
            )
            if app_data:
                components["application"] = app_data
            
            # 提取WebResources
            web_data = self._extract_memory_region(
                slot_config.webresources_address,
                slot_config.webresources_size,
                "webresources"
            )
            if web_data:
                components["webresources"] = web_data
            
            # 提取ADC Mapping
            adc_data = self._extract_memory_region(
                slot_config.adc_mapping_address,
                slot_config.adc_mapping_size,
                "adc_mapping"
            )
            if adc_data:
                components["adc_mapping"] = adc_data
                
        except Exception as e:
            print(f"从设备提取数据失败: {e}")
            return None
            
        return components
    
    def _extract_memory_region(self, address: int, size: int, name: str) -> bytes:
        """通过OpenOCD从设备提取内存区域"""
        temp_file = f"temp_{name}.bin"
        
        try:
            cmd = [
                "openocd",
                "-f", "interface/stlink.cfg",
                "-f", str(self.application_dir / "Openocd_Script" / "ST-LINK-QSPIFLASH.cfg"),
                "-c", "init",
                "-c", "reset halt",
                "-c", f"dump_image {temp_file} 0x{address:08X} {size}",
                "-c", "exit"
            ]
            
            result = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
            
            if result.returncode == 0:
                temp_path = Path(temp_file)
                if temp_path.exists():
                    with open(temp_path, 'rb') as f:
                        data = f.read()
                    temp_path.unlink()  # 删除临时文件
                    print(f"✓ 成功提取 {name}: {len(data)} 字节")
                    return data
                else:
                    print(f"✗ 未找到 {name} 输出文件")
            else:
                print(f"✗ 提取 {name} 失败: {result.stderr}")
                
        except subprocess.TimeoutExpired:
            print(f"✗ 提取 {name} 超时")
        except FileNotFoundError:
            print("✗ 未找到 OpenOCD，请确保已安装")
        except Exception as e:
            print(f"✗ 提取 {name} 异常: {e}")
        
        return None
    
    def extract_from_flash_dump(self, dump_file: str) -> Dict[str, bytes]:
        """从Flash转储文件提取槽A数据"""
        print(f"从Flash转储文件提取数据: {dump_file}")
        
        dump_path = Path(dump_file)
        if not dump_path.exists():
            print(f"错误: 转储文件不存在: {dump_file}")
            return None
            
        components = {}
        slot_config = self.slot_configs["A"]
        
        try:
            with open(dump_path, 'rb') as f:
                f.seek(0, 2)
                file_size = f.tell()
                print(f"转储文件大小: {file_size} 字节 ({file_size/(1024*1024):.1f} MB)")
                
                # 提取各组件数据 - 使用物理偏移地址
                components_info = [
                    ("application", 0x00000000, slot_config.application_size),
                    ("webresources", 0x00100000, slot_config.webresources_size),
                    ("adc_mapping", 0x00280000, slot_config.adc_mapping_size)
                ]
                
                for name, offset, size in components_info:
                    if file_size >= offset + size:
                        f.seek(offset)
                        data = f.read(size)
                        components[name] = data
                        print(f"✓ 提取 {name}: {len(data)} 字节 (偏移 0x{offset:08X})")
                    else:
                        print(f"✗ 文件大小不足，无法提取 {name}")
                        
        except Exception as e:
            print(f"读取转储文件失败: {e}")
            return None
            
        return components
    
    def extract_from_build_output(self) -> Dict[str, bytes]:
        """从构建输出提取数据 - 强制重新编译"""
        print("强制重新编译并提取数据...")
        
        components = {}
        
        # 强制重新编译Application（槽A）
        print("清理构建目录并重新编译Application...")
        app_data = self.build_application_for_slot("A")
        if app_data:
            components["application"] = app_data
            components["application_type"] = "hex"  # 编译产出的是HEX格式
            print(f"✓ 重新编译 application: {len(app_data)} 字节 (HEX格式)")
        else:
            print("✗ Application重新编译失败")
        
        # 从resources目录提取WebResources
        web_resources_files = [
            self.resources_dir / "web_resources.bin",
            self.application_dir / "Libs" / "httpd" / "ex_fsdata.bin"
        ]
        
        for web_file in web_resources_files:
            if web_file.exists():
                with open(web_file, 'rb') as f:
                    components["webresources"] = f.read()
                print(f"✓ 提取 webresources: {len(components['webresources'])} 字节 (从 {web_file})")
                break
        else:
            print("✗ 未找到webresources文件")
        
        # 从resources目录提取ADC Mapping
        adc_mapping_bin = self.resources_dir / "slot_a_adc_mapping.bin"
        if adc_mapping_bin.exists():
            with open(adc_mapping_bin, 'rb') as f:
                components["adc_mapping"] = f.read()
            print(f"✓ 提取 adc_mapping: {len(components['adc_mapping'])} 字节")
        else:
            print("✗ 未找到adc_mapping文件")
        
        return components if components else None
    
    def build_application_for_slot(self, slot: str) -> Optional[bytes]:
        """为指定槽重新编译Application"""
        print(f"为槽{slot}重新编译Application...")
        
        # 保存当前工作目录
        original_cwd = os.getcwd()
        backup_files = []
        
        try:
            # 切换到application目录
            os.chdir(self.application_dir)
            print(f"切换到目录: {self.application_dir}")
            
            # 修改链接脚本
            backup_path = self._modify_linker_script_for_slot(slot)
            if backup_path:
                backup_files.append(backup_path)
            
            # 清理之前的构建
            print("清理之前的构建...")
            build_dir = self.application_dir / "build"
            if build_dir.exists():
                shutil.rmtree(build_dir)
            
            # 执行make编译
            print("开始编译...")
            cmd = ["make", "-j8"]  # 使用8个并行任务
            
            # 修复编码问题 - 使用系统默认编码并忽略错误
            try:
                system_encoding = locale.getpreferredencoding()
            except:
                system_encoding = 'utf-8'
            print(f"使用系统编码: {system_encoding}")
            
            result = subprocess.run(
                cmd,
                capture_output=True,
                text=True,
                encoding=system_encoding,
                errors='ignore',  # 忽略编码错误
                timeout=300  # 5分钟超时
            )
            
            print(f"编译返回码: {result.returncode}")
            
            if result.returncode == 0:
                print("✓ 编译命令执行成功")
                
                # 查找ELF文件并重新生成正确地址的HEX文件
                possible_elf_outputs = [
                    self.application_dir / "build" / "application.elf",
                    self.application_dir / "build" / "STM32H750XBHx.elf"
                ]
                
                slot_config = self.slot_configs[slot]
                
                for elf_file in possible_elf_outputs:
                    if elf_file.exists():
                        print(f"找到ELF文件: {elf_file}")
                        
                        # 为所有槽重新生成正确地址的HEX文件
                        print(f"为槽{slot}重新生成HEX文件，目标地址: 0x{slot_config.application_address:08X}")
                        
                        # 使用objcopy生成指定地址的HEX文件
                        temp_hex = self.application_dir / "build" / f"application_slot_{slot}.hex"
                        
                        # 方法1：尝试使用LMA地址生成HEX文件
                        cmd_hex = [
                            "arm-none-eabi-objcopy",
                            "-O", "ihex",
                            "--change-section-vma", "*+0",  # 保持VMA不变
                            str(elf_file),
                            str(temp_hex)
                        ]
                        
                        result_hex = subprocess.run(cmd_hex, capture_output=True, text=True)
                        
                        if result_hex.returncode == 0 and temp_hex.exists():
                            with open(temp_hex, 'rb') as f:
                                app_data = f.read()
                            print(f"✓ 槽{slot} Application HEX文件重新生成成功: {len(app_data)} 字节")
                            
                            # 验证HEX文件地址
                            with open(temp_hex, 'r') as f:
                                first_lines = [f.readline().strip() for _ in range(5)]
                            print(f"槽{slot} HEX文件前5行:")
                            for i, line in enumerate(first_lines):
                                print(f"  {line}")
                            
                            return app_data
                        else:
                            print(f"✗ 重新生成槽{slot} HEX文件失败: {result_hex.stderr}")
                        break
                
                # 如果以上方法都失败，查找任何.hex文件
                hex_files = list(build_dir.glob("*.hex"))
                if hex_files:
                    print(f"发现其他hex文件: {[f.name for f in hex_files]}")
                    hex_file = hex_files[0]
                    with open(hex_file, 'rb') as f:
                        app_data = f.read()
                    print(f"✓ 使用备选hex文件: {hex_file.name}, {len(app_data)} 字节")
                    return app_data
                
                print("✗ 编译成功但未找到可用的输出文件")
            else:
                print(f"✗ 编译失败:")
                print(f"返回码: {result.returncode}")
                if result.stdout:
                    stdout_preview = result.stdout[:2000] if len(result.stdout) > 2000 else result.stdout
                    print(f"标准输出 (前2000字符): {stdout_preview}")
                if result.stderr:
                    stderr_preview = result.stderr[:2000] if len(result.stderr) > 2000 else result.stderr
                    print(f"错误输出 (前2000字符): {stderr_preview}")
                
        except subprocess.TimeoutExpired:
            print("✗ 编译超时")
        except UnicodeDecodeError as e:
            print(f"✗ 编码错误: {e}")
            print("尝试使用不同的编码方式...")
        except Exception as e:
            print(f"✗ 编译异常: {e}")
            import traceback
            traceback.print_exc()
        finally:
            # 恢复原始文件
            for backup_file in backup_files:
                self._restore_file(backup_file)
            
            # 恢复工作目录
            os.chdir(original_cwd)
        
        return None
    
    def _modify_linker_script_for_slot(self, slot: str) -> Optional[Path]:
        """修改链接脚本以适应指定槽"""
        linker_script = self.application_dir / "STM32H750XBHx_FLASH.ld"
        
        if not linker_script.exists():
            print(f"错误: 链接脚本不存在: {linker_script}")
            return None
            
        # 备份原始文件
        backup_path = self._backup_file(linker_script)
        
        try:
            # 读取原始文件
            with open(linker_script, 'r', encoding='utf-8') as f:
                content = f.read()
            
            # 获取槽配置
            slot_cfg = self.slot_configs[slot]
            
            # 使用正则表达式替换地址
            # 替换FLASH地址 - 匹配 FLASH (rx) : ORIGIN = 0x90000000, LENGTH = 2048K
            flash_pattern = r'(FLASH\s*\(rx\)\s*:\s*ORIGIN\s*=\s*)0x[0-9A-Fa-f]+(\s*,\s*LENGTH\s*=\s*\d+K)'
            new_flash_line = f'\\g<1>0x{slot_cfg.application_address:08X}\\g<2>'
            content = re.sub(flash_pattern, new_flash_line, content)
            
            print(f"已修改链接脚本以适应槽{slot}:")
            print(f"  FLASH: 0x{slot_cfg.application_address:08X}")
            
            # 写入修改后的文件
            with open(linker_script, 'w', encoding='utf-8') as f:
                f.write(content)
                
            return backup_path
            
        except Exception as e:
            print(f"错误: 修改链接脚本失败: {e}")
            # 恢复备份
            if backup_path:
                self._restore_file(backup_path)
            return None
    
    def _backup_file(self, file_path: Path) -> Path:
        """备份文件"""
        backup_path = file_path.with_suffix(file_path.suffix + '.bak')
        if file_path.exists():
            shutil.copy2(file_path, backup_path)
            print(f"已备份: {file_path.name}")
        return backup_path
    
    def _restore_file(self, backup_path: Path):
        """恢复文件"""
        if not backup_path.exists():
            return
            
        original_path = backup_path.with_suffix('')
        original_path = Path(str(original_path).replace('.bak', ''))
        
        if backup_path.exists():
            shutil.move(backup_path, original_path)
            print(f"已恢复: {original_path.name}")
    
    def create_release_package(self, slot: str, version: str, source_components: Dict[str, bytes], 
                             extraction_method: str = "dump") -> ReleasePackage:
        """创建发版包"""
        print(f"创建槽{slot}发版包 v{version}...")
        
        slot_config = self.slot_configs[slot]
        release_components = []
        
        # 处理Application组件 - 所有槽都重新编译以确保正确的地址
        print(f"  ⚠ 槽{slot}重新编译Application以适配地址空间...")
        app_data = self.build_application_for_slot(slot)
        if not app_data:
            print(f"  ✗ 槽{slot} Application编译失败")
            return None
        
        # 检查Application大小并填充
        if len(app_data) > slot_config.application_size:
            print(f"警告: Application大小 {len(app_data)} 超过限制 {slot_config.application_size}")
            app_data = app_data[:slot_config.application_size]
        
        if len(app_data) < slot_config.application_size:
            app_data = app_data + b'\x00' * (slot_config.application_size - len(app_data))
        
        app_component = ReleaseComponent(
            name="application",
            data=app_data,
            address=slot_config.application_address,
            size=len(app_data),
            checksum=self.calculate_checksum(app_data)
        )
        release_components.append(app_component)
        print(f"  ✓ Application: {len(app_data)} 字节, 校验和: {app_component.checksum[:16]}...")
        
        # 处理WebResources和ADC Mapping（直接复制数据到对应地址）
        other_components = {
            "webresources": (slot_config.webresources_address, slot_config.webresources_size),
            "adc_mapping": (slot_config.adc_mapping_address, slot_config.adc_mapping_size)
        }
        
        for name, (address, max_size) in other_components.items():
            if name in source_components:
                data = source_components[name]
                
                # 检查大小限制
                if len(data) > max_size:
                    print(f"警告: {name} 大小 {len(data)} 超过限制 {max_size}")
                    data = data[:max_size]
                
                # 填充到指定大小
                if len(data) < max_size:
                    data = data + b'\x00' * (max_size - len(data))
                
                component = ReleaseComponent(
                    name=name,
                    data=data,
                    address=address,
                    size=len(data),
                    checksum=self.calculate_checksum(data)
                )
                release_components.append(component)
                print(f"  ✓ {name}: {len(data)} 字节, 校验和: {component.checksum[:16]}...")
        
        # 创建元数据
        metadata = {
            "firmware_version": self._get_firmware_version(),
            "config_version": self._get_config_version(),
            "adc_mapping_version": self._get_adc_mapping_version(),
            "build_timestamp": datetime.now().isoformat(),
            "git_commit": self._get_git_commit(),
            "extraction_method": extraction_method,
            "slot_config": {
                "base_address": f"0x{slot_config.base_address:08X}",
                "application_address": f"0x{slot_config.application_address:08X}",
                "webresources_address": f"0x{slot_config.webresources_address:08X}",
                "adc_mapping_address": f"0x{slot_config.adc_mapping_address:08X}"
            }
        }
        
        return ReleasePackage(
            version=version,
            build_date=datetime.now().strftime("%Y%m%d_%H%M%S"),
            slot=slot,
            components=release_components,
            metadata=metadata
        )
    
    def save_release_package(self, package: ReleasePackage, package_type: str = "complete") -> str:
        """保存发版包到文件"""
        # 创建发版包文件名
        package_name = f"hbox_firmware_slot_{package.slot.lower()}_v{package.version}_{package.build_date}_{package_type}.zip"
        package_path = self.releases_dir / package_name
        
        print(f"保存发版包: {package_path.name}")
        
        # 创建临时目录
        with tempfile.TemporaryDirectory() as temp_dir:
            temp_path = Path(temp_dir)
            
            # 保存各组件文件
            for component in package.components:
                if component.name == "application":
                    # Application保存为HEX格式，适合外部Flash
                    comp_file = temp_path / f"{component.name}.hex"
                    with open(comp_file, 'wb') as f:
                        f.write(component.data)
                else:
                    # 其他组件保存为BIN格式
                    comp_file = temp_path / f"{component.name}.bin"
                    with open(comp_file, 'wb') as f:
                        f.write(component.data)
            
            # 保存元数据
            manifest = {
                "package_info": {
                    "version": package.version,
                    "build_date": package.build_date,
                    "slot": package.slot,
                    "package_type": package_type
                },
                "components": [
                    {
                        "name": comp.name,
                        "file": f"{comp.name}.hex" if comp.name == "application" else f"{comp.name}.bin",
                        "address": f"0x{comp.address:08X}",
                        "size": comp.size,
                        "checksum": comp.checksum
                    }
                    for comp in package.components
                ],
                "metadata": package.metadata
            }
            
            manifest_file = temp_path / "manifest.json"
            with open(manifest_file, 'w', encoding='utf-8') as f:
                json.dump(manifest, f, indent=2, ensure_ascii=False)
            
            # 创建刷写脚本
            self._create_flash_scripts(temp_path, package)
            
            # 创建自包含的OpenOCD配置文件
            self._create_self_contained_config(temp_path)
            
            # 创建README
            self._create_package_readme(temp_path, package)
            
            # 创建ZIP包
            with zipfile.ZipFile(package_path, 'w', zipfile.ZIP_DEFLATED) as zf:
                for file_path in temp_path.rglob('*'):
                    if file_path.is_file():
                        arcname = file_path.relative_to(temp_path)
                        zf.write(file_path, arcname)
        
        print(f"✓ 发版包已保存: {package_path}")
        print(f"  大小: {package_path.stat().st_size / (1024*1024):.1f} MB")
        
        return str(package_path)
    
    def _create_flash_scripts(self, output_dir: Path, package: ReleasePackage):
        """创建刷写脚本"""
        slot_config = self.slot_configs[package.slot]
        
        # OpenOCD脚本 - 使用与build.py完全一致的命令序列
        openocd_script = output_dir / "flash_with_openocd.cfg"
        with open(openocd_script, 'w', encoding='utf-8') as f:
            f.write(f"# OpenOCD刷写脚本 - 槽{package.slot}\n")
            f.write("# 使用方法: openocd -d0 -f openocd_config/ST-LINK-QSPIFLASH.cfg -f flash_with_openocd.cfg\n\n")
            f.write("init\n")
            f.write("halt\n")
            f.write("reset init\n\n")
            
            for component in package.components:
                if component.name == "application":
                    # Application使用HEX格式，与build.py保持一致
                    f.write(f"# 刷写 {component.name} (HEX格式)\n")
                    f.write(f"flash write_image erase {component.name}.hex 0x{component.address:08X}\n")
                    f.write(f"flash verify_image {component.name}.hex 0x{component.address:08X}\n\n")
                else:
                    # 其他组件使用BIN格式，使用内存映射地址
                    f.write(f"# 刷写 {component.name} 到 0x{component.address:08X}\n")
                    f.write(f"flash write_image erase {component.name}.bin 0x{component.address:08X}\n")
                    f.write(f"flash verify_image {component.name}.bin 0x{component.address:08X}\n\n")
            
            f.write("reset run\n")
            f.write("shutdown\n")
        
        # 批处理脚本 - 添加-d0参数
        batch_script = output_dir / "flash.bat"
        with open(batch_script, 'w', encoding='utf-8') as f:
            f.write("@echo off\n")
            f.write(f"echo 刷写HBox固件到槽{package.slot}\n")
            f.write("echo.\n")
            f.write("echo 请确保设备已连接ST-Link调试器\n")
            f.write("pause\n\n")
            f.write("openocd -d0 -f openocd_config/ST-LINK-QSPIFLASH.cfg -f flash_with_openocd.cfg\n")
            f.write("pause\n")
        
        # Shell脚本 - 添加-d0参数
        shell_script = output_dir / "flash.sh"
        with open(shell_script, 'w', encoding='utf-8') as f:
            f.write("#!/bin/bash\n")
            f.write(f"echo \"刷写HBox固件到槽{package.slot}\"\n")
            f.write("echo\n")
            f.write("echo \"请确保设备已连接ST-Link调试器\"\n")
            f.write("read -p \"按回车继续...\"\n\n")
            f.write("openocd -d0 -f openocd_config/ST-LINK-QSPIFLASH.cfg -f flash_with_openocd.cfg\n")
        
        # 设置执行权限
        try:
            shell_script.chmod(0o755)
        except:
            pass
    
    def _create_package_readme(self, output_dir: Path, package: ReleasePackage):
        """创建发版包说明文档"""
        readme_file = output_dir / "README.md"
        
        with open(readme_file, 'w', encoding='utf-8') as f:
            f.write(f"# HBox 固件发版包 - 槽{package.slot}\n\n")
            f.write(f"## 版本信息\n")
            f.write(f"- **版本号**: {package.version}\n")
            f.write(f"- **构建日期**: {package.build_date}\n")
            f.write(f"- **目标槽**: 槽{package.slot}\n")
            f.write(f"- **固件版本**: {package.metadata.get('firmware_version', 'unknown')}\n")
            f.write(f"- **Git提交**: {package.metadata.get('git_commit', 'unknown')}\n")
            f.write(f"- **提取方法**: {package.metadata.get('extraction_method', 'unknown')}\n\n")
            
            f.write(f"## 组件列表\n\n")
            for comp in package.components:
                f.write(f"### {comp.name}\n")
                f.write(f"- **地址**: 0x{comp.address:08X}\n")
                f.write(f"- **大小**: {comp.size // 1024} KB\n")
                f.write(f"- **校验和**: {comp.checksum}\n\n")
            
            f.write(f"## 刷写方法\n\n")
            f.write(f"### Windows\n")
            f.write(f"双击运行 `flash.bat`\n\n")
            f.write(f"### Linux/macOS\n")
            f.write(f"```bash\n")
            f.write(f"chmod +x flash.sh\n")
            f.write(f"./flash.sh\n")
            f.write(f"```\n\n")
            f.write(f"### 手动刷写\n")
            f.write(f"```bash\n")
            f.write(f"openocd -f openocd_config/ST-LINK-QSPIFLASH.cfg -f flash_with_openocd.cfg\n")
            f.write(f"```\n\n")
            
            f.write(f"## 注意事项\n\n")
            f.write(f"1. 确保设备已连接ST-Link调试器\n")
            f.write(f"2. 刷写前请备份重要数据\n")
            f.write(f"3. 槽{package.slot}固件会覆盖对应地址空间的所有数据\n")
            if package.slot == "B" and package.metadata.get("extraction_method") == "dump":
                f.write(f"4. ⚠️ 槽B固件已重新编译以适配槽B地址空间\n")
    
    def _get_firmware_version(self) -> str:
        """获取固件版本号"""
        try:
            board_cfg = self.application_dir / "Core" / "Inc" / "board_cfg.h"
            with open(board_cfg, 'r') as f:
                for line in f:
                    if "FIRMWARE_VERSION" in line and "#define" in line:
                        parts = line.split()
                        if len(parts) >= 3:
                            version_hex = parts[2].replace("(uint32_t)", "")
                            return version_hex
        except:
            pass
        return "0x010000"
    
    def _get_config_version(self) -> str:
        """获取配置版本号"""
        try:
            board_cfg = self.application_dir / "Core" / "Inc" / "board_cfg.h"
            with open(board_cfg, 'r') as f:
                for line in f:
                    if "CONFIG_VERSION" in line and "#define" in line:
                        parts = line.split()
                        if len(parts) >= 3:
                            version_hex = parts[2].replace("(uint32_t)", "")
                            return version_hex
        except:
            pass
        return "0x000007"
    
    def _get_adc_mapping_version(self) -> str:
        """获取ADC映射版本号"""
        try:
            board_cfg = self.application_dir / "Core" / "Inc" / "board_cfg.h"
            with open(board_cfg, 'r') as f:
                for line in f:
                    if "ADC_MAPPING_VERSION" in line and "#define" in line:
                        parts = line.split()
                        if len(parts) >= 3:
                            version_hex = parts[2].replace("(uint32_t)", "")
                            return version_hex
        except:
            pass
        return "0x000001"
    
    def _get_git_commit(self) -> str:
        """获取Git提交哈希"""
        try:
            result = subprocess.run(
                ["git", "rev-parse", "HEAD"],
                cwd=self.project_root,
                capture_output=True,
                text=True
            )
            if result.returncode == 0:
                return result.stdout.strip()
        except:
            pass
        return "unknown"
    
    def create_complete_release(self, version: str, extraction_method: str, **kwargs) -> List[str]:
        """创建完整发版包（槽A和槽B）"""
        print(f"创建完整发版包 v{version}")
        print("=" * 50)
        
        # 提取槽A数据
        if extraction_method == "device":
            source_components = self.extract_from_device()
        elif extraction_method == "dump":
            source_components = self.extract_from_flash_dump(kwargs["dump_file"])
        elif extraction_method == "build":
            source_components = self.extract_from_build_output()
        else:
            print(f"错误: 未知的提取方法: {extraction_method}")
            return []
        
        if not source_components:
            print("错误: 未能提取到有效数据")
            return []
        
        print(f"\n成功提取到 {len(source_components)} 个组件")
        
        # 创建槽A和槽B发版包
        release_files = []
        
        for slot in ["A", "B"]:
            print(f"\n创建槽{slot}发版包...")
            package = self.create_release_package(slot, version, source_components, extraction_method)
            if package:
                package_file = self.save_release_package(package, "complete")
                release_files.append(package_file)
            else:
                print(f"✗ 槽{slot}发版包创建失败")
        
        print("\n" + "=" * 50)
        if release_files:
            print("发版包创建完成!")
            for file in release_files:
                print(f"  {Path(file).name}")
        else:
            print("发版包创建失败!")
        
        return release_files
    
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
                    expected_checksum = comp_info["checksum"]
                    
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
    
    def flash_release_package(self, package_file: str, verify_before_flash: bool = True) -> bool:
        """自动解压发版包并刷写到设备"""
        print(f"准备刷写发版包: {package_file}")
        
        package_path = Path(package_file)
        if not package_path.exists():
            print(f"错误: 发版包文件不存在: {package_file}")
            return False
        
        # 验证发版包（可选）
        if verify_before_flash:
            print("验证发版包完整性...")
            if not self.verify_release_package(package_file):
                print("发版包验证失败，中止刷写")
                return False
            print("✓ 发版包验证通过")
        
        # 创建临时目录解压发版包
        with tempfile.TemporaryDirectory() as temp_dir:
            temp_path = Path(temp_dir)
            
            try:
                # 解压发版包
                print("解压发版包...")
                with zipfile.ZipFile(package_path, 'r') as zf:
                    zf.extractall(temp_path)
                
                # 读取manifest获取发版包信息
                manifest_file = temp_path / "manifest.json"
                if not manifest_file.exists():
                    print("错误: 发版包缺少manifest.json文件")
                    return False
                
                with open(manifest_file, 'r', encoding='utf-8') as f:
                    manifest = json.load(f)
                
                package_info = manifest["package_info"]
                components = manifest["components"]
                
                print(f"发版包信息:")
                print(f"  版本: {package_info['version']}")
                print(f"  槽: {package_info['slot']}")
                print(f"  构建日期: {package_info['build_date']}")
                print(f"  组件数量: {len(components)}")
                
                # 检查OpenOCD连接
                print("\n检查设备连接...")
                if not self._check_openocd_connection():
                    print("错误: 无法连接到设备，请检查ST-Link连接")
                    return False
                print("✓ 设备连接正常")
                
                # 执行刷写
                print(f"\n开始刷写到槽{package_info['slot']}...")
                print("-" * 40)
                
                # 切换到解压目录
                original_cwd = os.getcwd()
                
                try:
                    # 检查是否有自包含配置文件
                    openocd_cfg = temp_path / "flash_with_openocd.cfg"
                    openocd_config_dir = temp_path / "openocd_config"
                    temp_script_file = None  # 初始化变量
                    
                    if openocd_cfg.exists() and openocd_config_dir.exists():
                        # 使用发版包自带的自包含配置文件，在发版包目录运行
                        print("使用发版包自带的自包含配置文件")
                        os.chdir(temp_path)
                        cmd = [
                            "openocd",
                            "-d0",
                            "-f", "openocd_config/ST-LINK-QSPIFLASH.cfg",
                            "-f", "flash_with_openocd.cfg"
                        ]
                    else:
                        # 备用方案：使用与build.py完全一致的命令格式
                        print("备用方案：使用与build.py一致的配置文件")
                        os.chdir(self.application_dir)
                        
                        # 创建临时脚本 - 使用与build.py一致的命令序列
                        temp_script_content = []
                        temp_script_content.append("init")
                        temp_script_content.append("halt")
                        temp_script_content.append("reset init")
                        temp_script_content.append("")
                        
                        for comp in components:
                            comp_file = temp_path / comp["file"]
                            comp_file_posix = str(comp_file).replace('\\', '/')
                            address = comp["address"]
                            
                            if comp["name"] == "application":
                                # Application使用HEX格式，与build.py保持一致
                                temp_script_content.append(f"# 刷写 {comp['name']} (HEX格式)")
                                temp_script_content.append(f"flash write_image erase \"{comp_file_posix}\" {address}")
                                temp_script_content.append(f"flash verify_image \"{comp_file_posix}\" {address}")
                            else:
                                # 其他组件使用BIN格式，使用内存映射地址
                                temp_script_content.append(f"# 刷写 {comp['name']} 到 {address}")
                                temp_script_content.append(f"flash write_image erase \"{comp_file_posix}\" {address}")
                                temp_script_content.append(f"flash verify_image \"{comp_file_posix}\" {address}")
                            temp_script_content.append("")
                        
                        temp_script_content.append("reset run")
                        temp_script_content.append("shutdown")
                        
                        temp_script_file = self.application_dir / "temp_flash_script.cfg"
                        with open(temp_script_file, 'w', encoding='utf-8') as f:
                            f.write('\n'.join(temp_script_content))
                        
                        cmd = [
                            "openocd",
                            "-d0",
                            "-f", "Openocd_Script/ST-LINK-QSPIFLASH.cfg",
                            "-f", "temp_flash_script.cfg"
                        ]
                    
                    print(f"执行刷写命令: {' '.join(cmd[:4])}...")
                    
                    # 执行OpenOCD刷写
                    result = subprocess.run(
                        cmd,
                        capture_output=True,
                        text=True,
                        timeout=300,  # 5分钟超时
                        encoding='utf-8',
                        errors='ignore'
                    )
                    
                    # 清理临时文件（如果有的话）
                    try:
                        temp_script_file = self.application_dir / "temp_flash_script.cfg"
                        if temp_script_file.exists():
                            temp_script_file.unlink()
                    except:
                        pass
                    
                    # 检查刷写结果
                    if result.returncode == 0:
                        print("✓ 刷写完成!")
                        
                        # 检查输出中的关键信息
                        success_indicators = [
                            "** Verified OK **",
                            "verified OK",
                            "Programming Finished"
                        ]
                        
                        if any(indicator in result.stdout for indicator in success_indicators):
                            print("✓ 刷写验证通过")
                        
                        # 显示刷写的组件信息
                        print("\n刷写的组件:")
                        for comp in components:
                            print(f"  ✓ {comp['name']}: {comp['address']} ({comp['size']} 字节)")
                        
                        return True
                    else:
                        print("✗ 刷写失败!")
                        print(f"返回码: {result.returncode}")
                        if result.stderr:
                            print("错误输出:")
                            print(result.stderr[:6000])  # 增加错误输出长度以查看完整刷写过程
                        if result.stdout:
                            print("标准输出:")
                            print(result.stdout[:6000])  # 增加标准输出长度以查看完整刷写过程
                        return False
                        
                except subprocess.TimeoutExpired:
                    print("✗ 刷写超时")
                    return False
                except Exception as e:
                    print(f"✗ 刷写异常: {e}")
                    return False
                finally:
                    # 恢复工作目录
                    os.chdir(original_cwd)
                    
            except zipfile.BadZipFile:
                print("错误: 无效的ZIP文件")
                return False
            except Exception as e:
                print(f"错误: 解压发版包失败: {e}")
                return False
    
    def _check_openocd_connection(self) -> bool:
        """检查OpenOCD设备连接"""
        try:
            cmd = [
                "openocd",
                "-f", "interface/stlink.cfg",
                "-f", str(self.application_dir / "Openocd_Script" / "ST-LINK-QSPIFLASH.cfg"),
                "-c", "init",
                "-c", "reset halt", 
                "-c", "exit"
            ]
            
            result = subprocess.run(
                cmd,
                capture_output=True,
                text=True,
                timeout=30,
                encoding='utf-8',
                errors='ignore'
            )
            
            # 检查是否成功连接 - 更新检测逻辑
            success_indicators = [
                "target halted",
                "cortex-m7",
                "processor detected",
                "halted due to debug-request",
                "stlink"
            ]
            
            # 合并标准输出和错误输出进行检查
            output_text = (result.stdout + result.stderr).lower()
            connection_success = any(indicator in output_text for indicator in success_indicators)
            
            # 即使返回码不为0，如果有成功指示器也认为连接成功
            # 因为OpenOCD可能会有警告但仍然成功连接
            if connection_success:
                print(f"✓ 检测到设备连接成功")
                
                # 提取并显示设备信息
                lines = result.stdout.split('\n')
                for line in lines:
                    if "STLINK" in line and "VID:PID" in line:
                        print(f"  ST-Link: {line.strip()}")
                    elif "processor detected" in line.lower():
                        print(f"  处理器: {line.strip()}")
                    elif "target halted" in line.lower() and "debug-request" in line.lower():
                        print(f"  状态: 设备已停机，准备刷写")
                        
                return True
            else:
                print(f"✗ 设备连接失败")
                print(f"返回码: {result.returncode}")
                if result.stdout:
                    print(f"标准输出（前500字符）:")
                    print(result.stdout[:500])
                if result.stderr:
                    print(f"错误输出（前500字符）:")
                    print(result.stderr[:500])
                return False
                    
        except subprocess.TimeoutExpired:
            print("设备连接检查超时")
            return False
        except FileNotFoundError:
            print("未找到OpenOCD，请确保已安装")
            return False
        except Exception as e:
            print(f"检查设备连接时异常: {e}")
            return False
    
    def flash_latest_release(self, slot: str = None, verify_before_flash: bool = True) -> bool:
        """刷写最新的发版包"""
        print("查找最新发版包...")
        
        if not self.releases_dir.exists():
            print("错误: 发版目录不存在")
            return False
        
        # 查找发版包文件
        release_files = list(self.releases_dir.glob("*.zip"))
        if not release_files:
            print("错误: 未找到任何发版包")
            return False
        
        # 根据槽过滤
        if slot:
            slot_files = [f for f in release_files if f"_slot_{slot.lower()}_" in f.name]
            if not slot_files:
                print(f"错误: 未找到槽{slot}的发版包")
                return False
            release_files = slot_files
        
        # 按修改时间排序，获取最新的
        latest_file = max(release_files, key=lambda x: x.stat().st_mtime)
        
        print(f"选择最新发版包: {latest_file.name}")
        
        return self.flash_release_package(str(latest_file), verify_before_flash)

    def _create_self_contained_config(self, output_dir: Path):
        """创建自包含的OpenOCD配置文件"""
        # 创建openocd_config目录
        config_dir = output_dir / "openocd_config"
        config_dir.mkdir(exist_ok=True)
        
        # 复制必要的配置文件
        source_config_dir = self.application_dir / "Openocd_Script"
        
        config_files = ["ST-LINK-QSPIFLASH.cfg", "stlink.cfg", "stm32h7x.cfg"]
        for config_file in config_files:
            source_file = source_config_dir / config_file
            if source_file.exists():
                shutil.copy2(source_file, config_dir / config_file)
        
        # 修改ST-LINK-QSPIFLASH.cfg中的路径引用为自包含
        main_config = config_dir / "ST-LINK-QSPIFLASH.cfg"
        if main_config.exists():
            with open(main_config, 'r', encoding='utf-8') as f:
                content = f.read()
            
            # 修改路径引用
            content = content.replace(
                "source [find Openocd_Script/stlink.cfg]",
                "source [find openocd_config/stlink.cfg]"
            )
            content = content.replace(
                "source [find Openocd_Script/stm32h7x.cfg]",
                "source [find openocd_config/stm32h7x.cfg]"
            )
            
            with open(main_config, 'w', encoding='utf-8') as f:
                f.write(content)
        
        print("✓ 创建自包含配置文件")

def main():
    parser = argparse.ArgumentParser(
        description="STM32 HBox 发版打包工具",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
使用示例:
  从设备提取并打包:
    python release_packager.py --extract-from-device --version 1.0.0
  
  从Flash转储打包:
    python release_packager.py --extract-from-dump flash_dump.bin --version 1.0.1
  
  从构建输出打包:
    python release_packager.py --extract-from-build --version 1.0.2
  
  验证发版包:
    python release_packager.py --verify package.zip
  
  刷写指定发版包:
    python release_packager.py --flash package.zip
  
  刷写最新发版包:
    python release_packager.py --flash-latest
    python release_packager.py --flash-latest --slot A
  
  列出发版包:
    python release_packager.py --list

注意: 槽B发版包会自动重新编译Application以适配槽B地址空间
        """
    )
    
    parser.add_argument("--version", help="发版版本号")
    parser.add_argument("--extract-from-device", action="store_true", 
                        help="从连接的设备提取数据")
    parser.add_argument("--extract-from-dump", metavar="DUMP_FILE",
                        help="从Flash转储文件提取数据")
    parser.add_argument("--extract-from-build", action="store_true",
                        help="从构建输出提取数据")
    parser.add_argument("--verify", metavar="PACKAGE_FILE",
                        help="验证发版包")
    parser.add_argument("--flash", metavar="PACKAGE_FILE",
                        help="刷写指定发版包")
    parser.add_argument("--flash-latest", action="store_true",
                        help="刷写最新发版包")
    parser.add_argument("--slot", choices=["A", "B"],
                        help="指定槽（用于--flash-latest）")
    parser.add_argument("--no-verify", action="store_true",
                        help="刷写前跳过发版包验证")
    parser.add_argument("--list", action="store_true",
                        help="列出所有发版包")
    
    args = parser.parse_args()
    
    packager = ReleasePackager()
    
    try:
        if args.list:
            packager.list_releases()
        elif args.verify:
            packager.verify_release_package(args.verify)
        elif args.flash:
            verify_before_flash = not args.no_verify
            success = packager.flash_release_package(args.flash, verify_before_flash)
            return 0 if success else 1
        elif args.flash_latest:
            verify_before_flash = not args.no_verify
            success = packager.flash_latest_release(args.slot, verify_before_flash)
            return 0 if success else 1
        elif args.extract_from_device:
            if not args.version:
                print("错误: 请指定版本号 --version")
                return 1
            packager.create_complete_release(args.version, "device")
        elif args.extract_from_dump:
            if not args.version:
                print("错误: 请指定版本号 --version")
                return 1
            packager.create_complete_release(args.version, "dump", 
                                             dump_file=args.extract_from_dump)
        elif args.extract_from_build:
            if not args.version:
                print("错误: 请指定版本号 --version")
                return 1
            packager.create_complete_release(args.version, "build")
        else:
            parser.print_help()
            return 1
            
    except KeyboardInterrupt:
        print("\n操作已取消")
        return 1
    except Exception as e:
        print(f"错误: {e}")
        return 1
    
    return 0

if __name__ == "__main__":
    sys.exit(main()) 