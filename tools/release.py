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
from pathlib import Path
from datetime import datetime
from typing import Dict, List, Optional, NamedTuple, Tuple, Any

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
        """刷写完整的release包"""
        print("=" * 60)
        print("STM32 HBox Release包刷写工具")
        print("=" * 60)
        
        try:
            # 解压release包
            manifest, temp_dir = self.extract_release_package(package_path)
            
            # 确定目标槽位
            package_slot = manifest.get('slot', 'A')
            if target_slot:
                if target_slot != package_slot:
                    print(f"警告: 指定槽位 {target_slot} 与包槽位 {package_slot} 不匹配")
                    print(f"将使用指定槽位 {target_slot} 的地址映射")
                final_slot = target_slot
            else:
                final_slot = package_slot
            
            print(f"目标槽位: {final_slot}")
            
            # 获取槽位地址映射
            if final_slot not in self.slot_addresses:
                raise ValueError(f"不支持的槽位: {final_slot}")
            
            slot_config = self.slot_addresses[final_slot]
            
            # 获取要刷写的组件
            available_components = manifest.get('components', [])
            if not available_components:
                raise ValueError("Release包中没有组件")
            
            # 过滤要刷写的组件
            if components:
                # 用户指定了特定组件
                components_to_flash = []
                for comp in available_components:
                    if comp['name'] in components:
                        components_to_flash.append(comp)
                
                if not components_to_flash:
                    raise ValueError(f"指定的组件不存在: {components}")
                    
                missing_components = set(components) - {c['name'] for c in components_to_flash}
                if missing_components:
                    print(f"警告: 以下组件在包中不存在: {missing_components}")
            else:
                # 刷写所有组件
                components_to_flash = available_components
            
            print(f"将刷写 {len(components_to_flash)} 个组件")
            print()
            
            # 逐个刷写组件
            success_count = 0
            failed_components = []
            
            for comp in components_to_flash:
                comp_name = comp['name']
                comp_file = comp['file']
                
                # 检查组件地址映射
                if comp_name not in slot_config:
                    print(f"警告: 未知组件类型 {comp_name}，跳过")
                    continue
                
                target_address = slot_config[comp_name]
                component_path = temp_dir / comp_file
                
                if self.flash_component(component_path, target_address, comp_name):
                    success_count += 1
                else:
                    failed_components.append(comp_name)
            
            # 显示结果
            print("\n" + "=" * 60)
            print("刷写完成")
            print("=" * 60)
            print(f"成功: {success_count}/{len(components_to_flash)} 个组件")
            
            if failed_components:
                print(f"失败的组件: {', '.join(failed_components)}")
                return False
            else:
                print("✓ 所有组件刷写成功!")
                return True
                
        except Exception as e:
            print(f"刷写失败: {e}")
            return False
        finally:
            # 清理临时目录
            self.cleanup_temp_dir()

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
