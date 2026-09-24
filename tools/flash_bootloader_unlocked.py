"""Build or flash an unlocked STM32H750 bootloader as a complete sector.

This developer operation deliberately replaces the identity and security-version
tail with erased bytes. It issues no explicit STM32 protection-setting command.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import struct
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BOOTLOADER = ROOT / "bootloader"
ARTIFACT_DIR = BOOTLOADER / "build-unlocked"
IMAGE_NAME = "internal-flash-unlocked.bin"
MANIFEST_NAME = "flash-manifest.json"
SECTOR_BASE = 0x08000000
SECTOR_BYTES = 0x20000
BOOTLOADER_BYTES = 0x1C000
IDCODE_ADDRESS = 0x5C001000
EXPECTED_DEV_ID = 0x450


def build_image(make: str, build_dir: Path) -> tuple[bytes, int]:
    print("Building unlocked bootloader (HBOX_SECURE_BOOT_REQUIRED=0)...", flush=True)
    subprocess.run(
        [make, "-C", str(BOOTLOADER),
         f"BUILD_DIR={build_dir.as_posix()}",
         "HBOX_SECURE_BOOT_REQUIRED=0"],
        cwd=ROOT,
        check=True,
        timeout=600,
    )
    binary = build_dir / "bootloader.bin"
    code = binary.read_bytes()
    if len(code) < 8 or len(code) > BOOTLOADER_BYTES:
        raise ValueError("bootloader image exceeds its 112 KiB code region")
    stack, reset_handler = struct.unpack_from("<II", code)
    if not (0x20000000 < stack <= 0x20020000 and
            SECTOR_BASE <= (reset_handler & ~1) < SECTOR_BASE + BOOTLOADER_BYTES and
            reset_handler & 1):
        raise ValueError("bootloader vector table does not target internal Flash/DTCM")
    return code + bytes([0xFF]) * (SECTOR_BYTES - len(code)), len(code)


def save_artifact(image_bytes: bytes, code_bytes: int, directory: Path) -> Path:
    if (len(image_bytes) != SECTOR_BYTES or
            not 8 <= code_bytes <= BOOTLOADER_BYTES or
            image_bytes[code_bytes:] != b"\xff" * (SECTOR_BYTES - code_bytes)):
        raise ValueError("unlocked build did not produce a complete sector image")
    directory = directory.resolve()
    if BOOTLOADER.resolve() not in directory.parents:
        raise ValueError("unlocked artifact directory must stay inside bootloader")
    directory.mkdir(parents=True, exist_ok=True)
    image = directory / IMAGE_NAME
    manifest = directory / MANIFEST_NAME
    digest = hashlib.sha256(image_bytes).hexdigest()
    record = {
        "formatVersion": 1,
        "bootSecurityMode": "unlocked-development",
        "requiresManualLifecycleProvisioning": False,
        "requiredLifecycle": [],
        "baseAddress": SECTOR_BASE,
        "sectorBytes": SECTOR_BYTES,
        "codeBytes": code_bytes,
        "sha256": digest,
    }
    with tempfile.NamedTemporaryFile(
        mode="wb", prefix="hbox-image-", suffix=".tmp",
        dir=directory, delete=False,
    ) as handle:
        staged_image = Path(handle.name)
        handle.write(image_bytes)
    with tempfile.NamedTemporaryFile(
        mode="w", encoding="utf-8", prefix="hbox-manifest-",
        suffix=".tmp", dir=directory, delete=False,
    ) as handle:
        staged_manifest = Path(handle.name)
        json.dump(record, handle, indent=2)
        handle.write("\n")
    try:
        os.replace(staged_image, image)
        os.replace(staged_manifest, manifest)
    finally:
        staged_image.unlink(missing_ok=True)
        staged_manifest.unlink(missing_ok=True)
    return image


def load_artifact(directory: Path) -> Path:
    image = directory / IMAGE_NAME
    manifest = directory / MANIFEST_NAME
    if not image.is_file() or not manifest.is_file():
        raise ValueError(
            "no unlocked bootloader artifact; run "
            "python tools/hbox.py build bootloader or "
            "python tools/hbox.py flash bootloader --build"
        )
    record = json.loads(manifest.read_text(encoding="utf-8"))
    if not isinstance(record, dict):
        raise ValueError("unlocked bootloader manifest must be an object")
    payload = image.read_bytes()
    code_bytes = record.get("codeBytes")
    if (record.get("formatVersion") != 1 or
            record.get("bootSecurityMode") != "unlocked-development" or
            record.get("requiresManualLifecycleProvisioning") is not False or
            record.get("requiredLifecycle") != [] or
            record.get("baseAddress") != SECTOR_BASE or
            record.get("sectorBytes") != SECTOR_BYTES or
            type(code_bytes) is not int or
            not 8 <= code_bytes <= BOOTLOADER_BYTES or
            len(payload) != SECTOR_BYTES or
            payload[code_bytes:] != b"\xff" * (SECTOR_BYTES - code_bytes) or
            record.get("sha256") != hashlib.sha256(payload).hexdigest()):
        raise ValueError("unlocked bootloader artifact failed manifest/image validation")
    stack, reset_handler = struct.unpack_from("<II", payload)
    if not (0x20000000 < stack <= 0x20020000 and
            SECTOR_BASE <= (reset_handler & ~1) < SECTOR_BASE + BOOTLOADER_BYTES and
            reset_handler & 1):
        raise ValueError("unlocked bootloader artifact has an invalid vector table")
    print(f"Full-sector SHA-256: {record['sha256']}", flush=True)
    return image


def flash_image(openocd: str, image: Path, serial: str | None) -> None:
    if image.stat().st_size != SECTOR_BYTES:
        raise ValueError("complete internal Flash image must be exactly 128 KiB")
    encoded = image.as_posix()
    if '"' in encoded or "\n" in encoded or "\r" in encoded:
        raise ValueError("image path cannot be safely passed to OpenOCD")
    command = [openocd, "-d0", "-f", "Openocd_Script/ST-LINK-FLASH.cfg"]
    if serial:
        if not serial.isalnum():
            raise ValueError("ST-Link serial must be alphanumeric")
        command += ["-c", f"hla_serial {serial}"]
    command += [
        "-c", "init",
        "-c", "reset halt",
        "-c", (
            f"set hbox_idcode [mrw 0x{IDCODE_ADDRESS:08X}]; "
            f"if {{($hbox_idcode & 0xFFF) != 0x{EXPECTED_DEV_ID:03X}}} "
            "{error {Unexpected STM32 DEV_ID; refusing sector erase}}"
        ),
        "-c", "flash erase_sector 0 0 0",
        "-c", f'flash write_image "{encoded}" 0x{SECTOR_BASE:08X} bin',
        "-c", f'verify_image "{encoded}" 0x{SECTOR_BASE:08X} bin',
        "-c", "reset run",
        "-c", "shutdown",
    ]
    # The command list is fixed to one sector and one image. In particular it
    # has no Option Byte, unlock, read-unprotect, or mass-erase operation.
    print("Flashing STM32 internal sector 0 and verifying all 128 KiB...", flush=True)
    subprocess.run(command, cwd=BOOTLOADER, check=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--openocd", default="openocd")
    parser.add_argument("--make", default=shutil.which("mingw32-make") or
                        shutil.which("make") or "make")
    parser.add_argument("--stlink-serial")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--build", action="store_true")
    mode.add_argument("--build-only", action="store_true")
    args = parser.parse_args()

    if args.build or args.build_only:
        with tempfile.TemporaryDirectory(prefix="hbox-unlocked-boot-", dir=BOOTLOADER) as temporary:
            build_dir = Path(temporary).resolve()
            if BOOTLOADER.resolve() not in build_dir.parents:
                raise RuntimeError("temporary build directory escaped the bootloader workspace")
            image_bytes, code_bytes = build_image(args.make, build_dir)
            save_artifact(image_bytes, code_bytes, ARTIFACT_DIR)
        if args.build_only:
            load_artifact(ARTIFACT_DIR)
            print("Build-only check completed; no device was written.", flush=True)
            return 0
    image = load_artifact(ARTIFACT_DIR)
    flash_image(args.openocd, image, args.stlink_serial)
    print("Flash/readback completed. 未修改任何保护位或锁定状态。", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
