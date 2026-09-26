#!/usr/bin/env python3
"""Safely stage a CH585 combined BIN through STM32 external QSPI.

This daily command has one deliberately narrow write scope: QSPI sectors
0x90780000..0x907FFFFF.  It cannot build/install STM32 firmware and contains no
internal-Flash, option-byte, protection, unlock, or CH585-IAP write command.
"""

from __future__ import annotations

import argparse
import hashlib
import re
import struct
import subprocess
import tempfile
import time
import zlib
from dataclasses import dataclass
from pathlib import Path

from build import BuildTool
from webconfig_flash import (
    AUTOMATIC_STLINK_BINDING,
    resolve_openocd_executable,
)


PROJECT_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_FIRMWARE = (
    PROJECT_ROOT / "RF_PHY_Hop" / "TX" / "build_tx" / "RF_PHY_Hop_TX.bin"
)

STAGING_ADDRESS = 0x90780000
STAGING_BYTES = 0x00080000
STAGING_HEADER_BYTES = 0x10000
STAGING_DATA_ADDRESS = STAGING_ADDRESS + STAGING_HEADER_BYTES
STAGING_DATA_BYTES = STAGING_BYTES - STAGING_HEADER_BYTES
CH585_IAP_BYTES = 0x1000

RECORD_MAGIC = 0x32433835
RECORD_VERSION = 2
RECORD_BYTES = 256
RECORD_COMMIT = 0x54494D43
RECORD_CRC_BYTES = 64
RECORD_FORMAT = "<IHHBBBBIIII32sII184sI"

STATE_READY = 1
STATE_CLAIMED = 2
STATE_APPLIED = 3
STATE_FAILED = 4
STATE_NAMES = {
    STATE_READY: "READY",
    STATE_CLAIMED: "CLAIMED",
    STATE_APPLIED: "APPLIED",
    STATE_FAILED: "FAILED",
}
STAGE_NAMES = {
    0: "NONE",
    1: "STAGING",
    2: "PROBE",
    3: "BEGIN",
    4: "WRITE",
    5: "END",
    6: "VERIFY_APP",
    7: "COMPLETE",
}
DEFAULT_SWD_KHZ = 1800


class Ch585StlinkUpdateError(RuntimeError):
    pass


@dataclass(frozen=True)
class StagingRecord:
    index: int
    state: int
    stage: int
    client_status: int
    device_status: int
    generation: int
    image_size: int
    error_offset: int
    progress: int
    sha256: bytes
    image_crc32: int


def validate_firmware(firmware: bytes) -> None:
    if len(firmware) <= CH585_IAP_BYTES:
        raise Ch585StlinkUpdateError("CH585 image does not contain an application")
    if len(firmware) > STAGING_DATA_BYTES:
        raise Ch585StlinkUpdateError("CH585 image exceeds the 448 KiB payload area")
    if len(firmware) % 4 != 0:
        raise Ch585StlinkUpdateError("CH585 image size must be four-byte aligned")


def build_ready_record(firmware: bytes, generation: int) -> bytes:
    validate_firmware(firmware)
    digest = hashlib.sha256(firmware).digest()
    app_crc = zlib.crc32(firmware[CH585_IAP_BYTES:]) & 0xFFFFFFFF
    fields = [
        RECORD_MAGIC,
        RECORD_VERSION,
        RECORD_BYTES,
        STATE_READY,
        1,  # STAGING
        0,
        0,
        generation & 0xFFFFFFFF,
        len(firmware),
        0,
        0,
        digest,
        app_crc,
        0,
        bytes([0xFF]) * 184,
        RECORD_COMMIT,
    ]
    record = bytearray(struct.pack(RECORD_FORMAT, *fields))
    struct.pack_into("<I", record, 64, zlib.crc32(record[:64]) & 0xFFFFFFFF)
    if len(record) != RECORD_BYTES:
        raise AssertionError("staging record layout drifted")
    return bytes(record)


def parse_records(header_sector: bytes) -> list[StagingRecord]:
    if len(header_sector) != STAGING_HEADER_BYTES:
        raise Ch585StlinkUpdateError("status dump is not exactly one 64 KiB sector")
    records: list[StagingRecord] = []
    for index in range(STAGING_HEADER_BYTES // RECORD_BYTES):
        raw = header_sector[index * RECORD_BYTES : (index + 1) * RECORD_BYTES]
        unpacked = struct.unpack(RECORD_FORMAT, raw)
        (
            magic,
            version,
            record_bytes,
            state,
            stage,
            client,
            device,
            generation,
            image_size,
            error_offset,
            progress,
            digest,
            image_crc,
            record_crc,
            _reserved,
            commit,
        ) = unpacked
        if (
            magic != RECORD_MAGIC
            or version != RECORD_VERSION
            or record_bytes != RECORD_BYTES
            or commit != RECORD_COMMIT
            or record_crc != (zlib.crc32(raw[:RECORD_CRC_BYTES]) & 0xFFFFFFFF)
        ):
            continue
        records.append(
            StagingRecord(
                index=index,
                state=state,
                stage=stage,
                client_status=client,
                device_status=device,
                generation=generation,
                image_size=image_size,
                error_offset=error_offset,
                progress=progress,
                sha256=digest,
                image_crc32=image_crc,
            )
        )
    return records


def _openocd_status_dump(
    openocd: Path,
    expected_uid: str | None,
    swd_khz: int = DEFAULT_SWD_KHZ,
) -> bytes:
    config = PROJECT_ROOT / "application" / "Openocd_Script" / "ST-LINK-QSPIFLASH.cfg"
    build_dir = PROJECT_ROOT / "application" / "build"
    build_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".ch585-status-", dir=build_dir) as temp:
        destination = Path(temp) / "header.bin"
        commands = [
            "gdb_port disabled",
            "tcl_port disabled",
            "telnet_port disabled",
            "init",
            "halt",
            "qspi_init",
        ]
        if expected_uid is not None:
            commands.extend(BuildTool._openocd_target_assert_commands(expected_uid))
        commands.extend(
            [
                "flash probe 1",
                # Read through the physical stmqspi bank.  The Cortex-M7
                # memory map can retain stale QSPI cache lines across a
                # header update and previously reported an obsolete READY.
                f"flash read_bank 1 "
                f"{BuildTool._openocd_tcl_braced_path(destination, must_exist=False)} "
                f"0x{STAGING_ADDRESS - 0x90000000:08X} "
                f"0x{STAGING_HEADER_BYTES:X}",
                "qspi_init",
                "resume",
                "shutdown",
            ]
        )
        script = Path(temp) / "read-status.tcl"
        script.write_text("\n".join(commands) + "\n", encoding="utf-8")
        tool = BuildTool()
        tool.config["openocd_path"] = str(openocd)
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
        if not tool.run_command(command, PROJECT_ROOT / "application", quiet=True):
            raise Ch585StlinkUpdateError(
                "unable to read CH585 QSPI status sector while STM32 is running"
            )
        data = destination.read_bytes()
    return data


def _probe_uid(
    openocd: Path,
    swd_khz: int = DEFAULT_SWD_KHZ,
    *,
    resume_after: bool = True,
    wait_seconds: float = 0.0,
) -> str:
    config = PROJECT_ROOT / "application" / "Openocd_Script" / "ST-LINK-QSPIFLASH.cfg"
    command = [
        str(openocd),
        "-d1",
        "-f",
        str(config),
        "-c",
        f"adapter speed {swd_khz}",
        "-c",
        "gdb_port disabled",
        "-c",
        "tcl_port disabled",
        "-c",
        "telnet_port disabled",
        "-c",
        "init",
        "-c",
        "halt",
        "-c",
        "mdw 0x5C001000 1",
        "-c",
        "mdw 0x1FF1E800 3",
        *(["-c", "resume"] if resume_after else []),
        "-c",
        "shutdown",
    ]
    def run(candidate: list[str]) -> subprocess.CompletedProcess[str]:
        try:
            return subprocess.run(
                candidate,
                cwd=PROJECT_ROOT / "application",
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                check=False,
                timeout=3.0 if wait_seconds > 0 else None,
            )
        except subprocess.TimeoutExpired as exc:
            output = exc.stdout or ""
            if isinstance(output, bytes):
                output = output.decode("utf-8", errors="replace")
            return subprocess.CompletedProcess(candidate, 124, stdout=output)

    deadline = time.monotonic() + max(0.0, wait_seconds)
    while True:
        result = run(command)
        if result.returncode == 0 or time.monotonic() >= deadline:
            break
        time.sleep(0.25)
    output = result.stdout or ""
    if result.returncode != 0:
        raise Ch585StlinkUpdateError(
            f"ST-LINK target probe failed with exit code {result.returncode}"
        )
    id_match = re.search(r"0x5c001000:\s+([0-9a-f]{8})", output, re.I)
    uid_match = re.search(
        r"0x1ff1e800:\s+([0-9a-f]{8})\s+([0-9a-f]{8})\s+([0-9a-f]{8})",
        output,
        re.I,
    )
    if id_match is None or uid_match is None:
        raise Ch585StlinkUpdateError("ST-LINK did not return STM32 identity")
    if (int(id_match.group(1), 16) & 0xFFF) != 0x450:
        raise Ch585StlinkUpdateError("connected target is not STM32H750")
    uid = "".join(uid_match.groups()).upper()
    if uid in {"0" * 24, "F" * 24}:
        raise Ch585StlinkUpdateError("connected STM32 returned an invalid UID")
    return uid


def print_record(record: StagingRecord | None) -> None:
    if record is None:
        print("CH585 staging status: EMPTY/INVALID (normal boot, no update)")
        return
    print(f"CH585 staging status: {STATE_NAMES.get(record.state, 'UNKNOWN')}")
    print(f"  Record index: {record.index}")
    print(f"  Generation: {record.generation}")
    print(f"  Image bytes: {record.image_size}")
    print(f"  Image SHA-256: {record.sha256.hex()}")
    print(f"  IAP stage: {STAGE_NAMES.get(record.stage, 'UNKNOWN')}")
    print(f"  Client/device status: {record.client_status}/{record.device_status}")
    print(f"  Offset/progress: {record.error_offset}/{record.progress}%")


def read_latest_status(
    openocd: Path,
    expected_uid: str | None,
    swd_khz: int = DEFAULT_SWD_KHZ,
) -> StagingRecord | None:
    records = parse_records(_openocd_status_dump(openocd, expected_uid, swd_khz))
    return records[-1] if records else None


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Stage CH585 Application through ST-LINK -> STM32 QSPI -> SPI IAP"
    )
    parser.add_argument("--firmware", type=Path, default=DEFAULT_FIRMWARE)
    parser.add_argument("--openocd", type=Path)
    parser.add_argument("--execute", action="store_true")
    parser.add_argument("--status", action="store_true")
    parser.add_argument("--wait-seconds", type=float, default=45.0)
    parser.add_argument(
        "--probe-wait-seconds",
        type=float,
        default=120.0,
        help="wait for the initial STM32 Reset/SWD attach window (default: 120)",
    )
    parser.add_argument(
        "--swd-khz",
        type=int,
        default=DEFAULT_SWD_KHZ,
        choices=range(50, 10001),
        metavar="50..10000",
        help="SWD clock in kHz (default: 1800; use 100 for first-install recovery)",
    )
    args = parser.parse_args()

    if args.status:
        try:
            openocd = resolve_openocd_executable(args.openocd, allow_automatic=True)
            uid = _probe_uid(openocd, args.swd_khz)
            latest = read_latest_status(openocd, uid, args.swd_khz)
            print(f"Bound STM32 UID: {uid}")
            print_record(latest)
            if latest is None:
                return 0
            if latest.state == STATE_APPLIED:
                return 0
            if latest.state == STATE_FAILED:
                return 1
            return 3
        except Exception as exc:
            print(f"error: status preflight/read failed: {exc}")
            return 2

    try:
        firmware_path = args.firmware.expanduser().resolve(strict=True)
        firmware = firmware_path.read_bytes()
        validate_firmware(firmware)
        ready = build_ready_record(firmware, int(time.time()))
    except (OSError, Ch585StlinkUpdateError) as exc:
        print(f"error: {exc}")
        return 2

    digest = hashlib.sha256(firmware).hexdigest()
    print("CH585 daily staging plan")
    print(f"  Firmware: {firmware_path}")
    print(f"  Firmware bytes/SHA-256: {len(firmware)} / {digest}")
    print(f"  Payload target: 0x{STAGING_DATA_ADDRESS:08X}")
    print(f"  READY journal target (last): 0x{STAGING_ADDRESS:08X}")
    print("  Scope: dedicated QSPI staging only; no Slot/metadata/internal Flash")
    print("  CH585 IAP 0x0000..0x0FFF and all protection settings are untouched")
    if not args.execute:
        print("DRY RUN complete; pass --execute to stage payload then READY")
        return 0

    try:
        openocd = resolve_openocd_executable(args.openocd, allow_automatic=True)
        uid = _probe_uid(
            openocd,
            args.swd_khz,
            resume_after=False,
            wait_seconds=args.probe_wait_seconds,
        )
        print(f"Bound STM32 UID: {uid}")
    except Exception as exc:
        print(f"error: ST-LINK/target preflight failed: {exc}")
        return 2

    tool = BuildTool()
    tool.config["openocd_path"] = str(openocd)
    build_dir = PROJECT_ROOT / "application" / "build"
    with tempfile.TemporaryDirectory(prefix=".ch585-stage-", dir=build_dir) as temp:
        payload_path = Path(temp) / "payload.bin"
        ready_path = Path(temp) / "ready.bin"
        payload_path.write_bytes(firmware)
        ready_path.write_bytes(ready)
        if not tool._flash_qspi_file_in_chunks(
            payload_path,
            STAGING_DATA_ADDRESS,
            "CH585 payload",
            reset_after=False,
            expected_target_uid=uid,
            adapter_speed_khz=args.swd_khz,
            connect_under_reset_fallback=False,
            runtime_attach=True,
            # READY is the second half of the same transaction.  Keeping the
            # core halted prevents the application from making runtime SWD
            # unavailable in the gap between the two OpenOCD processes.
            leave_halted=True,
        ):
            print("error: payload staging failed before READY; CH585 will not update")
            return 2
        if not tool._flash_qspi_file_in_chunks(
            ready_path,
            STAGING_ADDRESS,
            "CH585 READY commit",
            reset_after=True,
            expected_target_uid=uid,
            adapter_speed_khz=args.swd_khz,
            connect_under_reset_fallback=False,
            runtime_attach=True,
        ):
            print("error: READY commit failed; CH585 will not update")
            return 2

    if args.wait_seconds <= 0:
        print("Image safely staged; result not awaited")
        return 3
    print(f"Waiting {args.wait_seconds:g}s before one non-interfering status read...")
    time.sleep(args.wait_seconds)
    try:
        latest = read_latest_status(openocd, uid, args.swd_khz)
    except Exception as exc:
        print(f"Image safely staged, but result read failed: {exc}")
        return 3
    print_record(latest)
    if latest is None or latest.sha256.hex() != digest:
        return 1
    if latest.state == STATE_APPLIED:
        return 0
    if latest.state == STATE_FAILED:
        return 1
    return 3


if __name__ == "__main__":
    raise SystemExit(main())
