#!/usr/bin/env python3
"""Build and safely install the STM32 CH585 bridge into the inactive slot.

Only external-QSPI application/ADC/metadata regions are written.  Signed
metadata is the final commit.  The bootloader, STM32 internal Flash, option
bytes, protection registers and CH585 staging area are outside this tool.
"""

from __future__ import annotations

import argparse
import hashlib
import shutil
import struct
import subprocess
import tempfile
from datetime import datetime, timezone
from pathlib import Path

from build import BuildTool
from ch585_stlink_update import DEFAULT_SWD_KHZ, _probe_uid
from firmware_metadata import (
    FIRMWARE_MAGIC,
    METADATA_CRC32_OFFSET,
    METADATA_STRUCT_SIZE,
    SLOT_A_ADC_MAPPING_ADDR,
    SLOT_A_APPLICATION_ADDR,
    SLOT_B_ADC_MAPPING_ADDR,
    SLOT_B_APPLICATION_ADDR,
)
from release import calculate_crc32, create_metadata_binary, verify_signed_metadata
from webconfig_local import (
    DEFAULT_STATE_DIR,
    FIRMWARE_SECURITY_VERSION,
    _state_paths,
    build_local_metadata_components,
)
from webconfig_flash import resolve_openocd_executable


PROJECT_ROOT = Path(__file__).resolve().parents[1]
METADATA_ADDRESS = 0x90570000
ADC_MAPPING_SOURCE = PROJECT_ROOT / "resources" / "slot_a_adc_mapping.bin"


class BridgeInstallError(RuntimeError):
    pass


def validate_current_metadata(metadata: bytes) -> str:
    if len(metadata) != METADATA_STRUCT_SIZE:
        raise BridgeInstallError("current metadata has an invalid size")
    if struct.unpack_from("<I", metadata, 0)[0] != FIRMWARE_MAGIC:
        raise BridgeInstallError("current metadata magic is invalid")
    stored_crc = struct.unpack_from("<I", metadata, METADATA_CRC32_OFFSET)[0]
    expected_crc = calculate_crc32(metadata, METADATA_CRC32_OFFSET, 4)
    if stored_crc == 0 or stored_crc != expected_crc:
        raise BridgeInstallError("current metadata CRC is invalid")
    target = metadata[52]
    if target not in (0, 1):
        raise BridgeInstallError("current metadata target slot is invalid")
    return "A" if target == 0 else "B"


def _read_current_metadata(openocd: Path, uid: str, swd_khz: int) -> bytes:
    config = PROJECT_ROOT / "application" / "Openocd_Script" / "ST-LINK-QSPIFLASH.cfg"
    build_dir = PROJECT_ROOT / "application" / "build"
    build_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".bridge-meta-", dir=build_dir) as temp:
        destination = Path(temp) / "current-metadata.bin"
        commands = [
            "gdb_port disabled",
            "tcl_port disabled",
            "telnet_port disabled",
            "init",
            "halt",
            "qspi_init",
            *BuildTool._openocd_target_assert_commands(uid),
            f"dump_image {BuildTool._openocd_tcl_braced_path(destination, must_exist=False)} "
            f"0x{METADATA_ADDRESS:08X} 0x{METADATA_STRUCT_SIZE:X}",
            "qspi_init",
            "shutdown",
        ]
        script = Path(temp) / "read-metadata.tcl"
        script.write_text("\n".join(commands) + "\n", encoding="utf-8")
        command = [
                str(openocd),
                "-d0",
                "-f",
                str(config),
                "-c",
                f"adapter speed {swd_khz}",
                "-f",
                str(script),
            ]
        result = subprocess.run(
            command,
            cwd=PROJECT_ROOT / "application",
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            check=False,
        )
        if result.returncode != 0:
            raise BridgeInstallError("unable to read active-slot metadata")
        return destination.read_bytes()


def _build_inactive_application(slot: str, trust_header: Path) -> Path:
    make = shutil.which("make")
    if make is None:
        raise BridgeInstallError("default Makefile tool 'make' is not available")
    builder = BuildTool()
    backup = builder.modify_linker_script_for_slot(slot)
    if backup is None:
        raise BridgeInstallError(f"unable to prepare Slot {slot} linker script")
    try:
        commands = (
            [make, "clean"],
            [
                make,
                f"-j{builder.config.get('parallel_jobs', builder.cpu_count)}",
                f"HBOX_TRUST_HEADER={trust_header.as_posix()}",
                "HBOX_SECURE_BOOT_REQUIRED=0",
                "POWER_DEVICE_PROBE_ENABLED=1",
                "APP_LOG_ENABLE=1",
            ],
        )
        for command in commands:
            result = subprocess.run(
                command,
                cwd=PROJECT_ROOT / "application",
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                encoding="utf-8",
                errors="replace",
                check=False,
            )
            if result.returncode != 0:
                output = (result.stdout or "").splitlines()
                if output:
                    print("\n".join(output[-80:]))
                raise BridgeInstallError(
                    f"application default Makefile failed with exit code {result.returncode}"
                )
        print("Application default Makefile build completed")
    finally:
        builder.restore_file(backup)
    application = PROJECT_ROOT / "application" / "build" / "application.bin"
    if not application.is_file() or application.stat().st_size == 0:
        raise BridgeInstallError("application Makefile produced no application.bin")
    return application


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Install the STM32 CH585 bridge into the current inactive QSPI slot"
    )
    parser.add_argument("--openocd", type=Path)
    parser.add_argument("--state-dir", type=Path, default=DEFAULT_STATE_DIR)
    parser.add_argument("--execute", action="store_true")
    parser.add_argument(
        "--swd-khz",
        type=int,
        default=DEFAULT_SWD_KHZ,
        choices=range(50, 10001),
        metavar="50..10000",
        help="SWD clock in kHz (default: 1800; use 100 for first-install recovery)",
    )
    parser.add_argument(
        "--probe-wait-seconds",
        type=float,
        default=0.0,
        help="wait for a powered/awake target, then keep it halted through install",
    )
    args = parser.parse_args()

    print("STM32 CH585 bridge install plan")
    print("  Build: repository application Makefile only")
    print("  Target: automatically detected inactive external-QSPI slot")
    print("  Commit: signed metadata last, after application/ADC verification")
    print("  Excluded: internal Flash, bootloader, CH585 staging, Option Bytes and locks")
    if not args.execute:
        print("DRY RUN complete; pass --execute to probe, build and install")
        return 0

    try:
        openocd = resolve_openocd_executable(args.openocd, allow_automatic=True)
        uid = _probe_uid(
            openocd,
            args.swd_khz,
            resume_after=False,
            wait_seconds=args.probe_wait_seconds,
        )
        current_metadata = _read_current_metadata(openocd, uid, args.swd_khz)
        active_slot = validate_current_metadata(current_metadata)
        target_slot = "B" if active_slot == "A" else "A"
        state_dir = args.state_dir.expanduser().resolve(strict=True)
        paths = _state_paths(state_dir)
        for name in ("trust_header", "firmware_private", "firmware_public"):
            if not paths[name].is_file():
                raise BridgeInstallError(f"local WebConfig file is missing: {paths[name]}")
        adc_mapping = ADC_MAPPING_SOURCE.resolve(strict=True)
        from device_identity_provisioning import load_public_key

        verify_signed_metadata(
            current_metadata, load_public_key(paths["firmware_public"])
        )
    except (OSError, ValueError, BridgeInstallError) as exc:
        print(f"error: bridge preflight failed: {exc}")
        return 2

    print(f"Bound STM32 UID: {uid}")
    print(f"Active slot: {active_slot}; safe install target: {target_slot}")
    try:
        application = _build_inactive_application(target_slot, paths["trust_header"])
        components = build_local_metadata_components(
            application, adc_mapping, target_slot
        )
        metadata = create_metadata_binary(
            version="2.0.0",
            slot=target_slot,
            build_date=datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M:%S"),
            components=components,
            signing_key=paths["firmware_private"],
            security_version=FIRMWARE_SECURITY_VERSION,
            webresources_optional=True,
        )
        verify_signed_metadata(metadata, load_public_key(paths["firmware_public"]))
    except (OSError, RuntimeError, ValueError, BridgeInstallError) as exc:
        print(f"error: bridge build/signing failed before hardware writes: {exc}")
        return 2

    app_address = (
        SLOT_A_APPLICATION_ADDR if target_slot == "A" else SLOT_B_APPLICATION_ADDR
    )
    adc_address = (
        SLOT_A_ADC_MAPPING_ADDR if target_slot == "A" else SLOT_B_ADC_MAPPING_ADDR
    )
    print(
        f"Application: {application.stat().st_size} bytes, "
        f"SHA-256 {hashlib.sha256(application.read_bytes()).hexdigest()}"
    )

    tool = BuildTool()
    tool.config["openocd_path"] = str(openocd)
    build_dir = PROJECT_ROOT / "application" / "build"
    with tempfile.TemporaryDirectory(prefix=".bridge-install-", dir=build_dir) as temp:
        metadata_path = Path(temp) / "metadata.bin"
        metadata_path.write_bytes(metadata)
        stages = (
            (adc_mapping, adc_address, f"Slot {target_slot} ADC mapping", False),
            (application, app_address, f"Slot {target_slot} CH585 bridge", False),
            (metadata_path, METADATA_ADDRESS, "signed metadata final commit", True),
        )
        for source, address, label, reset_after in stages:
            if not tool._flash_qspi_file_in_chunks(
                source,
                address,
                label,
                reset_after=reset_after,
                expected_target_uid=uid,
                adapter_speed_khz=args.swd_khz,
                connect_under_reset_fallback=False,
                runtime_attach=True,
                leave_halted=True,
            ):
                print(
                    f"error: {label} failed; original active Slot {active_slot} remains selected"
                )
                return 1

    print(f"Bridge installed and metadata switched to Slot {target_slot}")
    print("No STM32 or CH585 protection/lock setting was read or changed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
