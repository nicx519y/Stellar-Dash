#!/usr/bin/env python3
"""Verify the source and byte-level ABI frozen for the PCB migration."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "docs" / "rf_frozen_manifest.sha256"
BINARY_MANIFEST = ROOT / "docs" / "rf_frozen_binaries.sha256"
BEHAVIOR_BASELINE = ROOT / "docs" / "rf_frozen_behavior_baseline.json"
SHA256_RE = re.compile(r"^[0-9a-fA-F]{64}$")
TX_CORE_RE = re.compile(
    r"^RF_PHY_Hop/TX/APP/(?:include/)?"
    r"(?:RF_PHY|rfm_(?:spi_bridge|spi_command_txn|spi_reliable_event|"
    r"cold_boot|input_stream))\.(?:c|h)$"
)
APP_CORE_RE = re.compile(
    r"^application/Cpp_Core/(?:Inc|Src)/"
    r"(?:rf_transport|rf_command_transaction|rf_reliable_event|"
    r"report_scheduler)\.(?:hpp|cpp)$"
)


def _crc8_atm(data: bytes) -> int:
    crc = 0
    for value in data:
        crc ^= value
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc


def _checksum8(data: bytes) -> int:
    return sum(data) & 0xFF


def _latency_q8(us: int) -> int:
    if us == 0:
        return 0
    if us <= 512:
        return min(128, max(1, (us + 2) // 4))
    if us <= 2048:
        return min(224, 128 + ((us - 512 + 8) // 16))
    return min(255, 224 + ((us - 2048 + 64) // 128))


def _is_frozen_path(path: str) -> bool:
    return (
        path.startswith("RF_PHY_Hop/Common/include/")
        or path.startswith("RF_PHY_Hop/RX/")
        or TX_CORE_RE.fullmatch(path) is not None
        or path == "RF_PHY_Hop/TX/APP/include/rfm_config.h"
        or APP_CORE_RE.fullmatch(path) is not None
    )


def _read_manifest() -> dict[str, str]:
    entries: dict[str, str] = {}
    for line_number, raw_line in enumerate(
        MANIFEST.read_text(encoding="utf-8").splitlines(), start=1
    ):
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        fields = line.split(None, 1)
        if len(fields) != 2 or SHA256_RE.fullmatch(fields[0]) is None:
            raise ValueError(f"{MANIFEST}:{line_number}: malformed manifest line")
        digest, relative = fields[0].lower(), fields[1].strip().replace("\\", "/")
        if relative in entries:
            raise ValueError(f"{MANIFEST}:{line_number}: duplicate path: {relative}")
        candidate = (ROOT / relative).resolve()
        try:
            candidate.relative_to(ROOT)
        except ValueError as exc:
            raise ValueError(
                f"{MANIFEST}:{line_number}: path escapes repository: {relative}"
            ) from exc
        entries[relative] = digest
    if not entries:
        raise ValueError(f"{MANIFEST}: no frozen files")
    return entries


def _git_frozen_paths() -> set[str] | None:
    if not (ROOT / ".git").exists():
        return None
    result = subprocess.run(
        ["git", "-C", str(ROOT), "ls-files", "-z"],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if result.returncode != 0:
        message = result.stderr.decode("utf-8", errors="replace").strip()
        raise RuntimeError(f"git ls-files failed: {message}")
    paths = result.stdout.decode("utf-8", errors="strict").split("\0")
    return {path for path in paths if path and _is_frozen_path(path)}


def _check_golden_vectors() -> list[str]:
    errors: list[str] = []

    input_without_crc = (
        bytes((0x2A, 0x11))
        + (0x00012345).to_bytes(4, "little")
        + (125).to_bytes(2, "little")
        + bytes((0x00,))
    )
    input_payload = input_without_crc + bytes((_crc8_atm(input_without_crc),))
    expected_input = bytes.fromhex("2A 11 45 23 01 00 7D 00 00 21")
    if input_payload != expected_input or len(input_payload) != 10:
        errors.append("generated 10B input payload differs from frozen vector")

    spi_without_checksum = bytes((0xA5, 0x06, len(input_payload))) + input_payload
    spi_frame = spi_without_checksum + bytes((_checksum8(spi_without_checksum),))
    expected_spi = bytes.fromhex(
        "A5 06 0A 2A 11 45 23 01 00 7D 00 00 21 F7"
    )
    if spi_frame != expected_spi or len(spi_frame) != 14:
        errors.append("generated 14B SPI frame differs from frozen vector")

    header_data_8k_link = (1 << 6) | (3 << 4) | 0x08
    rf_input = bytes(
        (
            header_data_8k_link,
            0x2A,
            0x45,
            0x23,
            0x01,
            _latency_q8(125),
            _latency_q8(64),
        )
    )
    expected_rf_input = bytes.fromhex("78 2A 45 23 01 1F 10")
    if rf_input != expected_rf_input or len(rf_input) != 7:
        errors.append("generated 7B RF input differs from frozen vector")

    header_control = header_data_8k_link | 0x01 | 0x02
    rf_control = bytes(
        (
            header_control,
            0x2A,
            0x10,
            0x16,
            0x00,
            0x00,
            0x07,
            0x27,
            0x34,
            0x12,
            0x55,
            0x02,
        )
    )
    expected_rf_control = bytes.fromhex(
        "7B 2A 10 16 00 00 07 27 34 12 55 02"
    )
    if rf_control != expected_rf_control or len(rf_control) != 12:
        errors.append("generated 12B RF control packet differs from frozen vector")

    return errors


def _check_behavior_constants() -> list[str]:
    errors: list[str] = []
    protocol = (ROOT / "RF_PHY_Hop/Common/include/rf_hop_protocol.h").read_text(
        encoding="utf-8"
    )
    tx_phy = (ROOT / "RF_PHY_Hop/TX/APP/RF_PHY.c").read_text(
        encoding="utf-8", errors="replace"
    )
    tx_makefile = (ROOT / "RF_PHY_Hop/TX/Makefile").read_text(encoding="utf-8")
    required = {
        "RFH_AIR_PACKET_LEN": (protocol, r"#define\s+RFH_AIR_PACKET_LEN\s+12u\b"),
        "RFH_INPUT_AIR_PACKET_LEN": (
            protocol,
            r"#define\s+RFH_INPUT_AIR_PACKET_LEN\s+"
            r"\(RFH_DATA_OFFSET\s*\+\s*RFH_INPUT_AIR_DATA_LEN\)",
        ),
        "RFH_SLOT_US": (protocol, r"#define\s+RFH_SLOT_US\s+125u\b"),
        "RFH_HOP_CHANNELS": (
            protocol,
            r"#define\s+RFH_HOP_CHANNELS\s+\{\s*10u,\s*16u,\s*22u,\s*"
            r"24u,\s*28u,\s*34u,\s*39u\s*\}",
        ),
        "ACK interval": (
            tx_phy,
            r"#define\s+RF_AUTO_DEMO_ACK_INTERVAL_MS\s+500u\b",
        ),
        "ACK burst": (
            tx_phy,
            r"#define\s+RF_AUTO_DEMO_ACK_REQUEST_BURST\s+3u\b",
        ),
        "ACK timeout": (
            tx_phy,
            r"#define\s+RF_AUTO_DEMO_ACK_RX_TIMEOUT_US\s+1200u\b",
        ),
        "TX RF_8K": (tx_makefile, r"-DRF_8K=1\b"),
        "TX hop role": (tx_makefile, r"MODE_DEFINE\s*:=\s*-DRF_HOP_MODE=1\b"),
    }
    for name, (text, pattern) in required.items():
        if re.search(pattern, text, flags=re.MULTILINE) is None:
            errors.append(f"frozen behavior constant changed: {name}")
    return errors


def _check_behavior_baseline() -> list[str]:
    expected = {
        "wire": {
            "stm32PayloadBytes": 10,
            "spiFrameBytes": 14,
            "normalAirBytes": 7,
            "controlAirBytes": 12,
        },
        "ratesHz": [1000, 2000, 4000, 8000],
        "slotUsAt8k": 125,
        "ack": {
            "intervalMs": 500,
            "requestBurstPackets": 3,
            "rxTimeoutUs": 1200,
        },
        "hopChannels": [10, 16, 22, 24, 28, 34, 39],
        "discoveryChannels": [16, 39],
        "pairAccessAddress": "0x6D5A3C17",
        "traceCount": 3,
    }
    try:
        baseline = json.loads(BEHAVIOR_BASELINE.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        return [f"invalid RF behavior baseline: {exc}"]

    actual = {
        "wire": baseline.get("wire"),
        "ratesHz": baseline.get("scheduler", {}).get("ratesHz"),
        "slotUsAt8k": baseline.get("scheduler", {}).get("slotUsAt8k"),
        "ack": baseline.get("ack"),
        "hopChannels": baseline.get("hopChannels"),
        "discoveryChannels": baseline.get("discoveryChannels"),
        "pairAccessAddress": baseline.get("pairing", {}).get(
            "pairAccessAddress"
        ),
        "traceCount": len(baseline.get("traces", [])),
    }
    return (
        []
        if actual == expected
        else ["machine-readable RF behavior/trace baseline changed"]
    )


def _check_rx_binary(require: bool) -> tuple[list[str], bool]:
    errors: list[str] = []
    if not BINARY_MANIFEST.is_file():
        return ([f"missing binary baseline: {BINARY_MANIFEST}"], False)

    verified = False
    for line_number, raw_line in enumerate(
        BINARY_MANIFEST.read_text(encoding="utf-8").splitlines(), start=1
    ):
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        fields = line.split(None, 2)
        if (
            len(fields) != 3
            or SHA256_RE.fullmatch(fields[0]) is None
            or not fields[1].isdigit()
        ):
            errors.append(
                f"{BINARY_MANIFEST}:{line_number}: malformed binary baseline"
            )
            continue
        expected_digest, expected_size_text, relative = fields
        path = ROOT / relative
        if not path.is_file():
            if require:
                errors.append(
                    f"missing RX binary; rebuild with the frozen toolchain: {relative}"
                )
            continue
        data = path.read_bytes()
        expected_size = int(expected_size_text)
        actual_digest = hashlib.sha256(data).hexdigest()
        if len(data) != expected_size or actual_digest != expected_digest.lower():
            errors.append(
                f"frozen RX binary changed: {relative}\n"
                f"  expected {expected_size} bytes {expected_digest.lower()}\n"
                f"  actual   {len(data)} bytes {actual_digest}"
            )
        else:
            verified = True
    return errors, verified


def check(require_rx_binary: bool = False) -> tuple[list[str], bool]:
    errors: list[str] = []
    if not MANIFEST.is_file():
        return [f"missing manifest: {MANIFEST}"], False

    try:
        entries = _read_manifest()
    except (OSError, ValueError) as exc:
        return [str(exc)], False

    for relative, expected in entries.items():
        path = ROOT / relative
        if not path.is_file():
            errors.append(f"missing frozen file: {relative}")
            continue
        actual = hashlib.sha256(path.read_bytes()).hexdigest()
        if actual != expected:
            errors.append(
                f"frozen file changed: {relative}\n"
                f"  expected {expected}\n"
                f"  actual   {actual}"
            )

    try:
        tracked = _git_frozen_paths()
    except RuntimeError as exc:
        errors.append(str(exc))
    else:
        if tracked is not None:
            manifested = set(entries)
            for relative in sorted(tracked - manifested):
                errors.append(f"unmanifested frozen file: {relative}")
            for relative in sorted(manifested - tracked):
                errors.append(f"manifest path is not a tracked frozen file: {relative}")

    errors.extend(_check_golden_vectors())
    errors.extend(_check_behavior_constants())
    errors.extend(_check_behavior_baseline())
    binary_errors, binary_verified = _check_rx_binary(require_rx_binary)
    errors.extend(binary_errors)
    return errors, binary_verified


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--quiet", action="store_true", help="print only failures")
    parser.add_argument(
        "--require-rx-binary",
        action="store_true",
        help="also require and hash the reproducible frozen RX .bin artifact",
    )
    args = parser.parse_args()

    errors, binary_verified = check(args.require_rx_binary)
    if errors:
        print("[RF-FREEZE] FAILED", file=sys.stderr)
        for error in errors:
            print(f"- {error}", file=sys.stderr)
        return 1

    if not args.quiet:
        entries = _read_manifest()
        print(
            f"[RF-FREEZE] OK: {len(entries)} frozen files; "
            "generated 10B/14B/7B/12B vectors and RF behavior constants "
            f"verified; RX binary {'verified' if binary_verified else 'not present'}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
