#!/usr/bin/env python3
"""
STM32 H7xx 双槽固件构建工具
支持构建 bootloader 和 application (槽A/槽B)
支持烧录 bootloader 和 application
"""

import os
import sys
import argparse
import subprocess
import shutil
import json
import multiprocessing
from pathlib import Path
from typing import Optional, Dict, Any

class BuildTool:
    def __init__(self):
        self.project_root = Path(__file__).parent.parent
        self.bootloader_dir = self.project_root / "bootloader"
        self.application_dir = self.project_root / "application"
        self.tools_dir = self.project_root / "tools"
        self.build_config_file = self.tools_dir / "build_config.json"
        
        # 自动检测CPU核心数
        self.cpu_count = multiprocessing.cpu_count()
        
        # 槽地址配置 - 修正槽B地址，保持槽A兼容性
        self.slot_config = {
            "A": {
                "address": "0x90000000",        # 槽A Application地址 (保持原有兼容地址)
                "webres_address": "0x90100000", # 槽A WebResources地址  
                "adc_address": "0x90280000"     # 槽A ADC Mapping地址
            },
            "B": {
                "address": "0x902B0000",        # 槽B Application地址 (修正为正确地址)
                "webres_address": "0x903B0000", # 槽B WebResources地址 (修正为正确地址)
                "adc_address": "0x90530000"     # 槽B ADC Mapping地址 (修正为正确地址)
            }
        }
        
        # 当前链接脚本使用的默认地址
        self.default_addresses = {
            "flash": "0x90000000",
            "webres": "0x90200000", 
            "adc": "0x90300000"
        }
        
        # 加载构建配置
        self.load_build_config()

    def load_build_config(self):
        """加载构建配置"""
        # 默认使用80%的CPU核心数，最少2个，最多16个
        default_jobs = max(2, min(16, int(self.cpu_count * 0.8)))
        
        default_config = {
            "gcc_path": "",
            "openocd_path": "openocd",
            "openocd_interface": "stlink",
            "openocd_target": "stm32h7x",
            "parallel_jobs": default_jobs,  # 并行编译任务数
            "last_build": {
                "bootloader": None,
                "application": None
            }
        }
        
        if self.build_config_file.exists():
            try:
                with open(self.build_config_file, 'r', encoding='utf-8') as f:
                    self.config = json.load(f)
            except Exception as e:
                print(f"警告: 加载配置文件失败: {e}")
                self.config = default_config
        else:
            self.config = default_config
            
        # 确保所有必需的字段存在
        for key in default_config:
            if key not in self.config:
                self.config[key] = default_config[key]

    def save_build_config(self):
        """保存构建配置"""
        try:
            os.makedirs(self.tools_dir, exist_ok=True)
            with open(self.build_config_file, 'w', encoding='utf-8') as f:
                json.dump(self.config, f, indent=2, ensure_ascii=False)
        except Exception as e:
            print(f"警告: 保存配置文件失败: {e}")

    def run_command(self, cmd: list, cwd: Path, env: Optional[Dict[str, str]] = None) -> bool:
        """执行命令"""
        print(f"执行命令: {' '.join(cmd)}")
        print(f"工作目录: {cwd}")
        
        try:
            # 准备环境变量
            exec_env = os.environ.copy()
            if env:
                exec_env.update(env)
                
            # 如果配置了GCC路径，添加到PATH
            if self.config.get("gcc_path"):
                gcc_path = Path(self.config["gcc_path"])
                if gcc_path.exists():
                    exec_env["PATH"] = str(gcc_path) + os.pathsep + exec_env.get("PATH", "")
            
            result = subprocess.run(
                cmd, 
                cwd=cwd, 
                env=exec_env,
                check=True,
                capture_output=False  # 让输出直接显示
            )
            return True
        except subprocess.CalledProcessError as e:
            print(f"命令执行失败，退出码: {e.returncode}")
            return False
        except FileNotFoundError:
            print(f"命令未找到: {cmd[0]}")
            return False

    def backup_file(self, file_path: Path) -> Path:
        """备份文件"""
        backup_path = file_path.with_suffix(file_path.suffix + '.bak')
        if file_path.exists():
            shutil.copy2(file_path, backup_path)
            print(f"已备份: {file_path} -> {backup_path}")
        return backup_path

    def restore_file(self, backup_path: Path):
        """恢复文件"""
        original_path = Path(str(backup_path).replace('.bak', ''))
        if backup_path.exists():
            shutil.move(backup_path, original_path)
            print(f"已恢复: {backup_path} -> {original_path}")

    def modify_linker_script_for_slot(self, slot: str) -> Optional[Path]:
        """修改链接脚本以适应指定槽"""
        linker_script = self.application_dir / "STM32H750XBHx_FLASH.ld"
        
        if not linker_script.exists():
            print(f"错误: 链接脚本不存在: {linker_script}")
            return None
            
        # 备份原始文件
        backup_path = self.backup_file(linker_script)
        
        try:
            # 读取原始文件
            with open(linker_script, 'r', encoding='utf-8') as f:
                content = f.read()
            
            # 获取槽配置
            slot_cfg = self.slot_config[slot]
            
            # 使用更精确的正则表达式替换地址
            import re
            
            # 替换FLASH地址 - 匹配 FLASH (rx) : ORIGIN = 0x90000000, LENGTH = 2048K
            flash_pattern = r'(FLASH\s*\(rx\)\s*:\s*ORIGIN\s*=\s*)0x[0-9A-Fa-f]+(\s*,\s*LENGTH\s*=\s*\d+K)'
            new_flash_line = f'\\g<1>{slot_cfg["address"]}\\g<2>'
            content = re.sub(flash_pattern, new_flash_line, content)
            
            # 替换WEB_RESOURCES_FLASH地址 - 匹配 WEB_RESOURCES_FLASH (rx) : ORIGIN = 0x90200000, LENGTH = 1024K  
            webres_pattern = r'(WEB_RESOURCES_FLASH\s*\(rx\)\s*:\s*ORIGIN\s*=\s*)0x[0-9A-Fa-f]+(\s*,\s*LENGTH\s*=\s*\d+K)'
            new_webres_line = f'\\g<1>{slot_cfg["webres_address"]}\\g<2>'
            content = re.sub(webres_pattern, new_webres_line, content)
            
            # 替换ADC_VALUES_MAPPING_FLASH地址 - 匹配 ADC_VALUES_MAPPING_FLASH (rx) : ORIGIN = 0x90300000, LENGTH = 1024K
            adc_pattern = r'(ADC_VALUES_MAPPING_FLASH\s*\(rx\)\s*:\s*ORIGIN\s*=\s*)0x[0-9A-Fa-f]+(\s*,\s*LENGTH\s*=\s*\d+K)'
            new_adc_line = f'\\g<1>{slot_cfg["adc_address"]}\\g<2>'
            content = re.sub(adc_pattern, new_adc_line, content)
            
            # 修复CONFIG_FLASH地址冲突问题 - 将其设置为一个不冲突的地址
            # CONFIG_FLASH应该有自己独立的地址空间
            config_flash_addr = hex(int(slot_cfg["adc_address"], 16) + 0x100000)  # ADC地址 + 1MB
            config_pattern = r'(CONFIG_FLASH\s*\(rx\)\s*:\s*ORIGIN\s*=\s*)0x[0-9A-Fa-f]+(\s*,\s*LENGTH\s*=\s*\d+K)'
            new_config_line = f'\\g<1>{config_flash_addr}\\g<2>'
            content = re.sub(config_pattern, new_config_line, content)
            
            # 写入修改后的文件
            with open(linker_script, 'w', encoding='utf-8') as f:
                f.write(content)
                
            print(f"已修改链接脚本以适应槽{slot}:")
            print(f"  FLASH: {slot_cfg['address']}")
            print(f"  WEB_RESOURCES: {slot_cfg['webres_address']}")
            print(f"  ADC_MAPPING: {slot_cfg['adc_address']}")
            print(f"  CONFIG_FLASH: {config_flash_addr} (修复地址冲突)")
            
            return backup_path
            
        except Exception as e:
            print(f"错误: 修改链接脚本失败: {e}")
            # 恢复备份
            self.restore_file(backup_path)
            return None

    def build_bootloader(self) -> bool:
        """构建 bootloader"""
        print("=" * 50)
        print("构建 Bootloader")
        print("=" * 50)
        
        if not self.bootloader_dir.exists():
            print(f"错误: Bootloader目录不存在: {self.bootloader_dir}")
            return False
        
        # 获取并行任务数
        jobs = self.config.get("parallel_jobs", self.cpu_count)
        print(f"使用 {jobs} 个并行任务进行编译 (CPU核心数: {self.cpu_count})")
            
        # 清理并构建
        success = True
        print("清理旧的构建文件...")
        if not self.run_command(["make", "clean"], self.bootloader_dir):
            success = False
        
        if success:
            print(f"开始多线程编译 (j{jobs})...")
            if not self.run_command(["make", f"-j{jobs}"], self.bootloader_dir):
                success = False
            
        if success:
            print("✅ Bootloader 构建成功")
            self.config["last_build"]["bootloader"] = "成功"
        else:
            print("❌ Bootloader 构建失败")
            self.config["last_build"]["bootloader"] = "失败"
            
        self.save_build_config()
        return success

    def build_application(self, slot: str) -> bool:
        """构建 application"""
        print("=" * 50)
        print(f"构建 Application (槽{slot})")
        print("=" * 50)
        
        if slot not in ["A", "B"]:
            print("错误: 槽必须是 A 或 B")
            return False
            
        if not self.application_dir.exists():
            print(f"错误: Application目录不存在: {self.application_dir}")
            return False
        
        # 获取并行任务数
        jobs = self.config.get("parallel_jobs", self.cpu_count)
        print(f"使用 {jobs} 个并行任务进行编译 (CPU核心数: {self.cpu_count})")
        
        # 修改链接脚本
        backup_path = self.modify_linker_script_for_slot(slot)
        if not backup_path:
            return False
            
        try:
            # 清理并构建
            success = True
            print("清理旧的构建文件...")
            if not self.run_command(["make", "clean"], self.application_dir):
                success = False
            
            if success:
                print(f"开始多线程编译 (j{jobs})...")
                if not self.run_command(["make", f"-j{jobs}"], self.application_dir):
                    success = False
                
            if success:
                # 重命名生成的文件以包含槽信息
                build_dir = self.application_dir / "build"
                original_bin = build_dir / "application.bin"
                original_elf = build_dir / "application.elf"
                original_hex = build_dir / "application.hex"
                
                slot_bin = build_dir / f"application_slot_{slot}.bin"
                slot_elf = build_dir / f"application_slot_{slot}.elf"
                slot_hex = build_dir / f"application_slot_{slot}.hex"
                
                # 复制ELF文件
                if original_elf.exists():
                    shutil.copy2(original_elf, slot_elf)
                    print(f"已生成: {slot_elf}")
                    
                    # 从ELF文件重新生成正确的HEX和BIN文件
                    try:
                        # 生成HEX文件
                        hex_cmd = [
                            "arm-none-eabi-objcopy",
                            "-O", "ihex",
                            str(slot_elf),
                            str(slot_hex)
                        ]
                        if self.run_command(hex_cmd, self.application_dir):
                            print(f"已生成: {slot_hex}")
                        
                        # 生成BIN文件
                        bin_cmd = [
                            "arm-none-eabi-objcopy", 
                            "-O", "binary",
                            str(slot_elf),
                            str(slot_bin)
                        ]
                        if self.run_command(bin_cmd, self.application_dir):
                            print(f"已生成: {slot_bin}")
                            
                        # 显示文件大小信息
                        if slot_hex.exists():
                            hex_size = slot_hex.stat().st_size
                            print(f"  槽{slot} HEX文件大小: {hex_size:,} 字节")
                        if slot_bin.exists():
                            bin_size = slot_bin.stat().st_size
                            print(f"  槽{slot} BIN文件大小: {bin_size:,} 字节")
                            
                    except Exception as e:
                        print(f"警告: 生成槽{slot}文件时出错: {e}")
                        # 如果objcopy失败，回退到复制原始文件
                        if original_bin.exists():
                            shutil.copy2(original_bin, slot_bin)
                            print(f"已复制: {slot_bin}")
                        if original_hex.exists():
                            shutil.copy2(original_hex, slot_hex)
                            print(f"已复制: {slot_hex}")
                else:
                    print(f"警告: 原始ELF文件不存在: {original_elf}")
                
                print(f"✅ Application 槽{slot} 构建成功")
                self.config["last_build"]["application"] = f"槽{slot} 成功"
            else:
                print(f"❌ Application 槽{slot} 构建失败")
                self.config["last_build"]["application"] = f"槽{slot} 失败"
                
        finally:
            # 恢复原始链接脚本
            self.restore_file(backup_path)
            
        self.save_build_config()
        return success

    def flash_bootloader(self) -> bool:
        """烧录 bootloader"""
        print("=" * 50)
        print("烧录 Bootloader")
        print("=" * 50)
        
        bootloader_elf = self.bootloader_dir / "build" / "bootloader.elf"
        if not bootloader_elf.exists():
            print(f"错误: Bootloader ELF文件不存在: {bootloader_elf}")
            print("请先构建 bootloader")
            return False
            
        # 使用make flash（如果Makefile支持）
        if (self.bootloader_dir / "Makefile").exists():
            # 检查Makefile是否有flash目标
            try:
                with open(self.bootloader_dir / "Makefile", 'r') as f:
                    makefile_content = f.read()
                if "flash:" in makefile_content:
                    return self.run_command(["make", "flash"], self.bootloader_dir)
            except:
                pass
        
        # 转换路径为字符串，确保Windows兼容性
        bootloader_elf_str = str(bootloader_elf).replace('\\', '/')
        
        # 使用OpenOCD烧录
        openocd_cmd = [
            self.config["openocd_path"],
            "-f", f"interface/{self.config['openocd_interface']}.cfg",
            "-f", f"target/{self.config['openocd_target']}.cfg",
            "-c", "init",
            "-c", "halt",
            "-c", f"flash write_image erase {bootloader_elf_str}",
            "-c", "reset run",
            "-c", "shutdown"
        ]
        
        return self.run_command(openocd_cmd, self.bootloader_dir)

    def flash_application(self, slot: str) -> bool:
        """烧录应用程序到指定槽"""
        print(f"正在烧录应用程序到槽 {slot}...")
        
        # 首先尝试使用Makefile的flash目标（推荐方式）
        makefile_result = self._flash_using_makefile(slot)
        if makefile_result:
            return True
            
        # 如果Makefile不可用，使用手动OpenOCD配置
        return self._flash_using_openocd(slot)

    def flash_web_resources(self, slot: str) -> bool:
        """烧录Web Resources到指定槽"""
        print("=" * 50)
        print(f"烧录 Web Resources 到槽 {slot}")
        print("=" * 50)
        
        # 检查Web Resources文件是否存在
        web_resources_file = self.application_dir / "Libs" / "httpd" / "ex_fsdata.bin"
        if not web_resources_file.exists():
            print(f"错误: Web Resources文件不存在: {web_resources_file}")
            return False
            
        # 获取槽配置
        if slot not in self.slot_config:
            print(f"错误: 未知槽 {slot}")
            return False
            
        slot_cfg = self.slot_config[slot]
        webres_address = slot_cfg["webres_address"]
        
        # 计算物理地址（去掉0x90000000的内存映射基址）
        physical_address = hex(int(webres_address, 16) - 0x90000000)
        
        print(f"Web Resources文件: {web_resources_file}")
        print(f"目标地址: {webres_address} (物理地址: {physical_address})")
        
        # 首先尝试使用Makefile方式
        makefile_result = self._flash_web_resources_using_makefile(slot, webres_address, physical_address)
        if makefile_result:
            return True
            
        # 如果失败，使用OpenOCD方式
        return self._flash_web_resources_using_openocd(slot, webres_address, physical_address)
        
    def _flash_using_makefile(self, slot: str) -> bool:
        """使用Makefile的flash目标进行烧录"""
        try:
            makefile_path = self.application_dir / "Makefile"
            if not makefile_path.exists():
                return False
                
            # 对于特定槽，需要临时复制文件
            temp_files = []
            hex_file = None
            
            if slot == "A":
                slot_hex = self.application_dir / "build" / "application_slot_A.hex"
                if slot_hex.exists():
                    default_hex = self.application_dir / "build" / "application.hex"
                    temp_files.append(self.backup_file(default_hex))
                    shutil.copy2(slot_hex, default_hex)
                    hex_file = default_hex
                    print(f"使用槽A的HEX文件: {hex_file}")
                else:
                    # 如果没有槽A特定文件，直接使用默认文件
                    hex_file = self.application_dir / "build" / "application.hex"
                    print(f"槽A特定文件不存在，使用默认HEX文件: {hex_file}")
            elif slot == "B":
                slot_hex = self.application_dir / "build" / "application_slot_B.hex"
                if slot_hex.exists():
                    default_hex = self.application_dir / "build" / "application.hex"
                    temp_files.append(self.backup_file(default_hex))
                    shutil.copy2(slot_hex, default_hex)
                    hex_file = default_hex
                    print(f"使用槽B的HEX文件: {hex_file}")
                else:
                    # 如果没有槽B特定文件，直接使用默认文件
                    hex_file = self.application_dir / "build" / "application.hex"
                    print(f"槽B特定文件不存在，使用默认HEX文件: {hex_file}")
            else:
                # 默认槽，直接使用application.hex
                hex_file = self.application_dir / "build" / "application.hex"
                
            if not hex_file or not hex_file.exists():
                print(f"错误: HEX文件不存在: {hex_file}")
                return False
                
            # 使用Makefile的flash目标
            cmd = ["make", "flash"]
            result = subprocess.run(
                cmd,
                cwd=self.application_dir,
                capture_output=True,
                text=True,
                timeout=120
            )
            
            # 恢复备份文件
            for backup_path in temp_files:
                if backup_path and backup_path.exists():
                    original_path = backup_path.with_suffix('')
                    shutil.move(backup_path, original_path)
                    
            if result.returncode == 0:
                print(f"✓ Makefile烧录成功")
                if "wrote" in result.stdout:
                    for line in result.stdout.split('\n'):
                        if "wrote" in line:
                            print(f"  {line.strip()}")
                return True
            else:
                print(f"Makefile烧录失败: {result.stderr}")
                return False
                
        except Exception as e:
            print(f"Makefile烧录异常: {e}")
            return False
    
    def _flash_using_openocd(self, slot: str) -> bool:
        """使用手动OpenOCD配置进行烧录"""
        try:
            # 确定HEX文件路径
            if slot == "A":
                hex_file = self.application_dir / "build" / "application_slot_A.hex"
                if not hex_file.exists():
                    hex_file = self.application_dir / "build" / "application.hex"
                    print(f"槽A特定文件不存在，使用默认HEX文件: {hex_file}")
            elif slot == "B":
                hex_file = self.application_dir / "build" / "application_slot_B.hex"
                if not hex_file.exists():
                    hex_file = self.application_dir / "build" / "application.hex"
                    print(f"槽B特定文件不存在，使用默认HEX文件: {hex_file}")
            else:
                hex_file = self.application_dir / "build" / "application.hex"
                
            if not hex_file.exists():
                print(f"错误: HEX文件不存在: {hex_file}")
                return False
                
            # 优先使用QSPI Flash配置
            openocd_cfg = self.application_dir / "Openocd_Script" / "ST-LINK-QSPIFLASH.cfg"
            if not openocd_cfg.exists():
                # 备用配置
                openocd_cfg = self.application_dir / "Openocd_Script" / "openocd.cfg"
                if not openocd_cfg.exists():
                    print(f"错误: OpenOCD配置文件不存在")
                    return False
                    
            print(f"使用OpenOCD配置: {openocd_cfg}")
            print(f"烧录HEX文件: {hex_file}")
            
            # 使用与Makefile相同的OpenOCD命令
            cmd = [
                "openocd",
                "-d0",
                "-f", str(openocd_cfg),
                "-c", "init",
                "-c", "halt", 
                "-c", "reset init",
                "-c", f"flash write_image erase {hex_file} 0x00000000",
                "-c", f"flash verify_image {hex_file} 0x00000000",
                "-c", "reset run",
                "-c", "shutdown"
            ]
            
            result = subprocess.run(
                cmd,
                cwd=self.application_dir,
                capture_output=True,
                text=True,
                timeout=120
            )
            
            if result.returncode == 0:
                print(f"✓ OpenOCD烧录成功")
                if "wrote" in result.stdout:
                    for line in result.stdout.split('\n'):
                        if "wrote" in line:
                            print(f"  {line.strip()}")
                return True
            else:
                print(f"OpenOCD烧录失败:")
                print(f"stdout: {result.stdout}")
                print(f"stderr: {result.stderr}")
                return False
                
        except Exception as e:
            print(f"OpenOCD烧录异常: {e}")
            return False

    def _flash_web_resources_using_makefile(self, slot: str, webres_address: str, physical_address: str) -> bool:
        """使用Makefile方式烧录Web Resources"""
        try:
            # 使用自定义的OpenOCD命令，不依赖Makefile的固定地址
            web_resources_file = self.application_dir / "Libs" / "httpd" / "ex_fsdata.bin"
            
            # 确保路径格式正确（Windows兼容性）
            web_resources_path = str(web_resources_file).replace('\\', '/')
            
            # **重要修改：直接使用内存映射地址，不进行物理地址转换**
            # 因为STM32H7的QSPI Flash映射可能不是简单的线性映射
            memory_mapped_address = webres_address
            
            # 构建OpenOCD命令
            cmd = [
                "openocd",
                "-d0", 
                "-f", "Openocd_Script/ST-LINK-QSPIFLASH.cfg",
                "-c", "init",
                "-c", "halt",
                "-c", "reset init",
                "-c", f"flash write_image erase \"{web_resources_path}\" {memory_mapped_address}",
                "-c", f"flash verify_image \"{web_resources_path}\" {memory_mapped_address}",
                "-c", "reset",
                "-c", "shutdown"
            ]
            
            print(f"执行命令: {' '.join(cmd)}")
            print(f"Web Resources文件路径: {web_resources_path}")
            print(f"使用内存映射地址: {memory_mapped_address}")
            
            result = subprocess.run(
                cmd,
                cwd=self.application_dir,
                capture_output=True,
                text=True,
                timeout=120
            )
            
            if result.returncode == 0:
                print(f"✓ Web Resources烧录成功到槽{slot}")
                if "wrote" in result.stdout:
                    for line in result.stdout.split('\n'):
                        if "wrote" in line:
                            print(f"  {line.strip()}")
                return True
            else:
                print(f"Web Resources烧录失败:")
                print(f"stdout: {result.stdout}")
                print(f"stderr: {result.stderr}")
                return False
                
        except Exception as e:
            print(f"Web Resources烧录异常: {e}")
            return False
    
    def _flash_web_resources_using_openocd(self, slot: str, webres_address: str, physical_address: str) -> bool:
        """使用原生OpenOCD方式烧录Web Resources（备用方案）"""
        # 与_flash_web_resources_using_makefile相同的实现
        return self._flash_web_resources_using_makefile(slot, webres_address, physical_address)

    def show_status(self):
        """显示构建状态"""
        print("=" * 50)
        print("构建状态")
        print("=" * 50)
        
        print(f"项目根目录: {self.project_root}")
        print(f"GCC路径: {self.config.get('gcc_path', '系统PATH')}")
        print(f"OpenOCD: {self.config.get('openocd_path', 'openocd')}")
        print(f"CPU核心数: {self.cpu_count}")
        print(f"并行编译任务数: {self.config.get('parallel_jobs', self.cpu_count)}")
        print()
        
        print("最近构建状态:")
        print(f"  Bootloader: {self.config['last_build'].get('bootloader', '未构建')}")
        print(f"  Application: {self.config['last_build'].get('application', '未构建')}")
        print()
        
        # 检查文件存在性
        print("构建文件状态:")
        
        bootloader_elf = self.bootloader_dir / "build" / "bootloader.elf"
        print(f"  Bootloader ELF: {'✅' if bootloader_elf.exists() else '❌'} {bootloader_elf}")
        
        build_dir = self.application_dir / "build"
        for slot in ["A", "B"]:
            slot_elf = build_dir / f"application_slot_{slot}.elf"
            print(f"  Application 槽{slot} ELF: {'✅' if slot_elf.exists() else '❌'} {slot_elf}")
            
        # 检查Web Resources文件
        web_resources_file = self.application_dir / "Libs" / "httpd" / "ex_fsdata.bin"
        print(f"  Web Resources: {'✅' if web_resources_file.exists() else '❌'} {web_resources_file}")
        
        # 显示槽地址配置
        print("\n槽地址配置:")
        for slot, config in self.slot_config.items():
            print(f"  槽{slot}:")
            print(f"    Application: {config['address']}")
            print(f"    WebResources: {config['webres_address']}")
            print(f"    ADC Mapping: {config['adc_address']}")

    def build_and_flash_complete_slot(self, slot: str) -> bool:
        """构建并烧录完整槽（Application + Web Resources）"""
        print("=" * 60)
        print(f"构建并烧录完整槽 {slot}")
        print("=" * 60)
        
        # 1. 构建Application
        print("1/3 构建Application...")
        build_success = self.build_application(slot)
        if not build_success:
            print("❌ Application构建失败，停止操作")
            return False
            
        # 2. 烧录Application  
        print("\n2/3 烧录Application...")
        app_flash_success = self.flash_application(slot)
        if not app_flash_success:
            print("❌ Application烧录失败，停止操作")
            return False
            
        # 3. 烧录Web Resources
        print("\n3/3 烧录Web Resources...")
        web_flash_success = self.flash_web_resources(slot)
        if not web_flash_success:
            print("❌ Web Resources烧录失败")
            return False
            
        print(f"\n✅ 完整槽 {slot} 构建并烧录成功！")
        print("槽内容:")
        slot_cfg = self.slot_config[slot]
        print(f"  - Application: {slot_cfg['address']}")
        print(f"  - WebResources: {slot_cfg['webres_address']}")
        print(f"  - ADC Mapping: {slot_cfg['adc_address']}")
        
        return True

def main():
    parser = argparse.ArgumentParser(
        description="STM32 H7xx 双槽固件构建工具",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
使用示例:
  %(prog)s build bootloader              # 构建bootloader
  %(prog)s build app A                   # 构建application槽A  
  %(prog)s build app B                   # 构建application槽B
  %(prog)s build app A -j8               # 使用8个并行任务构建
  %(prog)s flash bootloader              # 烧录bootloader
  %(prog)s flash app A                   # 烧录application槽A
  %(prog)s flash app B                   # 烧录application槽B
  %(prog)s flash web A                   # 烧录Web Resources到槽A
  %(prog)s flash web B                   # 烧录Web Resources到槽B
  %(prog)s flash all A                   # 烧录application和Web Resources到槽A
  %(prog)s flash all B                   # 烧录application和Web Resources到槽B
  %(prog)s deploy A                      # 一键构建并烧录完整槽A
  %(prog)s deploy B                      # 一键构建并烧录完整槽B
  %(prog)s status                        # 显示构建状态
  %(prog)s config jobs 8                 # 设置默认并行任务数为8
        """
    )
    
    # 全局选项
    parser.add_argument("-j", "--jobs", type=int, metavar="N", 
                       help="并行编译任务数 (默认: CPU核心数的80%%)")
    
    subparsers = parser.add_subparsers(dest="command", help="可用命令")
    
    # build 命令
    build_parser = subparsers.add_parser("build", help="构建固件")
    build_parser.add_argument("target", choices=["bootloader", "app"], help="构建目标")
    build_parser.add_argument("slot", nargs="?", choices=["A", "B"], help="Application槽选择")
    build_parser.add_argument("-j", "--jobs", type=int, metavar="N", 
                             help="并行编译任务数 (覆盖全局设置)")
    
    # flash 命令
    flash_parser = subparsers.add_parser("flash", help="烧录固件")
    flash_parser.add_argument("target", choices=["bootloader", "app", "web", "all"], help="烧录目标")
    flash_parser.add_argument("slot", nargs="?", choices=["A", "B"], help="槽选择 (app/web/all时必须)")
    
    # deploy 命令 - 一键构建并烧录
    deploy_parser = subparsers.add_parser("deploy", help="一键构建并烧录完整槽")
    deploy_parser.add_argument("slot", choices=["A", "B"], help="目标槽")
    deploy_parser.add_argument("-j", "--jobs", type=int, metavar="N", 
                              help="并行编译任务数 (覆盖全局设置)")
    
    # status 命令
    subparsers.add_parser("status", help="显示构建状态")
    
    # config 命令
    config_parser = subparsers.add_parser("config", help="配置设置")
    config_parser.add_argument("setting", choices=["jobs"], help="配置项")
    config_parser.add_argument("value", type=int, help="配置值")
    
    args = parser.parse_args()
    
    if not args.command:
        parser.print_help()
        return 1
        
    tool = BuildTool()
    
    # 处理并行任务数设置
    if hasattr(args, 'jobs') and args.jobs:
        if args.jobs < 1 or args.jobs > 32:
            print("错误: 并行任务数必须在1-32之间")
            return 1
        tool.config["parallel_jobs"] = args.jobs
        tool.save_build_config()
        print(f"临时设置并行任务数为: {args.jobs}")
    
    try:
        if args.command == "build":
            if args.target == "bootloader":
                success = tool.build_bootloader()
            elif args.target == "app":
                if not args.slot:
                    print("错误: 构建application时必须指定槽 (A 或 B)")
                    return 1
                success = tool.build_application(args.slot)
            return 0 if success else 1
            
        elif args.command == "flash":
            if args.target == "bootloader":
                success = tool.flash_bootloader()
            elif args.target == "app":
                if not args.slot:
                    print("错误: 烧录application时必须指定槽 (A 或 B)")
                    return 1
                success = tool.flash_application(args.slot)
            elif args.target == "web":
                if not args.slot:
                    print("错误: 烧录Web Resources时必须指定槽 (A 或 B)")
                    return 1
                success = tool.flash_web_resources(args.slot)
            elif args.target == "all":
                if not args.slot:
                    print("错误: 烧录完整固件时必须指定槽 (A 或 B)")
                    return 1
                print("=" * 50)
                print(f"烧录完整固件到槽 {args.slot}")
                print("=" * 50)
                # 先烧录Application
                print("1/2 烧录Application...")
                app_success = tool.flash_application(args.slot)
                if not app_success:
                    print("❌ Application烧录失败，停止操作")
                    return 1
                    
                print("\n2/2 烧录Web Resources...")
                web_success = tool.flash_web_resources(args.slot)
                if not web_success:
                    print("❌ Web Resources烧录失败")
                    return 1
                    
                print(f"\n✅ 完整固件烧录成功到槽 {args.slot}")
                success = True
            return 0 if success else 1
            
        elif args.command == "deploy":
            success = tool.build_and_flash_complete_slot(args.slot)
            return 0 if success else 1
            
        elif args.command == "status":
            tool.show_status()
            return 0
            
        elif args.command == "config":
            if args.setting == "jobs":
                if args.value < 1 or args.value > 32:
                    print("错误: 并行任务数必须在1-32之间")
                    return 1
                tool.config["parallel_jobs"] = args.value
                tool.save_build_config()
                print(f"✅ 已设置默认并行任务数为: {args.value}")
                return 0
            
    except KeyboardInterrupt:
        print("\n操作被用户取消")
        return 1
    except Exception as e:
        print(f"错误: {e}")
        return 1

if __name__ == "__main__":
    sys.exit(main()) 