#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
HBox 工具统一入口（tools/hbox.py）

用法:
  python tools/hbox.py <command> <target> [args...]

可用命令总览:

1) build
  - bootloader
  - web（V2服务器托管页面）
  - assets
  - ADCMapping
  - app A|B
  - appAll A|B

2) flash
  - bootloader（生产安全门禁，拒绝单独擦除）
  - bootloader-dev（仅限未置备开发板）
  - app A|B（默认只烧录现有安全完整槽；--build 先构建签名）
  - code A|B（低层纯代码烧录，不更新metadata）
  - appAll A|B
  - assets
  - tx（通过 ST-LINK → QSPI → SPI 烧录 CH585 TX 固件；--build 可先构建）

3) release
  - auto
  - flash

示例:
  python tools/hbox.py build app A
  python tools/hbox.py build web
  python tools/hbox.py build assets
  python tools/hbox.py build appAll A
  python tools/hbox.py flash app A
  python tools/hbox.py flash app A --build
  python tools/hbox.py flash appAll A
  python tools/hbox.py flash bootloader-dev
  python tools/hbox.py flash code A
  python tools/hbox.py flash appAll A --code-only
  python tools/hbox.py flash tx
  python tools/hbox.py flash tx --build
  python tools/hbox.py release auto --version 1.0.0
  python tools/hbox.py release flash 0.0.1_a --slot A
  python tools/hbox.py web dev
  python tools/hbox.py web build
  python tools/hbox.py web local-init
  python tools/hbox.py web local-build
  python tools/hbox.py web local-flash-stm32 --simple-execute
  python tools/hbox.py web local-ch585-status
  python tools/hbox.py web local-install-ch585-bridge --execute
  python tools/hbox.py web local-serve
  python tools/hbox.py web local-grant-account-role --email user@example.com --role admin
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
    try:
        return subprocess.call(cmd, cwd=_project_root())
    except KeyboardInterrupt:
        # Long-running helpers such as web local-serve already receive the
        # same Ctrl+C and perform their own cleanup. Avoid printing a second
        # parent-process traceback after that graceful shutdown.
        return 130


def _run_tx_build() -> int:
    """Build the accepted CH585 TX image before the normal flash flow."""

    project_root = _project_root()
    tx_makefile = project_root / "RF_PHY_Hop" / "TX" / "Makefile"
    tx_firmware = (
        project_root
        / "RF_PHY_Hop"
        / "TX"
        / "build_tx"
        / "RF_PHY_Hop_TX.bin"
    )
    if not tx_makefile.is_file():
        print(f"错误: 未找到 CH585 TX Makefile: {tx_makefile}")
        return 2

    print("正在构建 CH585 TX 固件...")
    try:
        rc = subprocess.call(
            ["make", "-C", "RF_PHY_Hop/TX"],
            cwd=project_root,
        )
    except FileNotFoundError as exc:
        print(f"错误: 未找到 make: {exc}")
        return 2
    except KeyboardInterrupt:
        return 130

    if rc != 0:
        print("错误: CH585 TX 构建失败，已停止烧录。")
        return rc
    if not tx_firmware.is_file():
        print(f"错误: CH585 TX 构建产物不存在: {tx_firmware}")
        return 2

    print(f"CH585 TX 构建完成: {tx_firmware}")
    return 0


def _local_webconfig_state_is_initialized() -> bool:
    return (
        _project_root()
        / ".hbox"
        / "webconfig-local"
        / "manifest.json"
    ).is_file()


def _local_artifacts_are_unlocked_development(
    expected_slot: str | None = None,
) -> bool:
    manifest_path = (
        _project_root()
        / ".hbox"
        / "webconfig-local"
        / "artifacts"
        / "artifact-manifest.json"
    )
    if not manifest_path.is_file():
        print(f"错误: 未找到本地固件 manifest: {manifest_path}")
        print("请先执行 web local-build；开发板构建必须为 unlocked-development。")
        return False
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        print(f"错误: 无法读取本地固件 manifest: {exc}")
        return False

    if (
        manifest.get("bootSecurityMode") != "unlocked-development"
        or manifest.get("requiresManualLifecycleProvisioning") is not False
        or manifest.get("requiredLifecycle") not in ([], None)
    ):
        print("错误: 拒绝烧录非 unlocked-development 的 STM32 产物。")
        print("禁止要求或修改 RDP、SECURITY、SCAR、Option Bytes 或任何锁定状态。")
        print("请重新执行: python tools/hbox.py web local-build --unlocked-development")
        return False
    if expected_slot is not None:
        artifact_slot = str(manifest.get("targetSlot", "")).upper()
        normalized_slot = expected_slot.upper()
        if artifact_slot != normalized_slot:
            print(
                f"错误: 现有产物是槽 {artifact_slot or 'UNKNOWN'}，"
                f"拒绝按槽 {normalized_slot} 烧录。"
            )
            print(
                f"请先执行: python tools/hbox.py flash app "
                f"{normalized_slot} --build"
            )
            return False
    return True


def _run_secure_application_flash(slot: str, build: bool = False) -> int:
    """Flash an existing slot artifact, optionally rebuilding it first."""

    normalized_slot = slot.upper()
    if build:
        if not _local_webconfig_state_is_initialized():
            print("首次安全烧录：正在创建本机调试用设备身份和签名密钥...")
            rc = _run_python_tool("webconfig_local.py", ["init"])
            if rc != 0:
                return rc

        print(f"正在构建并签名 Application 槽 {normalized_slot}...")
        rc = _run_python_tool(
            "webconfig_local.py",
            [
                "build",
                "--slot",
                normalized_slot,
                "--skip-web",
                "--jobs",
                "4",
                "--unlocked-development",
            ],
        )
        if rc != 0:
            return rc
    else:
        print(f"使用现有 Application 槽 {normalized_slot} 产物（不重新编译）...")

    if not _local_artifacts_are_unlocked_development(normalized_slot):
        return 2

    print("开始无锁开发烧录：槽内容先写入，签名 metadata 最后提交...")
    rc = _run_python_tool("webconfig_flash.py", ["--simple-execute"])
    if rc == 0:
        print(f"Application 槽 {normalized_slot} 已烧录并提交签名 metadata。")
        print("flash code 仅用于明确需要的低层纯代码写入。")
    return rc


def _run_node_makefsdata() -> int:
    www_dir = _project_root() / "application" / "www"
    script = www_dir / "makefsdata.js"
    if not script.exists():
        print(f"错误: 未找到 WebResources 生成脚本: {script}")
        return 2
    return subprocess.call(["node", str(script)], cwd=www_dir)


def _run_npm_script(script_name: str) -> int:
    www_dir = _project_root() / "application" / "www"
    package_json = www_dir / "package.json"
    if not package_json.exists():
        print(f"错误: 未找到 WebConfig 工程: {package_json}")
        return 2

    last_error = None
    for executable in ("npm", "npm.cmd"):
        try:
            return subprocess.call(
                [executable, "run", script_name],
                cwd=www_dir,
            )
        except FileNotFoundError as exc:
            last_error = exc

    print(f"错误: 未找到 npm: {last_error}")
    print("请先安装 Node.js，并执行: npm --prefix application/www install")
    return 2


def _run_hosted_web_build() -> int:
    rc = _run_npm_script("build:hosted")
    if rc != 0:
        return rc

    output_dir = _project_root() / "application" / "www" / "build"
    if not (output_dir / "index.html").exists():
        print(f"错误: Hosted WebConfig 构建产物不存在: {output_dir}")
        return 2

    print(f"V2 Hosted WebConfig 已生成: {output_dir}")
    print("该目录应部署到 HTTPS 服务器，不会打包进 STM32 固件")
    return 0


def _run_pack_assets() -> int:
    out_file = _project_root() / "application" / "build" / "system_assets.bin"
    in_dir = _project_root() / "application" / "assets" / "sysicons"
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
  python tools/hbox.py flash bootloader-dev
  python tools/hbox.py flash app A
  python tools/hbox.py flash app A --build
  python tools/hbox.py flash appAll A
  python tools/hbox.py flash tx
  python tools/hbox.py flash tx --build
  python tools/hbox.py release auto --version 1.0.0
  python tools/hbox.py web dev
  python tools/hbox.py web build
  python tools/hbox.py web local-init
  python tools/hbox.py web local-build
  python tools/hbox.py web local-flash-stm32 --simple-execute
  python tools/hbox.py web local-serve
""",
    )

    subparsers = parser.add_subparsers(dest="cmd", required=True)

    p_build = subparsers.add_parser("build", help="构建相关")
    p_build.add_argument("target", choices=["bootloader", "web", "assets", "sysbg", "ADCMapping", "app", "appAll"])
    p_build.add_argument("slot", nargs="?", choices=["A", "B"])

    p_flash = subparsers.add_parser("flash", help="烧录相关")
    p_flash.add_argument(
        "target",
        choices=[
            "bootloader",
            "bootloader-dev",
            "app",
            "code",
            "appAll",
            "assets",
            "sysbg",
            "tx",
        ],
    )
    p_flash.add_argument("slot", nargs="?", choices=["A", "B"])
    p_flash.add_argument("--code-only", action="store_true", help="仅烧录代码（app），不烧录资源")
    p_flash.add_argument(
        "--build",
        action="store_true",
        help=(
            "flash app 时先重新构建并签名，flash tx 时先构建 TX；"
            "默认只烧录现有产物"
        ),
    )

    p_release = subparsers.add_parser("release", help="发版相关")
    p_release.add_argument("target", choices=["auto", "flash"])
    p_release.add_argument("args", nargs=argparse.REMAINDER)

    p_web = subparsers.add_parser("web", help="Web前端")
    p_web.add_argument(
        "target",
        choices=[
            "dev",
            "build",
            "local-init",
            "local-build",
            "local-flash-stm32",
            "local-flash-ch585",
            "local-ch585-status",
            "local-install-ch585-bridge",
            "local-serve",
            "local-grant-account-role",
            "local-status",
            "probe-revision",
        ],
    )
    p_web.add_argument("args", nargs=argparse.REMAINDER)

    args = parser.parse_args(argv)

    if args.cmd == "build":
        if args.target == "bootloader":
            return _run_python_tool("build.py", ["build", "bootloader"])
        if args.target == "web":
            return _run_hosted_web_build()
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
            rc = _run_pack_assets()
            if rc == 0:
                print("V2 appAll 构建完成（WebConfig 由服务器独立部署）")
            return rc

    if args.cmd == "flash":
        if args.target == "tx":
            if args.build:
                rc = _run_tx_build()
                if rc != 0:
                    return rc
            return _run_python_tool("ch585_stlink_update.py", ["--execute"])
        if args.target == "bootloader":
            return _run_python_tool("build.py", ["flash", "bootloader"])
        if args.target == "bootloader-dev":
            return _run_python_tool("build.py", ["flash", "bootloader-dev"])
        if args.target == "assets":
            return _run_python_tool("build.py", ["flash", "assets"])
        if args.target == "sysbg":
            return _run_python_tool("build.py", ["flash", "sysbg"])
        if args.target == "app":
            if not args.slot:
                print("错误: flash app 需要指定槽位 A 或 B")
                return 2
            return _run_secure_application_flash(args.slot, build=args.build)
        if args.target == "code":
            if not args.slot:
                print("错误: flash code 需要指定槽位 A 或 B")
                return 2
            return _run_python_tool("build.py", ["flash", "app", args.slot])
        if args.target == "appAll":
            if not args.slot:
                print("错误: flash appAll 需要指定槽位 A 或 B")
                return 2
            if args.code_only:
                return _run_python_tool("build.py", ["flash", "app", args.slot])
            return _run_secure_application_flash(args.slot, build=True)

    if args.cmd == "release":
        if args.target == "auto":
            return _run_python_tool("release.py", ["auto", *args.args])
        if args.target == "flash":
            return _run_python_tool("release.py", ["flash", *args.args])

    if args.cmd == "web":
        if args.target == "local-grant-account-role":
            database = (
                _project_root()
                / ".hbox"
                / "webconfig-local"
                / "server-data"
                / "user_accounts.sqlite3"
            )
            return subprocess.call(
                [
                    "node",
                    str(
                        _project_root()
                        / "server"
                        / "scripts"
                        / "account-role.js"
                    ),
                    "--database",
                    str(database),
                    *args.args,
                ],
                cwd=_project_root(),
            )
        if args.target == "dev":
            return _run_npm_script("dev:hosted")
        if args.target == "build":
            return _run_hosted_web_build()
        if args.target == "local-flash-stm32":
            if not _local_artifacts_are_unlocked_development():
                return 2
            return _run_python_tool("webconfig_flash.py", args.args)
        if args.target == "local-flash-ch585":
            return _run_python_tool("ch585_stlink_update.py", args.args)
        if args.target == "local-ch585-status":
            return _run_python_tool("ch585_stlink_update.py", ["--status", *args.args])
        if args.target == "local-install-ch585-bridge":
            return _run_python_tool("ch585_bridge_install.py", args.args)
        local_commands = {
            "local-init": "init",
            "local-build": "build",
            "local-serve": "serve",
            "local-status": "status",
            "probe-revision": "probe-revision",
        }
        if args.target in local_commands:
            local_args = list(args.args)
            if (args.target == "local-build" and
                    "--unlocked-development" not in local_args):
                local_args.append("--unlocked-development")
            return _run_python_tool(
                "webconfig_local.py",
                [local_commands[args.target], *local_args],
            )

    return 2


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
