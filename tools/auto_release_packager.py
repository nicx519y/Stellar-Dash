#!/usr/bin/env python3
"""
自动生成双槽release固件包工具
- 自动构建slot A/B application
- 自动提取adc mapping
- 复制webresources
- 生成manifest元数据
- 打包为zip
"""
import os
import sys
import subprocess
import shutil
import hashlib
import json
from pathlib import Path
from datetime import datetime
import zipfile
import time

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

def run_cmd(cmd, cwd=None, check=True):
    print(f"[CMD] {' '.join(map(str, cmd))}")
    result = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, encoding='utf-8', errors='ignore')
    if result.returncode != 0 and check:
        print("STDOUT:")
        print(result.stdout)
        print("STDERR:")
        print(result.stderr)
        raise RuntimeError(f"命令执行失败: {' '.join(cmd)}")
    return result

def sha256sum(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        while True:
            chunk = f.read(8192)
            if not chunk:
                break
            h.update(chunk)
    return h.hexdigest()

def build_app(slot, tools_dir, app_dir, progress=None):
    # 调用 build.py 构建 application
    if progress:
        progress.update(f"构建槽{slot} Application...")
    else:
        print(f"正在构建槽{slot}的Application...")
    
    try:
        result = run_cmd([sys.executable, str(tools_dir/'build.py'), 'build', 'app', slot], cwd=tools_dir, check=False)
        # 检查是否生成了目标文件，而不是依赖返回码
        hex_file = app_dir / 'build' / f'application_slot_{slot}.hex'
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

def extract_adc_mapping(tools_dir, resources_dir, progress=None):
    # 调用 extract_adc_mapping.py 生成 adc mapping
    if progress:
        progress.update("提取ADC Mapping数据...")
    else:
        print("正在提取ADC Mapping数据...")
    
    try:
        result = run_cmd([sys.executable, str(tools_dir/'extract_adc_mapping.py')], cwd=tools_dir, check=False)
        adc_file = resources_dir / 'slot_a_adc_mapping.bin'
        if not adc_file.exists():
            print("\nADC Mapping提取可能失败")
            print("STDOUT:")
            print(result.stdout)
            print("STDERR:")
            print(result.stderr)
            raise FileNotFoundError(f"未找到ADC Mapping文件: {adc_file}")
        
        if not progress:
            print(f"成功提取ADC Mapping: {adc_file}")
        return adc_file
    except Exception as e:
        print(f"\n提取ADC Mapping失败: {e}")
        raise

def copy_webresources(app_dir, out_dir, progress=None):
    if progress:
        progress.update("复制WebResources...")
    
    src = app_dir / 'Libs' / 'httpd' / 'ex_fsdata.bin'
    dst = out_dir / 'webresources.bin'
    if not src.exists():
        raise FileNotFoundError(f"未找到webresources: {src}")
    shutil.copy2(src, dst)
    
    if not progress:
        print(f"成功复制WebResources: {src} -> {dst}")
    return dst

def make_manifest(slot, hex_file, adc_file, web_file, version):
    # 地址参考README
    slot_addr = {
        'A': {
            'application': 0x90000000,
            'webresources': 0x90100000,
            'adc_mapping': 0x90280000
        },
        'B': {
            'application': 0x902B0000,
            'webresources': 0x903B0000,
            'adc_mapping': 0x90530000
        }
    }[slot]
    manifest = {
        'version': version,
        'slot': slot,
        'build_date': datetime.now().strftime('%Y-%m-%d %H:%M:%S'),
        'components': [
            {
                'name': 'application',
                'file': hex_file.name,
                'address': f"0x{slot_addr['application']:08X}",
                'size': hex_file.stat().st_size,
                'sha256': sha256sum(hex_file)
            },
            {
                'name': 'webresources',
                'file': web_file.name,
                'address': f"0x{slot_addr['webresources']:08X}",
                'size': web_file.stat().st_size,
                'sha256': sha256sum(web_file)
            },
            {
                'name': 'adc_mapping',
                'file': adc_file.name,
                'address': f"0x{slot_addr['adc_mapping']:08X}",
                'size': adc_file.stat().st_size,
                'sha256': sha256sum(adc_file)
            }
        ]
    }
    return manifest

def make_release_pkg(slot, version, tools_dir, app_dir, resources_dir, out_dir, releases_dir, build_timestamp):
    print(f"\n=== 开始生成槽{slot} release包 ===")
    
    # 计算总步骤数
    total_steps = 6  # 构建app + 提取adc + 复制web + 生成manifest + 打包 + 移动文件
    if slot == 'B':
        total_steps = 5  # 槽B不需要重新提取ADC
    
    progress = ProgressBar(total_steps)
    
    # 1. 构建app
    hex_file = build_app(slot, tools_dir, app_dir, progress)
    
    # 2. 提取adc mapping (只需要提取一次)
    if slot == 'A':  # 只在处理槽A时提取，避免重复
        adc_file = extract_adc_mapping(tools_dir, resources_dir, progress)
    else:
        adc_file = resources_dir / 'slot_a_adc_mapping.bin'
        if not adc_file.exists():
            print("\n警告: ADC Mapping文件不存在，尝试重新提取...")
            adc_file = extract_adc_mapping(tools_dir, resources_dir, progress)
    
    # 3. 复制webresources
    web_file = copy_webresources(app_dir, out_dir, progress)
    
    # 4. 生成manifest
    progress.update("生成manifest...")
    manifest = make_manifest(slot, hex_file, adc_file, web_file, version)
    manifest_path = out_dir / 'manifest.json'
    with open(manifest_path, 'w', encoding='utf-8') as f:
        json.dump(manifest, f, indent=2, ensure_ascii=False)
    
    # 5. 打包 - 使用传入的构建时间戳
    progress.update("打包文件...")
    zip_name = f"hbox_firmware_slot_{slot.lower()}_v{version.replace('.', '_')}_{build_timestamp}.zip"
    temp_zip_path = out_dir / zip_name
    
    with zipfile.ZipFile(temp_zip_path, 'w', zipfile.ZIP_DEFLATED) as zf:
        zf.write(hex_file, hex_file.name)
        zf.write(web_file, web_file.name)
        zf.write(adc_file, adc_file.name)
        zf.write(manifest_path, manifest_path.name)
    
    # 6. 移动到releases目录
    progress.update("移动到releases目录...")
    final_zip_path = releases_dir / zip_name
    shutil.move(temp_zip_path, final_zip_path)
    
    print(f"\n[OK] 生成release包: {final_zip_path}")
    print(f"     包大小: {final_zip_path.stat().st_size:,} 字节")
    return final_zip_path

def main():
    # 设置控制台编码为UTF-8（Windows兼容性）
    if sys.platform == 'win32':
        try:
            import codecs
            sys.stdout = codecs.getwriter('utf-8')(sys.stdout.buffer, 'strict')
            sys.stderr = codecs.getwriter('utf-8')(sys.stderr.buffer, 'strict')
        except:
            pass  # 如果设置失败，继续使用默认编码
    
    root = Path(__file__).parent.parent
    tools_dir = root / 'tools'
    app_dir = root / 'application'
    resources_dir = root / 'resources'
    out_dir = tools_dir / 'release_temp'
    releases_dir = root / 'releases'  # 新增releases目录
    
    # 确保目录存在
    out_dir.mkdir(exist_ok=True)
    releases_dir.mkdir(exist_ok=True)
    
    print("=== STM32 HBox 双槽Release包生成工具 ===")
    print(f"工作目录: {root}")
    print(f"输出目录: {releases_dir}")
    
    version = None
    if len(sys.argv) > 1:
        version = sys.argv[1]
    else:
        version = input('请输入版本号（如1.0.0）: ').strip()
    
    if not version:
        print("错误: 版本号不能为空")
        sys.exit(1)
    
    print(f"版本号: {version}")
    
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
                package_path = make_release_pkg(slot, version, tools_dir, app_dir, resources_dir, out_dir, releases_dir, build_timestamp)
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
            print(f"\n所有包已保存到: {releases_dir}")
        else:
            print("所有包生成失败，请检查错误信息")
            sys.exit(1)
    
    finally:
        # 清理临时目录
        if out_dir.exists():
            print(f"\n清理临时目录: {out_dir}")
            try:
                shutil.rmtree(out_dir)
                print("临时目录清理完成")
            except Exception as e:
                print(f"清理临时目录失败: {e}")
                # 清理失败不影响主程序退出

if __name__ == '__main__':
    main() 