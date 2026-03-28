#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
HBox 工具统一入口（tools/hbox.py）

用法:
  python tools/hbox.py <command> <target> [args...]

可用命令总览:

1) build
  - bootloader
  - web
  - assets
  - ADCMapping
  - app A|B
  - appAll A|B

2) flash
  - bootloader
  - app A|B
  - appAll A|B
  - assets

3) release
  - auto
  - flash

示例:
  python tools/hbox.py build app A
  python tools/hbox.py build web
  python tools/hbox.py build assets
  python tools/hbox.py build appAll A
  python tools/hbox.py flash appAll A
  python tools/hbox.py release auto --version 1.0.0
  python tools/hbox.py release flash 0.0.1_a --slot A
  python tools/hbox.py web dev
  python tools/hbox.py web build
"""

import argparse
import json
import subprocess
import sys
from pathlib import Path


def _project_root() -> Path:
    return Path(__file__).resolve().parent.parent


def _tools_dir() -> Path:
    return Path(__file__).resolve().parent


def _run_python_tool(script_name: str, tool_args: list[str]) -> int:
    script_path = _tools_dir() / script_name
    if not script_path.exists():
        print(f"错误: 未找到工具脚本: {script_path}")
        return 2

    cmd = [sys.executable, str(script_path), *tool_args]
    return subprocess.call(cmd, cwd=_project_root())


def _run_node_makefsdata() -> int:
    www_dir = _project_root() / "application" / "www"
    script = www_dir / "makefsdata.js"
    if not script.exists():
        print(f"错误: 未找到 WebResources 生成脚本: {script}")
        return 2
    return subprocess.call(["node", str(script)], cwd=www_dir)


def _run_pack_assets() -> int:
    out_file = _project_root() / "application" / "build" / "system_assets.bin"
    in_dir = _project_root() / "application" / "assets" / "icons"
    assets_fix = _tools_dir() / "assets_fix.py"
    if assets_fix.exists():
        fit_arg = "320x170"
        dither_flag = True
        cfg_path = _tools_dir() / "assets_config.json"
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
        subprocess.call(
            [
                sys.executable,
                str(assets_fix),
                "--dir", str(in_dir),
                "--out", str(in_dir),
                "--fit", fit_arg,
                "--inplace",
            ] + (["--dither"] if dither_flag else []),
            cwd=_project_root(),
        )
    return _run_python_tool(
        "pack_assets.py",
        ["--icons-dir", str(in_dir), "--icons-output", str(out_file)],
    )


def _run_pack_sysbg() -> int:
    out_file = _project_root() / "application" / "build" / "sysbg.bin"
    in_dir = _project_root() / "application" / "assets" / "sysbg"
    return _run_python_tool(
        "pack_assets.py",
        ["--sysbg-dir", str(in_dir), "--sysbg-output", str(out_file)],
    )


def _run_extract_adc_mapping() -> int:
    script_path = _tools_dir() / "extract_adc_mapping.py"
    if not script_path.exists():
        print(f"错误: 未找到工具脚本: {script_path}")
        return 2
    cmd = [sys.executable, str(script_path)]
    return subprocess.call(cmd, cwd=_tools_dir())


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(
        prog="hbox",
        description="HBox 工具统一入口（精简版）",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例:
  python tools/hbox.py build bootloader
  python tools/hbox.py build app A
  python tools/hbox.py build web
  python tools/hbox.py build assets
  python tools/hbox.py build ADCMapping
  python tools/hbox.py build appAll A
  python tools/hbox.py flash appAll A
  python tools/hbox.py release auto --version 1.0.0
  python tools/hbox.py web dev
  python tools/hbox.py web build
""",
    )

    subparsers = parser.add_subparsers(dest="cmd", required=True)

    p_build = subparsers.add_parser("build", help="构建相关")
    p_build.add_argument("target", choices=["bootloader", "web", "assets", "sysbg", "ADCMapping", "app", "appAll"])
    p_build.add_argument("slot", nargs="?", choices=["A", "B"])

    p_flash = subparsers.add_parser("flash", help="烧录相关")
    p_flash.add_argument("target", choices=["bootloader", "app", "appAll", "assets", "sysbg"])
    p_flash.add_argument("slot", nargs="?", choices=["A", "B"])

    p_release = subparsers.add_parser("release", help="发版相关")
    p_release.add_argument("target", choices=["auto", "flash"])
    p_release.add_argument("args", nargs=argparse.REMAINDER)

    p_web = subparsers.add_parser("web", help="Web前端")
    p_web.add_argument("target", choices=["dev", "build"])

    args = parser.parse_args(argv)

    if args.cmd == "build":
        if args.target == "bootloader":
            return _run_python_tool("build.py", ["build", "bootloader"])
        if args.target == "web":
            return _run_node_makefsdata()
        if args.target == "assets":
            return _run_pack_assets()
        if args.target == "sysbg":
            return _run_pack_sysbg()
        if args.target == "ADCMapping":
            return _run_extract_adc_mapping()
        if args.target == "app":
            if not args.slot:
                print("错误: build app 需要指定槽位 A 或 B")
                return 2
            return _run_python_tool("build.py", ["build", "app", args.slot])
        if args.target == "appAll":
            if not args.slot:
                print("错误: build appAll 需要指定槽位 A 或 B")
                return 2
            rc = _run_python_tool("build.py", ["build", "app", args.slot])
            if rc != 0:
                return rc
            rc = _run_node_makefsdata()
            if rc != 0:
                return rc
            return _run_pack_assets()

    if args.cmd == "flash":
        if args.target == "bootloader":
            return _run_python_tool("build.py", ["flash", "bootloader"])
        if args.target == "assets":
            return _run_python_tool("build.py", ["flash", "assets"])
        if args.target == "sysbg":
            return _run_python_tool("build.py", ["flash", "sysbg"])
        if args.target == "app":
            if not args.slot:
                print("错误: flash app 需要指定槽位 A 或 B")
                return 2
            return _run_python_tool("build.py", ["flash", "app", args.slot])
        if args.target == "appAll":
            if not args.slot:
                print("错误: flash appAll 需要指定槽位 A 或 B")
                return 2
            return _run_python_tool("build.py", ["flash", "all", args.slot])

    if args.cmd == "release":
        if args.target == "auto":
            return _run_python_tool("release.py", ["auto", *args.args])
        if args.target == "flash":
            return _run_python_tool("release.py", ["flash", *args.args])

    if args.cmd == "web":
        if args.target == "dev":
            www_dir = _project_root() / "application" / "www"
            next_bin = www_dir / "node_modules" / "next" / "dist" / "bin" / "next"
            if not next_bin.exists():
                print(f"错误: 未找到 Next.js 可执行脚本: {next_bin}")
                print("请先安装依赖: npm --prefix application/www install")
                return 2
            try:
                ws = subprocess.Popen(
                    ["node", "websocket-server.js"],
                    cwd=www_dir,
                )
                dev = subprocess.Popen(
                    ["node", str(next_bin), "dev"],
                    cwd=www_dir,
                )
                dev.wait()
                try:
                    ws.terminate()
                except Exception:
                    pass
                return dev.returncode
            except FileNotFoundError as e:
                print(f"错误: 启动开发服务失败: {e}")
                print("请确保系统已安装 Node.js，并且 'node' 命令可用")
                return 2
        if args.target == "build":
            www_dir = _project_root() / "application" / "www"
            makefs = www_dir / "makefsdata.js"
            next_bin = www_dir / "node_modules" / "next" / "dist" / "bin" / "next"
            try:
                rc = subprocess.call(
                    ["npm", "--prefix", str(www_dir), "run", "build"],
                    cwd=_project_root(),
                )
            except FileNotFoundError:
                try:
                    rc = subprocess.call(
                        ["npm.cmd", "--prefix", str(www_dir), "run", "build"],
                        cwd=_project_root(),
                    )
                except FileNotFoundError:
                    if not next_bin.exists():
                        print("错误: 未找到 npm 或 Next.js 可执行文件")
                        print("请先安装依赖: npm --prefix application/www install")
                        return 2
                    rc = subprocess.call(["node", str(next_bin), "build"], cwd=www_dir)
            if rc != 0:
                print("错误: Web前端构建失败")
                return rc
            if not makefs.exists():
                print(f"错误: 未找到 makefsdata.js: {makefs}")
                return 2
            rc2 = subprocess.call(["node", str(makefs)], cwd=www_dir)
            if rc2 != 0:
                print("错误: 生成 WebResources 二进制失败")
                return rc2
            print("Web 前端构建并生成二进制完成")
            return 0

    return 2


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
