#!/usr/bin/env python3
"""
STM32 H7xx 双槽固件构建工具
支持构建 bootloader 和 application (槽A/槽B)
支持烧录 application；V2 bootloader 单独烧录按安全策略拒绝
"""

import os
import sys
import argparse
import subprocess
import shutil
import json
import multiprocessing
import re
import tempfile
from datetime import datetime
from pathlib import Path
from typing import Optional, Dict, Any

# Windows 控制台经常是 GBK，避免输出 Unicode 符号导致打印异常中断流程
if hasattr(sys.stdout, "reconfigure"):
    try:
        sys.stdout.reconfigure(errors="replace")
        sys.stderr.reconfigure(errors="replace")
    except Exception:
        pass

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
        self.shared_addresses = self.load_shared_addresses()

    def load_shared_addresses(self) -> Dict[str, str]:
        shared = {
            "sys_assets_addr": "0x905B0000",
            "sys_assets_size": "0x00040000",
            "user_image_addr": "0x905F0000",
            "user_image_size": "0x00210000",
        }

        board_cfg = self.application_dir / "Core" / "Inc" / "board_cfg.h"
        if not board_cfg.exists():
            return shared

        try:
            text = board_cfg.read_text(encoding="utf-8", errors="ignore")
            for key, macro in [
                ("sys_assets_addr", "SYS_IMAGE_RESOURCES_ADDR"),
                ("sys_assets_size", "SYS_IMAGE_RESOURCES_SIZE"),
                ("user_image_addr", "USER_IMAGE_RESOURCES_ADDR"),
                ("user_image_size", "USER_IMAGE_RESOURCES_SIZE"),
            ]:
                m = re.search(
                    rf"^[ \t]*#define[ \t]+{re.escape(macro)}[ \t]+(0x[0-9A-Fa-f]+)",
                    text,
                    re.MULTILINE,
                )
                if m:
                    shared[key] = m.group(1)
        except Exception:
            return shared

        return shared

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

    def run_command(
        self,
        cmd: list,
        cwd: Path,
        env: Optional[Dict[str, str]] = None,
        *,
        quiet: bool = False,
    ) -> bool:
        """执行命令"""
        if not quiet:
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
            
            if quiet:
                result = subprocess.run(
                    cmd,
                    cwd=cwd,
                    env=exec_env,
                    check=False,
                    text=True,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    errors="replace",
                )
                if result.returncode != 0:
                    detail = (result.stdout or "").strip()
                    if detail:
                        detail = "\n".join(
                            line
                            for line in detail.splitlines()
                            if line.strip()
                            != "DEPRECATED! use 'read_memory' not 'mem2array'"
                        ).strip()
                    print()
                    if detail:
                        print(detail)
                    print(f"命令执行失败，退出码: {result.returncode}")
                    return False
                return True

            subprocess.run(
                cmd,
                cwd=cwd,
                env=exec_env,
                check=True,
                capture_output=False,
            )
            return True
        except subprocess.CalledProcessError as exc:
            print(f"命令执行失败，退出码: {exc.returncode}")
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
        """拒绝不完整的 V2 bootloader 单独烧录事务。"""
        print("=" * 50)
        print("Bootloader 单独烧录已禁用")
        print("=" * 50)
        print(
            "错误: STM32H750xB 的 bootloader、设备身份和防降级记录"
            "共享一个 128KiB erase sector。"
        )
        print(
            "必须使用经过评审的工厂全 sector 事务，一次性恢复 bootloader、"
            "identity 和 minimum security version；现场工具不得擦除该 sector。"
        )
        return False

    def flash_application(self, slot: str) -> bool:
        """烧录应用程序到指定槽"""
        print(f"正在烧录应用程序到槽 {slot}...")

        if slot not in self.slot_config:
            print(f"错误: 未知槽 {slot}")
            return False

        bin_file = (
            self.application_dir
            / "build"
            / f"application_slot_{slot}.bin"
        )
        if not bin_file.exists():
            bin_file = self.application_dir / "build" / "application.bin"
            print(f"槽{slot}特定BIN文件不存在，使用默认文件: {bin_file}")
        if not bin_file.exists():
            print(f"错误: Application BIN文件不存在: {bin_file}")
            return False

        return self._flash_qspi_file_in_chunks(
            bin_file,
            int(self.slot_config[slot]["address"], 16),
            f"Application槽{slot}",
        )

    def _flash_qspi_file_in_chunks(
        self,
        source_file: Path,
        target_address: int,
        label: str,
        *,
        reset_after: bool = True,
        stlink_serial: Optional[str] = None,
        expected_target_uid: Optional[str] = None,
        adapter_speed_khz: Optional[int] = None,
        connect_under_reset_fallback: bool = False,
        runtime_attach: bool = False,
        leave_halted: bool = False,
    ) -> bool:
        """Erase, program and physically verify one complete QSPI image."""
        try:
            source_file = source_file.resolve(strict=True)
            payload_size = source_file.stat().st_size
        except OSError as exc:
            print(f"错误: 无法读取{label}文件: {exc}")
            return False

        if payload_size <= 0:
            print(f"错误: {label}文件为空: {source_file}")
            return False
        if stlink_serial is not None and re.fullmatch(
            r"[0-9A-Fa-f]{24}", stlink_serial
        ) is None:
            print("错误: ST-Link序列号必须是24位十六进制字符")
            return False
        if expected_target_uid is not None and re.fullmatch(
            r"[0-9A-Fa-f]{24}", expected_target_uid
        ) is None:
            print("错误: STM32 UID必须是24位十六进制字符")
            return False
        if stlink_serial is not None and expected_target_uid is None:
            print("错误: 指定ST-Link序列号时必须同时提供目标UID")
            return False
        if adapter_speed_khz is not None and not (
            50 <= adapter_speed_khz <= 10000
        ):
            print("错误: SWD频率必须在50..10000 kHz范围内")
            return False
        if leave_halted and not runtime_attach:
            print("错误: leave_halted只允许用于运行时QSPI事务")
            return False

        qspi_base = 0x90000000
        qspi_size = 0x00800000
        qspi_sector_size = 0x00010000
        target_end = target_address + payload_size
        if (
            target_address < qspi_base
            or target_end > qspi_base + qspi_size
            or target_address % qspi_sector_size != 0
        ):
            print(
                f"错误: {label}目标范围不是有效的64KiB对齐QSPI区域: "
                f"0x{target_address:08X}..0x{target_end:08X}"
            )
            return False

        openocd_cfg = (
            self.application_dir
            / "Openocd_Script"
            / "ST-LINK-QSPIFLASH.cfg"
        )
        if not openocd_cfg.exists():
            print(f"错误: OpenOCD QSPI配置不存在: {openocd_cfg}")
            return False

        if runtime_attach:
            return self._flash_runtime_qspi_file(
                source_file,
                target_address,
                label,
                reset_after=reset_after,
                stlink_serial=stlink_serial,
                expected_target_uid=expected_target_uid,
                adapter_speed_khz=adapter_speed_khz,
                leave_halted=leave_halted,
            )

        print(
            f"{label}: {payload_size} bytes，目标0x{target_address:08X}，"
            "单次整文件擦除/写入/物理回读校验"
        )

        try:
            build_dir = self.application_dir / "build"
            build_dir.mkdir(parents=True, exist_ok=True)
            with tempfile.TemporaryDirectory(
                prefix=".qspi-flash-", dir=build_dir
            ) as temporary:
                temporary_dir = Path(temporary)
                encoded = self._openocd_tcl_braced_path(source_file)
                commands = [
                    "gdb_port disabled",
                    "tcl_port disabled",
                    "telnet_port disabled",
                    "init",
                    "reset init",
                    *(
                        self._openocd_target_assert_commands(
                            expected_target_uid
                        )
                        if expected_target_uid is not None
                        else []
                    ),
                    "flash probe 1",
                    f"flash write_image erase {encoded} "
                    f"0x{target_address:08X} bin",
                    f"flash verify_bank 1 {encoded} "
                    f"0x{target_address - qspi_base:08X}",
                ]
                if reset_after:
                    commands.append("reset run")
                commands.append("shutdown")

                script = temporary_dir / "openocd-single-qspi.tcl"
                script.write_text(
                    "\n".join(commands) + "\n", encoding="utf-8"
                )
                command = [
                    self.config.get("openocd_path", "openocd"),
                    "-d0",
                    "-f",
                    str(openocd_cfg),
                ]
                if stlink_serial is not None:
                    command.extend(
                        ["-c", f"adapter serial {stlink_serial.upper()}"]
                    )
                if adapter_speed_khz is not None:
                    command.extend(
                        ["-c", f"adapter speed {adapter_speed_khz}"]
                    )
                base_command = list(command)
                command.extend(["-f", str(script)])
                success = self.run_command(
                    command, self.application_dir, quiet=True
                )
                if not success and connect_under_reset_fallback:
                    print(
                        f"{label}: normal SWD session failed; "
                        "retrying connect-under-reset"
                    )
                    fallback = [
                        *base_command,
                        "-c",
                        "reset_config connect_assert_srst",
                        "-f",
                        str(script),
                    ]
                    success = self.run_command(
                        fallback, self.application_dir, quiet=True
                    )
                if not success:
                    print(f"错误: {label}单次整文件烧录/校验失败")
                    return False
        except (OSError, ValueError) as exc:
            print(f"错误: 无法准备QSPI整文件烧录事务: {exc}")
            return False

        print(f"{label}单次整文件烧录并通过stmqspi物理回读校验")
        return True

    def _flash_runtime_qspi_file(
        self,
        source_file: Path,
        target_address: int,
        label: str,
        *,
        reset_after: bool,
        stlink_serial: Optional[str],
        expected_target_uid: Optional[str],
        adapter_speed_khz: Optional[int],
        leave_halted: bool,
    ) -> bool:
        """Preserve the accepted single-session runtime-attach QSPI route."""

        openocd_cfg = (
            self.application_dir
            / "Openocd_Script"
            / "ST-LINK-QSPIFLASH.cfg"
        )
        qspi_base = 0x90000000
        payload_size = source_file.stat().st_size
        print(
            f"{label}: {payload_size} bytes，目标0x{target_address:08X}，"
            "使用已验收的运行时stmqspi整文件写入/校验"
        )
        try:
            build_dir = self.application_dir / "build"
            build_dir.mkdir(parents=True, exist_ok=True)
            with tempfile.TemporaryDirectory(
                prefix=".qspi-flash-", dir=build_dir
            ) as temporary:
                temporary_dir = Path(temporary)
                encoded = self._openocd_tcl_braced_path(source_file)
                commands = [
                    "gdb_port disabled",
                    "tcl_port disabled",
                    "telnet_port disabled",
                    "init",
                    "halt",
                    *(
                        self._openocd_target_assert_commands(expected_target_uid)
                        if expected_target_uid is not None
                        else []
                    ),
                    "qspi_init",
                    "flash probe 1",
                    f"flash write_image erase {encoded} "
                    f"0x{target_address:08X} bin",
                    f"flash verify_bank 1 {encoded} "
                    f"0x{target_address - qspi_base:08X}",
                    "qspi_init",
                ]
                if reset_after:
                    # The verified commit is complete before this SYSRESETREQ;
                    # do not wait for the DAP to return.
                    commands.append("mww 0xE000ED0C 0x05FA0004")
                elif not leave_halted:
                    commands.append("resume")
                commands.append("shutdown")

                script = temporary_dir / "openocd-runtime-qspi.tcl"
                script.write_text(
                    "\n".join(commands) + "\n", encoding="utf-8"
                )
                command = [
                    self.config.get("openocd_path", "openocd"),
                    "-d0",
                    "-f",
                    str(openocd_cfg),
                ]
                if stlink_serial is not None:
                    command.extend(
                        ["-c", f"adapter serial {stlink_serial.upper()}"]
                    )
                if adapter_speed_khz is not None:
                    command.extend(
                        ["-c", f"adapter speed {adapter_speed_khz}"]
                    )
                command.extend(["-f", str(script)])
                if not self.run_command(
                    command, self.application_dir, quiet=True
                ):
                    print(f"错误: {label}运行时QSPI整文件写入/校验失败")
                    return False
        except (OSError, ValueError) as exc:
            print(f"错误: 无法准备运行时QSPI烧录事务: {exc}")
            return False

        print(f"{label}运行时QSPI整文件烧录并校验成功")
        return True

    @staticmethod
    def _openocd_tcl_braced_path(
        path: Path,
        *,
        must_exist: bool = True,
    ) -> str:
        """Encode one filesystem path as a safe Tcl braced word."""
        try:
            value = path.resolve(strict=must_exist).as_posix()
        except OSError as exc:
            raise ValueError(f"无法解析OpenOCD Tcl路径: {path}") from exc
        if any(character in value for character in "{}\r\n"):
            raise ValueError("OpenOCD Tcl路径不能包含花括号或换行符")
        return "{" + value + "}"

    @staticmethod
    def _openocd_target_assert_commands(expected_uid: str) -> list[str]:
        """Return Tcl assertions for STM32H750 DEV_ID and UID."""
        uid = expected_uid.upper()
        if re.fullmatch(r"[0-9A-F]{24}", uid) is None:
            raise ValueError("STM32 UID必须是24位十六进制字符")
        commands = [
            "set hbox_dbgmcu_idcode [mrw 0x5C001000]",
            "if {($hbox_dbgmcu_idcode & 0xFFF) != 0x450} "
            "{error {STM32 DEV_ID changed during flash transaction}}",
        ]
        for index in range(3):
            address = 0x1FF1E800 + index * 4
            word = uid[index * 8 : (index + 1) * 8]
            commands.append(
                f"set hbox_uid_{index} [mrw 0x{address:08X}]"
            )
            commands.append(
                f"if {{$hbox_uid_{index} != 0x{word}}} "
                "{error {STM32 UID changed during flash transaction}}"
            )
        return commands

    def _load_internal_flash_security_layout(self) -> Optional[Dict[str, int]]:
        """从共享头文件读取内部 Flash 安全布局，避免 Python 常量漂移。"""
        header = self.project_root / "common" / "internal_flash_security_layout.h"
        try:
            text = header.read_text(encoding="utf-8")
        except OSError as exc:
            print(f"错误: 无法读取内部 Flash 布局: {exc}")
            return None

        names = {
            "base": "HBOX_INTERNAL_FLASH_BASE_ADDRESS",
            "total": "HBOX_INTERNAL_FLASH_TOTAL_BYTES",
            "identity": "HBOX_DEVICE_IDENTITY_REGION_ADDRESS",
        }
        values: Dict[str, int] = {}
        for key, macro in names.items():
            match = re.search(
                rf"^[ \t]*#define[ \t]+{macro}[ \t]+(0x[0-9A-Fa-f]+|[0-9]+)u?",
                text,
                re.MULTILINE,
            )
            if match is None:
                print(f"错误: 内部 Flash 布局缺少 {macro}")
                return None
            values[key] = int(match.group(1), 0)

        end = values["base"] + values["total"]
        if not (values["base"] < values["identity"] < end):
            print("错误: 内部 Flash 安全布局不合法")
            return None
        values["protected_size"] = end - values["identity"]
        return values

    def _bootloader_openocd_prefix(self) -> list:
        return [
            self.config.get("openocd_path", "openocd"),
            "-d0",
            "-f", "Openocd_Script/ST-LINK-FLASH.cfg",
            "-c", "init",
            "-c", "halt",
            "-c", "reset halt",
        ]

    @staticmethod
    def _file_is_erased(path: Path, expected_size: int) -> bool:
        try:
            data = path.read_bytes()
        except OSError as exc:
            print(f"错误: 无法读取 Flash 检查文件 {path}: {exc}")
            return False
        if len(data) != expected_size:
            print(
                f"错误: Flash 检查文件大小错误: {len(data)}，"
                f"期望 {expected_size}"
            )
            return False
        return all(value == 0xFF for value in data)

    def flash_bootloader_development(self) -> bool:
        """仅为未置备开发板烧录 bootloader；生产身份存在时拒绝擦除。"""
        print("=" * 50)
        print("开发板 Bootloader 安全烧录")
        print("=" * 50)

        bootloader_elf = self.bootloader_dir / "build" / "bootloader.elf"
        if not bootloader_elf.is_file():
            print(f"错误: Bootloader ELF 文件不存在: {bootloader_elf}")
            print("请先执行: python tools/hbox.py build bootloader")
            return False

        layout = self._load_internal_flash_security_layout()
        if layout is None:
            return False

        build_dir = self.bootloader_dir / "build"
        backup_dir = self.project_root / ".hbox" / "device-backups"
        timestamp = datetime.now().strftime("%Y%m%d-%H%M%S-%f")
        protected_dump = build_dir / ".development-security-tail.bin"
        post_flash_dump = build_dir / ".development-security-tail-after.bin"
        sector_backup = backup_dir / f"internal-flash-{timestamp}.bin"

        for temporary in (protected_dump, post_flash_dump):
            try:
                temporary.unlink(missing_ok=True)
            except OSError as exc:
                print(f"错误: 无法清理临时文件 {temporary}: {exc}")
                return False

        try:
            print("1/4 检查设备 Identity 和 Security Version 区域...")
            read_protected = self._bootloader_openocd_prefix() + [
                "-c",
                (
                    f'dump_image "{protected_dump.as_posix()}" '
                    f'0x{layout["identity"]:08X} '
                    f'0x{layout["protected_size"]:X}'
                ),
                "-c", "shutdown",
            ]
            if not self.run_command(read_protected, self.bootloader_dir):
                print("错误: 无法读取安全状态区域；禁止执行擦除")
                print("如果设备处于 RDP1，请勿执行 read-unprotect")
                return False
            if not self._file_is_erased(
                protected_dump, layout["protected_size"]
            ):
                print("错误: 检测到设备 Identity 或防降级记录")
                print("该设备不是未置备开发板，已拒绝擦除内部 Flash")
                return False

            print("2/4 备份整个 128KiB 内部 Flash...")
            backup_dir.mkdir(parents=True, exist_ok=True)
            read_sector = self._bootloader_openocd_prefix() + [
                "-c",
                (
                    f'dump_image "{sector_backup.as_posix()}" '
                    f'0x{layout["base"]:08X} 0x{layout["total"]:X}'
                ),
                "-c", "shutdown",
            ]
            if not self.run_command(read_sector, self.bootloader_dir):
                print("错误: 内部 Flash 备份失败；禁止执行擦除")
                return False
            try:
                backup_size = sector_backup.stat().st_size
            except OSError:
                backup_size = -1
            if backup_size != layout["total"]:
                print(
                    f"错误: 内部 Flash 备份大小错误: {backup_size}，"
                    f"期望 {layout['total']}"
                )
                return False
            print(f"开发板备份已保存: {sector_backup}")

            print("3/4 擦除内部 sector、烧录并校验 Bootloader...")
            flash_command = self._bootloader_openocd_prefix() + [
                "-c", "flash info 0",
                "-c", "flash erase_sector 0 0 0",
                "-c", f'program "{bootloader_elf.as_posix()}" verify',
                "-c",
                (
                    f'dump_image "{post_flash_dump.as_posix()}" '
                    f'0x{layout["identity"]:08X} '
                    f'0x{layout["protected_size"]:X}'
                ),
                "-c", "reset run",
                "-c", "shutdown",
            ]
            if not self.run_command(flash_command, self.bootloader_dir):
                print("错误: Bootloader 开发烧录失败")
                return False

            print("4/4 校验保留安全区域仍为空...")
            if not self._file_is_erased(
                post_flash_dump, layout["protected_size"]
            ):
                print("错误: Bootloader ELF 越界写入了安全状态区域")
                return False

            print("Bootloader 开发烧录成功")
            print("注意: 此入口只适用于 Identity/Security Version 为空的开发板")
            return True
        finally:
            for temporary in (protected_dump, post_flash_dump):
                try:
                    temporary.unlink(missing_ok=True)
                except OSError:
                    pass

    
    def load_assets_cfg(self) -> Dict[str, Any]:
        cfg = {"max_width": 320, "max_height": 170, "dither": True}
        cfg_path = self.tools_dir / "assets_config.json"
        try:
            if cfg_path.exists():
                with open(cfg_path, "r", encoding="utf-8") as f:
                    data = json.load(f)
                    if isinstance(data, dict):
                        cfg.update(data)
        except Exception:
            pass
        return cfg
    def flash_web_resources(self, slot: str) -> bool:
        """烧录V1 Legacy Web Resources到指定槽。V2不调用此入口。"""
        print("=" * 50)
        print("=" * 50)
        print(f"烧录 V1 Legacy Web Resources 到槽 {slot}")
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

        print(f"Web Resources文件: {web_resources_file}")
        print(f"目标地址: {webres_address}")
        return self._flash_qspi_file_in_chunks(
            web_resources_file,
            int(webres_address, 16),
            f"V1 WebResources槽{slot}",
        )

    def build_system_assets(self) -> Optional[Path]:
        assets_dir = self.application_dir / "assets" / "sysicons"
        if not assets_dir.exists():
            print(f"未找到 assets 目录: {assets_dir}")
            return None
        out_file = self.application_dir / "build" / "system_assets.bin"
        packer = self.tools_dir / "pack_assets.py"
        if not packer.exists():
            print(f"错误: assets 打包脚本不存在: {packer}")
            return None
        assets_fix = self.tools_dir / "assets_fix.py"
        if assets_fix.exists():
            acfg = self.load_assets_cfg()
            fit_arg = f"{int(acfg.get('max_width',320))}x{int(acfg.get('max_height',170))}"
            args = [
                sys.executable,
                str(assets_fix),
                "--dir", str(assets_dir),
                "--out", str(assets_dir),
                "--fit", fit_arg,
                "--inplace",
            ]
            if acfg.get("dither", True):
                args.append("--dither")
            self.run_command(args, self.tools_dir)

        max_size = self.shared_addresses.get("sys_assets_size", "0x00040000")
        cmd = [
            sys.executable,
            str(packer),
            "--icons-dir", str(assets_dir),
            "--icons-output", str(out_file),
            "--icons-max-size", str(max_size),
        ]

        ok = self.run_command(cmd, self.tools_dir)
        if not ok:
            return None

        if not out_file.exists():
            print(f"错误: assets 打包输出不存在: {out_file}")
            return None

        return out_file

    def build_sysbg(self) -> Optional[Path]:
        sysbg_dir = self.application_dir / "assets" / "sysbg"
        if not sysbg_dir.exists():
            print(f"未找到 sysbg 目录: {sysbg_dir}")
            return None
        out_file = self.application_dir / "build" / "sysbg.bin"
        packer = self.tools_dir / "pack_assets.py"
        if not packer.exists():
            print(f"错误: assets 打包脚本不存在: {packer}")
            return None

        max_size = self.shared_addresses.get("user_image_size", "0x00210000")
        cmd = [
            sys.executable,
            str(packer),
            "--sysbg-dir", str(sysbg_dir),
            "--sysbg-output", str(out_file),
            "--sysbg-max-size", str(max_size),
        ]
        ok = self.run_command(cmd, self.tools_dir)
        if not ok:
            return None
        if not out_file.exists():
            print(f"错误: sysbg 打包输出不存在: {out_file}")
            return None
        return out_file

    def flash_sysbg(self) -> bool:
        print("=" * 50)
        print("烧录 系统背景图片 (sysbg)")
        print("=" * 50)

        out_file = self.build_sysbg()
        if not out_file:
            return False

        target_address = self.shared_addresses.get("user_image_addr", "0x905F0000")
        max_size = int(self.shared_addresses.get("user_image_size", "0x00210000"), 16)
        file_size = out_file.stat().st_size
        if file_size > max_size:
            print(f"错误: sysbg.bin 超过用户图片区大小: {file_size} > {max_size}")
            return False

        return self._flash_qspi_file_in_chunks(
            out_file,
            int(target_address, 16),
            "系统背景图片",
        )

    def flash_system_assets(self, allow_missing: bool = False) -> bool:
        print("=" * 50)
        print("烧录 系统图片资源 (assets)")
        print("=" * 50)

        assets_dir = self.application_dir / "assets" / "sysicons"
        if not assets_dir.exists():
            if allow_missing:
                print(f"未找到 sysicons 目录: {assets_dir}")
                print("跳过系统图片资源烧录")
                return True
            print(f"未找到 sysicons 目录: {assets_dir}")
            return False

        out_file = self.build_system_assets()
        if not out_file:
            return False

        target_address = self.shared_addresses.get("sys_assets_addr", "0x905B0000")
        max_size = int(self.shared_addresses.get("sys_assets_size", "0x00040000"), 16)
        file_size = out_file.stat().st_size
        if file_size > max_size:
            print(f"错误: system_assets.bin 超过系统图片区大小: {file_size} > {max_size}")
            return False

        return self._flash_qspi_file_in_chunks(
            out_file,
            int(target_address, 16),
            "系统图片资源",
        )
        
    def _flash_using_makefile(self, slot: str) -> bool:
        """使用Makefile的flash目标进行烧录"""
        temp_files = []
        try:
            makefile_path = self.application_dir / "Makefile"
            if not makefile_path.exists():
                return False
                
            # 对于特定槽，需要临时复制文件
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
                capture_output=False,
                timeout=120
            )
            
            if result.returncode == 0:
                print("Makefile烧录成功")
                return True
            else:
                print("Makefile烧录失败")
                return False
                
        except Exception as e:
            print(f"Makefile烧录异常: {e}")
            return False
        finally:
            # 无论成功失败都恢复备份，避免污染后续烧录
            for backup_path in temp_files:
                if backup_path and backup_path.exists():
                    original_path = backup_path.with_suffix('')
                    shutil.move(backup_path, original_path)
    
    def _flash_using_openocd(self, slot: str) -> bool:
        """兼容旧调用点；实际使用稳定的QSPI分块烧录。"""
        return self.flash_application(slot)

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
        print(f"  V1 Legacy Web Resources: {'✅' if web_resources_file.exists() else '❌'} {web_resources_file}")
        
        # 显示槽地址配置
        print("\n槽地址配置:")
        for slot, config in self.slot_config.items():
            print(f"  槽{slot}:")
            print(f"    Application: {config['address']}")
            print(f"    WebResources: {config['webres_address']}")
            print(f"    ADC Mapping: {config['adc_address']}")

    def build_and_flash_complete_slot(self, slot: str) -> bool:
        """构建并烧录V2设备内容（Hosted WebConfig不属于固件）。"""
        print("=" * 60)
        print(f"构建并烧录完整槽 {slot}")
        print("=" * 60)
        
        # 1. 构建Application
        print("1/4 构建Application...")
        build_success = self.build_application(slot)
        if not build_success:
            print("❌ Application构建失败，停止操作")
            return False
            
        # 2. 烧录Application  
        print("\n2/4 烧录Application...")
        app_flash_success = self.flash_application(slot)
        if not app_flash_success:
            print("❌ Application烧录失败，停止操作")
            return False
            
        print("\n3/4 烧录系统图片资源...")
        assets_flash_success = self.flash_system_assets(allow_missing=True)
        if not assets_flash_success:
            print("❌ 系统图片资源烧录失败")
            return False

        print("\n4/4 烧录系统背景图片(sysbg)...")
        sysbg_flash_success = self.flash_sysbg()
        if not sysbg_flash_success:
            print("❌ 系统背景图片(sysbg)烧录失败")
            return False
            
        print(f"\n✅ 完整槽 {slot} 构建并烧录成功！")
        print("槽内容:")
        slot_cfg = self.slot_config[slot]
        print(f"  - Application: {slot_cfg['address']}")
        print(f"  - ADC Mapping: {slot_cfg['adc_address']}")
        print(f"  - SysAssets: {self.shared_addresses.get('sys_assets_addr', '0x905B0000')}")
        print(f"  - SysBg: {self.shared_addresses.get('user_image_addr', '0x905F0000')}")
        print("  - WebConfig: 由HTTPS服务器独立部署，不写入QSPI WebResources")
        
        return True

    def flash_v2_slot_contents(self, slot: str) -> bool:
        """烧录V2设备内容；Hosted WebConfig永远不在这个事务中。"""
        print("=" * 50)
        print(f"烧录V2设备内容到槽 {slot}")
        print("=" * 50)

        print("1/3 烧录Application...")
        if not self.flash_application(slot):
            print("❌ Application烧录失败，停止操作")
            return False

        print("\n2/3 烧录系统图片资源...")
        if not self.flash_system_assets(allow_missing=True):
            print("❌ 系统图片资源烧录失败")
            return False

        print("\n3/3 烧录系统背景图片(sysbg)...")
        if not self.flash_sysbg():
            print("❌ 系统背景图片(sysbg)烧录失败")
            return False

        print(f"\n✅ V2设备内容烧录成功到槽 {slot}")
        print("WebConfig未写入设备；V2页面由HTTPS服务器独立部署")
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
  %(prog)s flash bootloader              # 生产设备门禁：始终拒绝
  %(prog)s flash bootloader-dev          # 仅烧录未置备开发板
  %(prog)s flash app A                   # 烧录application槽A
  %(prog)s flash app B                   # 烧录application槽B
  %(prog)s flash web A                   # 仅V1旧板：烧录内置Web Resources到槽A
  %(prog)s flash web B                   # 仅V1旧板：烧录内置Web Resources到槽B
  %(prog)s flash assets                  # 烧录系统图片资源(assets)到共享区
  %(prog)s flash sysbg                   # 烧录系统背景图片(sysbg)到用户图片区
  %(prog)s flash all A                   # 烧录V2设备内容(不含Hosted WebConfig)到槽A
  %(prog)s flash all B                   # 烧录V2设备内容(不含Hosted WebConfig)到槽B
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
    flash_parser.add_argument("target", choices=["bootloader", "bootloader-dev", "app", "web", "assets", "sysbg", "all"], help="烧录目标")
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
            elif args.target == "bootloader-dev":
                success = tool.flash_bootloader_development()
            elif args.target == "app":
                if not args.slot:
                    print("错误: 烧录application时必须指定槽 (A 或 B)")
                    return 1
                success = tool.flash_application(args.slot)
            elif args.target == "web":
                if not args.slot:
                    print("错误: 烧录Web Resources时必须指定槽 (A 或 B)")
                    return 1
                print("警告: flash web 仅用于V1旧板；V2页面必须部署到HTTPS服务器")
                success = tool.flash_web_resources(args.slot)
            elif args.target == "assets":
                success = tool.flash_system_assets()
            elif args.target == "sysbg":
                success = tool.flash_sysbg()
            elif args.target == "all":
                if not args.slot:
                    print("错误: 烧录完整固件时必须指定槽 (A 或 B)")
                    return 1
                success = tool.flash_v2_slot_contents(args.slot)
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
