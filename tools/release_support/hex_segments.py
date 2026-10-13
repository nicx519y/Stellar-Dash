"""Split Intel HEX images into release components without accessing hardware."""

import hashlib
import json
import time
from datetime import datetime
from pathlib import Path
from typing import Dict, List

from intelhex import IntelHex

class HexSegmenter:
    """Intel HEX文件解析和分割器"""

    def __init__(self):
        self.segments = {}
        self.components = []

    def parse_hex_file(self, hex_path: Path) -> List[Dict]:
        """解析Intel HEX文件，按连续段分割并生成组件"""
        print(f"解析Intel HEX文件: {hex_path}")

        try:
            # 使用intelhex库加载HEX文件
            ih = IntelHex(str(hex_path))

            # 获取所有使用的地址段
            segments = ih.segments()
            print(f"发现 {len(segments)} 个地址段:")

            components = []
            for i, (start_addr, end_addr) in enumerate(segments):
                # 计算段大小（end_addr是exclusive的）
                segment_size = end_addr - start_addr

                # 提取连续段的数据
                segment_data = ih.tobinstr(start=start_addr, end=end_addr-1)

                # 确定段类型和组件名
                component_info = self.classify_segment(start_addr, end_addr, segment_size)

                print(f"  段 {i+1}: 0x{start_addr:08X} - 0x{end_addr:08X} ({segment_size:,} 字节) -> {component_info['name']}")

                # 创建组件信息
                component = {
                    'name': component_info['name'],
                    'start_address': start_addr,
                    'end_address': end_addr,
                    'size': len(segment_data),
                    'data': segment_data,
                    'target_address': component_info['target_address'],
                    'description': component_info['description']
                }

                components.append(component)

            print(f"成功解析Intel HEX文件，生成 {len(components)} 个组件")
            return components

        except Exception as e:
            print(f"解析Intel HEX文件失败: {e}")
            raise

    def classify_segment(self, start_addr: int, end_addr: int, size: int) -> Dict:
        """根据地址范围分类段，确定组件类型和目标地址"""

        # STM32H750的内存映射
        if 0x08000000 <= start_addr < 0x08200000:
            # 内部Flash区域 (STM32H750有128KB内部Flash)
            return {
                'name': 'internal_flash',
                'target_address': start_addr,
                'description': f'内部Flash (0x{start_addr:08X}-0x{end_addr:08X})',
                'memory_type': 'flash'
            }
        elif 0x20000000 <= start_addr < 0x20020000:
            # DTCM RAM区域 (128KB)
            return {
                'name': 'dtcm_ram',
                'target_address': start_addr,
                'description': f'DTCM RAM (0x{start_addr:08X}-0x{end_addr:08X})',
                'memory_type': 'ram',
                'skip_flash': True  # RAM段不需要烧写
            }
        elif 0x24000000 <= start_addr < 0x24080000:
            # AXI SRAM区域 (512KB)
            return {
                'name': 'axi_sram',
                'target_address': start_addr,
                'description': f'AXI SRAM (0x{start_addr:08X}-0x{end_addr:08X})',
                'memory_type': 'ram',
                'skip_flash': True  # RAM段不需要烧写
            }
        elif 0x30000000 <= start_addr < 0x30048000:
            # SRAM1 in Domain D2 (288KB)
            return {
                'name': 'sram1_d2',
                'target_address': start_addr,
                'description': f'SRAM1 D2域 (0x{start_addr:08X}-0x{end_addr:08X})',
                'memory_type': 'ram',
                'skip_flash': True  # RAM段不需要烧写
            }
        elif 0x38000000 <= start_addr < 0x38010000:
            # SRAM4 in Domain D3 (64KB)
            return {
                'name': 'sram4_d3',
                'target_address': start_addr,
                'description': f'SRAM4 D3域 (0x{start_addr:08X}-0x{end_addr:08X})',
                'memory_type': 'ram',
                'skip_flash': True  # RAM段不需要烧写
            }
        elif 0x38800000 <= start_addr < 0x38801000:
            # Backup SRAM (4KB)
            return {
                'name': 'backup_sram',
                'target_address': start_addr,
                'description': f'备份SRAM (0x{start_addr:08X}-0x{end_addr:08X})',
                'memory_type': 'ram',
                'skip_flash': True  # RAM段不需要烧写
            }
        elif 0x90000000 <= start_addr < 0x94000000:
            # 外部QSPI Flash区域 - 根据具体地址细分
            if 0x90000000 <= start_addr < 0x90100000:
                return {
                    'name': 'application',
                    'target_address': 0x90000000,  # 应用程序基地址
                    'description': f'应用程序 (0x{start_addr:08X}-0x{end_addr:08X})',
                    'memory_type': 'qspi_flash'
                }
            elif 0x90100000 <= start_addr < 0x90280000:
                return {
                    'name': 'webresources',
                    'target_address': 0x90100000,  # Web资源基地址
                    'description': f'Web资源 (0x{start_addr:08X}-0x{end_addr:08X})',
                    'memory_type': 'qspi_flash'
                }
            elif 0x90280000 <= start_addr < 0x902B0000:
                return {
                    'name': 'adc_mapping',
                    'target_address': 0x90280000,  # ADC映射基地址
                    'description': f'ADC映射 (0x{start_addr:08X}-0x{end_addr:08X})',
                    'memory_type': 'qspi_flash'
                }
            elif 0x902B0000 <= start_addr < 0x903B0000:
                return {
                    'name': 'application_slot_b',
                    'target_address': 0x902B0000,  # 槽B应用程序基地址
                    'description': f'槽B应用程序 (0x{start_addr:08X}-0x{end_addr:08X})',
                    'memory_type': 'qspi_flash'
                }
            elif 0x903B0000 <= start_addr < 0x90530000:
                return {
                    'name': 'webresources_slot_b',
                    'target_address': 0x903B0000,  # 槽B Web资源基地址
                    'description': f'槽B Web资源 (0x{start_addr:08X}-0x{end_addr:08X})',
                    'memory_type': 'qspi_flash'
                }
            elif 0x90530000 <= start_addr < 0x90560000:
                return {
                    'name': 'adc_mapping_slot_b',
                    'target_address': 0x90530000,  # 槽B ADC映射基地址
                    'description': f'槽B ADC映射 (0x{start_addr:08X}-0x{end_addr:08X})',
                    'memory_type': 'qspi_flash'
                }
            else:
                return {
                    'name': f'external_flash_{start_addr:08x}',
                    'target_address': start_addr,
                    'description': f'外部Flash段 (0x{start_addr:08X}-0x{end_addr:08X})',
                    'memory_type': 'qspi_flash'
                }
        else:
            # 其他地址区域
            return {
                'name': f'unknown_{start_addr:08x}',
                'target_address': start_addr,
                'description': f'未知段 (0x{start_addr:08X}-0x{end_addr:08X})',
                'memory_type': 'unknown'
            }

    def save_components(self, components: List[Dict], output_dir: Path, version: str) -> Dict:
        """将组件保存为独立的bin文件并生成manifest"""
        print(f"保存组件到目录: {output_dir}")

        output_dir.mkdir(exist_ok=True)

        manifest = {
            'version': version,
            'build_date': datetime.now().strftime('%Y-%m-%d %H:%M:%S'),
            'build_timestamp': int(time.time()),
            'components': [],
            'ram_segments': []  # 单独记录RAM段信息
        }

        flash_component_count = 0
        ram_component_count = 0

        for i, component in enumerate(components):
            # 检查组件信息
            component_info = self.classify_segment(
                component['start_address'],
                component['end_address'],
                component['size']
            )

            # 更新组件信息
            component.update(component_info)

            # 判断是否需要跳过Flash烧写
            if component_info.get('skip_flash', False) or component_info.get('memory_type') == 'ram':
                # RAM段：只记录信息，不生成bin文件
                print(f"  跳过RAM段 {i+1}: {component['name']} ({component['size']:,} 字节)")
                print(f"    地址: 0x{component['start_address']:08X} [{component_info['memory_type'].upper()}]")
                print(f"    说明: {component_info['description']}")

                # 添加到RAM段记录
                manifest['ram_segments'].append({
                    'name': component['name'],
                    'address': f"0x{component['start_address']:08X}",
                    'size': component['size'],
                    'memory_type': component_info['memory_type'],
                    'description': component_info['description'],
                    'note': 'VMA地址，运行时使用，不需要Flash烧写'
                })

                ram_component_count += 1
                continue

            # Flash段：生成bin文件
            bin_filename = f"{component['name']}.bin"
            bin_path = output_dir / bin_filename

            # 保存二进制数据
            with open(bin_path, 'wb') as f:
                f.write(component['data'])

            # 计算SHA256校验和
            sha256_hash = hashlib.sha256(component['data']).hexdigest()

            print(f"  保存组件 {flash_component_count + 1}: {component['name']} -> {bin_filename} ({component['size']:,} 字节)")
            print(f"    地址: 0x{component['start_address']:08X} -> 0x{component['target_address']:08X} [{component_info['memory_type'].upper()}]")
            print(f"    SHA256: {sha256_hash[:16]}...")

            # 添加到manifest
            manifest['components'].append({
                'name': component['name'],
                'file': bin_filename,
                'address': f"0x{component['target_address']:08X}",
                'size': component['size'],
                'sha256': sha256_hash,
                'active': True,
                'memory_type': component_info['memory_type'],
                'description': component_info['description'],
                'original_address': f"0x{component['start_address']:08X}"
            })

            flash_component_count += 1

        # 保存manifest文件
        manifest_path = output_dir / 'hex_manifest.json'
        with open(manifest_path, 'w', encoding='utf-8') as f:
            json.dump(manifest, f, indent=2, ensure_ascii=False)

        print(f"生成manifest文件: {manifest_path}")
        print(f"Flash组件: {flash_component_count} 个")
        print(f"RAM段(跳过): {ram_component_count} 个")
        print(f"总段数: {len(components)} 个")

        return manifest

    def process_hex_file(self, hex_path: Path, output_dir: Path, version: str) -> Dict:
        """完整处理Intel HEX文件：解析、分割、保存"""
        print(f"\n=== 处理Intel HEX文件 ===")
        print(f"输入文件: {hex_path}")
        print(f"输出目录: {output_dir}")
        print(f"版本: {version}")

        # 解析HEX文件
        components = self.parse_hex_file(hex_path)

        # 保存组件
        manifest = self.save_components(components, output_dir, version)

        print(f"=== 处理完成 ===\n")
        return manifest
