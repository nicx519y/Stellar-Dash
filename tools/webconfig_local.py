#!/usr/bin/env python3
"""Production-protocol WebConfig V2 laboratory workflow.

This helper keeps the runtime protocol identical to the production V2 path:
signed boot metadata, a manufacturer-signed device certificate, boot
attestation, a server-signed permit, and encrypted WebHID RPCs are all still
required.  It deliberately uses local PEM keys and an exported development
device scalar, so it is a *laboratory manufacturing substitute*, not a
production key-custody or factory-enrollment implementation.

All private material and generated provisioning images live below the ignored
``.hbox/webconfig-local`` directory.  Hardware writes are available only from
the explicit ``flash-stm32 --execute`` command after artifact, tool, target,
backup, and blank-security-tail preflights.  That command never changes STM32
option bytes; RDP/Secure/SCAR transitions still require a separately reviewed
factory procedure because a mistake can make a development board inaccessible.
"""

from __future__ import annotations

import argparse
import base64
import binascii
import hashlib
import json
import os
import re
import secrets
import shutil
import struct
import subprocess
import sys
import time
import urllib.error
import urllib.request
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Sequence


TOOLS_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = TOOLS_DIR.parent
DEFAULT_STATE_DIR = PROJECT_ROOT / ".hbox" / "webconfig-local"

sys.path.insert(0, str(TOOLS_DIR))
sys.path.insert(0, str(PROJECT_ROOT / "common"))

from device_identity_provisioning import (  # noqa: E402
    P256_ORDER,
    _public_key_from_private_scalar,
    assemble_certificate,
    build_development_identity_slot,
    create_certificate_tbs,
    encode_product_id,
    load_public_key,
    render_trust_bundle_header,
    sign_certificate_tbs_for_lab,
    verify_certificate,
    verify_development_identity_slot,
)
from firmware_metadata import (  # noqa: E402
    FIRMWARE_COMPONENT_COUNT,
    FIRMWARE_HASH_OFFSET,
    FIRMWARE_SECURITY_VERSION,
    METADATA_CRC32_OFFSET,
    METADATA_STRUCT_SIZE,
    SLOT_A_ADC_MAPPING_ADDR,
    SLOT_A_APPLICATION_ADDR,
    SLOT_A_WEBRESOURCES_ADDR,
    SLOT_B_ADC_MAPPING_ADDR,
    SLOT_B_APPLICATION_ADDR,
    SLOT_B_WEBRESOURCES_ADDR,
    SYS_IMAGE_RESOURCES_ADDR,
    SYS_IMAGE_RESOURCES_SIZE,
    USER_IMAGE_RESOURCES_ADDR,
    USER_IMAGE_RESOURCES_SIZE,
)
from build import BuildTool  # noqa: E402
from firmware_signing import (  # noqa: E402
    canonical_metadata,
    export_uncompressed_public_key,
)
from release import create_metadata_binary, verify_signed_metadata  # noqa: E402


INTERNAL_FLASH_BYTES = 0x20000
BOOTLOADER_LIMIT = 0x1C000
IDENTITY_OFFSET = 0x1C000
SECURITY_VERSION_OFFSET = 0x1D000
SECURITY_VERSION_RECORD_BYTES = 32
SECURITY_VERSION_MAGIC = 0x31564A53
SECURITY_VERSION_FORMAT = 1
SECURITY_VERSION_COMMITTED = 0x434D4954
LAB_MANIFEST_VERSION = 2
ARTIFACT_MANIFEST_VERSION = 3
# Product/PCB identity is carried in the manufacturer-signed device
# certificate. It must not be inferred from STM32 silicon DEV_ID/REV_ID.
HARDWARE_VERSION = 0x00020000
DEVICE_AUTH_LEVEL_MCU_PROTECTED = 1
SYSTEM_ASSETS_FILENAME = "system_assets.bin"
SYSTEM_BACKGROUND_FILENAME = "sysbg.bin"
SYSTEM_ASSETS_MAGIC = b"HIMG"
SYSTEM_ASSETS_VERSION = 1
SYSTEM_ASSETS_HEADER_BYTES = 64
SYSTEM_ASSETS_ENTRY_BYTES = 64
SYSTEM_BACKGROUND_MAGIC = 0x474D4955  # 'UIMG' in little endian
SYSTEM_BACKGROUND_VERSION = 2
SYSTEM_BACKGROUND_HEADER_BYTES = 4096
SYSTEM_BACKGROUND_WIDTH = 320
SYSTEM_BACKGROUND_HEIGHT = 172
SYSTEM_BACKGROUND_FRAME_BYTES = (
    SYSTEM_BACKGROUND_WIDTH * SYSTEM_BACKGROUND_HEIGHT * 2
)
SYSTEM_BACKGROUND_MAX_FRAMES = 8


class LocalWebConfigError(RuntimeError):
    """A safe, user-actionable local workflow failure."""


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _atomic_write(path: Path, data: bytes, *, private: bool = False) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_bytes(data)
    if private:
        try:
            temporary.chmod(0o600)
        except OSError:
            pass
    temporary.replace(path)


def _atomic_write_text(
    path: Path, value: str, *, private: bool = False
) -> None:
    _atomic_write(path, value.encode("utf-8"), private=private)


def _run(
    command: Sequence[str],
    *,
    cwd: Path = PROJECT_ROOT,
    environment: dict[str, str] | None = None,
    capture: bool = False,
    quiet: bool = False,
) -> subprocess.CompletedProcess[str]:
    if not quiet:
        print("+", subprocess.list2cmdline([str(part) for part in command]))
    try:
        result = subprocess.run(
            [str(part) for part in command],
            cwd=cwd,
            env=environment,
            text=True,
            stdout=subprocess.PIPE if capture or quiet else None,
            stderr=subprocess.STDOUT if capture or quiet else None,
            check=False,
        )
    except FileNotFoundError as exc:
        raise LocalWebConfigError(f"required tool not found: {command[0]}") from exc
    if result.returncode != 0:
        detail = (result.stdout or "").strip()
        if detail:
            print(detail)
        raise LocalWebConfigError(
            f"command failed with exit code {result.returncode}: {command[0]}"
        )
    return result


def _require_empty_state_directory(state_dir: Path) -> None:
    if state_dir.exists() and any(state_dir.iterdir()):
        raise LocalWebConfigError(
            f"local WebConfig state already exists: {state_dir}\n"
            "Use it as-is or move it aside explicitly before creating a new "
            "device identity. Automatic key replacement is intentionally disabled."
        )


def _generate_keypair(private_path: Path, public_path: Path) -> None:
    private_path.parent.mkdir(parents=True, exist_ok=True)
    _run(
        [
            "openssl",
            "genpkey",
            "-algorithm",
            "EC",
            "-pkeyopt",
            "ec_paramgen_curve:P-256",
            "-out",
            str(private_path),
        ]
    )
    try:
        private_path.chmod(0o600)
    except OSError:
        pass
    _run(
        [
            "openssl",
            "pkey",
            "-in",
            str(private_path),
            "-pubout",
            "-out",
            str(public_path),
        ]
    )


def _crc32_skipping(data: bytes, offset: int, size: int) -> int:
    import zlib

    crc = zlib.crc32(data[:offset]) & 0xFFFFFFFF
    return zlib.crc32(data[offset + size :], crc) & 0xFFFFFFFF


def build_security_version_record(minimum_version: int) -> bytes:
    """Build the first append-only security-version journal record."""

    if minimum_version < FIRMWARE_SECURITY_VERSION or minimum_version > 0xFFFFFFFF:
        raise LocalWebConfigError("invalid initial firmware security version")
    record = bytearray(SECURITY_VERSION_RECORD_BYTES)
    struct.pack_into(
        "<IHHIIIIII",
        record,
        0,
        SECURITY_VERSION_MAGIC,
        SECURITY_VERSION_FORMAT,
        SECURITY_VERSION_RECORD_BYTES,
        1,
        minimum_version,
        (~minimum_version) & 0xFFFFFFFF,
        0,
        0,
        SECURITY_VERSION_COMMITTED,
    )
    crc = _crc32_skipping(record, 24, 4)
    if crc == 0:
        raise LocalWebConfigError("security-version record CRC unexpectedly equals zero")
    struct.pack_into("<I", record, 24, crc)
    return bytes(record)


def build_internal_flash_provisioning_image(
    bootloader: bytes,
    identity_slot: bytes,
    security_version_record: bytes,
) -> bytes:
    """Combine one sector erase/program operation without overlapping regions."""

    if not bootloader or len(bootloader) > BOOTLOADER_LIMIT:
        raise LocalWebConfigError(
            f"bootloader must fit below 0x0801C000 (got {len(bootloader)} bytes)"
        )
    if len(identity_slot) != 288:
        raise LocalWebConfigError("identity slot must be exactly 288 bytes")
    if len(security_version_record) != SECURITY_VERSION_RECORD_BYTES:
        raise LocalWebConfigError("security-version record must be exactly 32 bytes")
    image = bytearray(b"\xFF" * INTERNAL_FLASH_BYTES)
    image[: len(bootloader)] = bootloader
    image[IDENTITY_OFFSET : IDENTITY_OFFSET + len(identity_slot)] = identity_slot
    image[
        SECURITY_VERSION_OFFSET :
        SECURITY_VERSION_OFFSET + len(security_version_record)
    ] = security_version_record
    return bytes(image)


def _state_paths(state_dir: Path) -> dict[str, Path]:
    return {
        "manifest": state_dir / "manifest.json",
        "manufacturer_private": state_dir / "pki" / "manufacturer-ca-private.pem",
        "manufacturer_public": state_dir / "pki" / "manufacturer-ca-public.pem",
        "firmware_private": state_dir / "pki" / "firmware-release-private.pem",
        "firmware_public": state_dir / "pki" / "firmware-release-public.pem",
        "authorization_private": state_dir / "pki" / "webconfig-auth-current-private.pem",
        "authorization_public": state_dir / "pki" / "webconfig-auth-current-public.pem",
        "authorization_next_private": state_dir / "pki" / "webconfig-auth-next-private.pem",
        "authorization_next_public": state_dir / "pki" / "webconfig-auth-next-public.pem",
        "trust_header": state_dir / "public" / "hbox-local-trust.h",
        "device_scalar": state_dir / "device" / "device-private-scalar.bin",
        "device_certificate": state_dir / "device" / "device-certificate.bin",
        "identity_slot": state_dir / "device" / "identity-slot.bin",
        "security_record": state_dir / "device" / "security-version-record.bin",
        "artifacts": state_dir / "artifacts",
    }


def _load_manifest(state_dir: Path) -> dict[str, Any]:
    manifest_path = _state_paths(state_dir)["manifest"]
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise LocalWebConfigError(
            f"local WebConfig state is not initialized: {manifest_path}"
        ) from exc
    if manifest.get("formatVersion") == 1:
        raise LocalWebConfigError(
            "local WebConfig identity predates the manufacturer-signed "
            "product ID; initialize a fresh state directory and rebuild"
        )
    if manifest.get("formatVersion") != LAB_MANIFEST_VERSION:
        raise LocalWebConfigError("unsupported local WebConfig manifest version")
    return manifest


def initialize_local_state(state_dir: Path) -> dict[str, Any]:
    _require_empty_state_directory(state_dir)
    paths = _state_paths(state_dir)
    state_dir.mkdir(parents=True, exist_ok=True)

    for prefix in (
        "manufacturer",
        "firmware",
        "authorization",
        "authorization_next",
    ):
        _generate_keypair(paths[f"{prefix}_private"], paths[f"{prefix}_public"])

    manufacturer_public = load_public_key(paths["manufacturer_public"])
    firmware_public = export_uncompressed_public_key(paths["firmware_private"])
    authorization_public = load_public_key(paths["authorization_public"])
    authorization_next_public = load_public_key(paths["authorization_next_public"])
    trust_header = render_trust_bundle_header(
        manufacturer_ca_public_key=manufacturer_public,
        firmware_release_public_key=firmware_public,
        authorization_current_public_key=authorization_public,
        authorization_next_public_key=authorization_next_public,
    )
    _atomic_write_text(paths["trust_header"], trust_header)

    scalar_value = secrets.randbelow(P256_ORDER - 1) + 1
    private_scalar = scalar_value.to_bytes(32, "big")
    device_public = _public_key_from_private_scalar(private_scalar)
    issued_at = int(time.time())
    batch = datetime.now(timezone.utc).strftime("LAB-%Y%m%d").encode("ascii")
    certificate_tbs = create_certificate_tbs(
        device_public,
        certificate_serial=secrets.token_bytes(16),
        product_id=encode_product_id("HBOX"),
        hardware_version=HARDWARE_VERSION,
        issued_at=issued_at,
        production_batch=batch,
        auth_level=DEVICE_AUTH_LEVEL_MCU_PROTECTED,
    )
    signature = sign_certificate_tbs_for_lab(
        certificate_tbs, paths["manufacturer_private"]
    )
    certificate = assemble_certificate(
        certificate_tbs, signature, paths["manufacturer_public"]
    )
    certificate_summary = verify_certificate(
        certificate, paths["manufacturer_public"]
    )
    identity_slot = build_development_identity_slot(
        private_scalar,
        certificate,
        paths["manufacturer_public"],
    )
    verify_development_identity_slot(
        identity_slot, paths["manufacturer_public"]
    )
    security_record = build_security_version_record(FIRMWARE_SECURITY_VERSION)

    _atomic_write(paths["device_scalar"], private_scalar, private=True)
    _atomic_write(paths["device_certificate"], certificate)
    _atomic_write(paths["identity_slot"], identity_slot, private=True)
    _atomic_write(paths["security_record"], security_record)

    manifest = {
        "formatVersion": LAB_MANIFEST_VERSION,
        "createdAt": datetime.now(timezone.utc).isoformat(),
        "purpose": "production-protocol-local-laboratory",
        "deviceId": certificate_summary["deviceId"],
        "certificateFingerprint": certificate_summary["certificateFingerprint"],
        "productId": certificate_summary["productId"],
        "pcbRevision": certificate_summary["pcbRevision"],
        "hardwareVersion": certificate_summary["hardwareVersion"],
        "securityVersion": FIRMWARE_SECURITY_VERSION,
        "trustHeaderSha256": _sha256(paths["trust_header"]),
        "warnings": [
            "Local PEM keys are development-only and are not production HSM keys.",
            "The development identity scalar was exported on the host.",
            "Never deploy this state directory or its artifacts to production devices.",
        ],
    }
    _atomic_write_text(
        paths["manifest"], json.dumps(manifest, indent=2) + "\n", private=True
    )
    print(f"Initialized local WebConfig V2 state: {state_dir}")
    print(f"Device ID: {manifest['deviceId']}")
    print(f"Public trust header: {paths['trust_header']}")
    return manifest


def _validate_revision_id(value: str) -> int:
    try:
        revision = int(value, 0)
    except ValueError as exc:
        raise LocalWebConfigError(f"invalid STM32 REV_ID: {value}") from exc
    if revision <= 0 or revision > 0xFFFF:
        raise LocalWebConfigError("STM32 REV_ID must be a nonzero 16-bit value")
    return revision


def _silicon_revision_make_arguments(revision_id: int | None) -> list[str]:
    """Return explicit bootloader flags for the optional silicon gate.

    The normal local WebConfig build does not qualify an exact die revision.
    Supplying a revision is an opt-in compatibility/errata control and never
    contributes to HBox device identity.
    """

    if revision_id is None:
        return ["HBOX_STM32H750_REVISION_QUALIFICATION=0"]
    if revision_id <= 0 or revision_id > 0xFFFF:
        raise LocalWebConfigError("STM32 REV_ID must be a nonzero 16-bit value")
    return [
        "HBOX_STM32H750_REVISION_QUALIFICATION=1",
        f"HBOX_STM32H750_REVISION_ID=0x{revision_id:04X}",
    ]


def _silicon_revision_manifest(revision_id: int | None) -> dict[str, Any]:
    return {
        "enabled": revision_id is not None,
        "stm32RevisionId": (
            f"0x{revision_id:04X}" if revision_id is not None else None
        ),
    }


def _required_lifecycle(revision_id: int | None) -> list[str]:
    requirements = ["DEV_ID=0x450 target-platform check"]
    if revision_id is not None:
        requirements.append(
            "REV_ID exactly matches optional qualified value "
            f"0x{revision_id:04X}"
        )
    requirements.extend(
        [
            "RDP Level 1",
            "SECURITY/Secure mode enabled",
            "SCAR covers the complete 128 KiB internal user Flash",
        ]
    )
    return requirements


def _copy_artifact(source: Path, destination: Path, *, private: bool = False) -> None:
    if not source.is_file():
        raise LocalWebConfigError(f"expected build artifact is missing: {source}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)
    if private:
        try:
            destination.chmod(0o600)
        except OSError:
            pass


def _validate_system_assets(path: Path) -> None:
    """Validate the HIMG index and every referenced payload.

    These resources are outside signed boot metadata, so the local artifact
    handoff must at least prove that the exact hashed file is structurally
    safe for the screen reader and fits its dedicated QSPI partition.
    """

    try:
        data = path.read_bytes()
    except OSError as exc:
        raise LocalWebConfigError("system image resources are missing") from exc
    if len(data) < SYSTEM_ASSETS_HEADER_BYTES:
        raise LocalWebConfigError("system image resources header is truncated")
    if len(data) > SYS_IMAGE_RESOURCES_SIZE:
        raise LocalWebConfigError("system image resources exceed their QSPI partition")
    magic, version, _flags, total_size, count, index_size, _reserved = (
        struct.unpack_from("<4sHHIIII", data, 0)
    )
    if magic != SYSTEM_ASSETS_MAGIC or version != SYSTEM_ASSETS_VERSION:
        raise LocalWebConfigError("system image resources have an invalid HIMG header")
    if total_size != len(data):
        raise LocalWebConfigError("system image resources total size is invalid")
    if count == 0:
        raise LocalWebConfigError("system image resources contain no icons")
    if index_size < count * SYSTEM_ASSETS_ENTRY_BYTES or index_size % 16 != 0:
        raise LocalWebConfigError("system image resources index size is invalid")
    payload_start = SYSTEM_ASSETS_HEADER_BYTES + index_size
    if payload_start > len(data):
        raise LocalWebConfigError("system image resources index exceeds the file")

    names: set[bytes] = set()
    for index in range(count):
        entry_offset = SYSTEM_ASSETS_HEADER_BYTES + index * SYSTEM_ASSETS_ENTRY_BYTES
        if entry_offset + SYSTEM_ASSETS_ENTRY_BYTES > payload_start:
            raise LocalWebConfigError("system image resources index is truncated")
        (
            encoded_name,
            image_type,
            _entry_flags,
            _entry_reserved,
            payload_offset,
            payload_size,
            width,
            height,
            expected_crc,
        ) = struct.unpack_from("<32sBBHIIHHI", data, entry_offset)
        name = encoded_name.partition(b"\0")[0]
        if not name or name in names:
            raise LocalWebConfigError("system image resources contain invalid names")
        names.add(name)
        if image_type not in (1, 2) or width == 0 or height == 0:
            raise LocalWebConfigError("system image resources contain an invalid entry")
        payload_end = payload_offset + payload_size
        if (
            payload_size == 0
            or payload_offset < payload_start
            or payload_end < payload_offset
            or payload_end > len(data)
        ):
            raise LocalWebConfigError("system image resource payload is out of bounds")
        actual_crc = binascii.crc32(data[payload_offset:payload_end]) & 0xFFFFFFFF
        if actual_crc != expected_crc:
            raise LocalWebConfigError("system image resource payload CRC32 is invalid")


def _validate_system_background(path: Path) -> None:
    """Validate the fixed-canvas UIMG v2 system-background container."""

    try:
        data = path.read_bytes()
    except OSError as exc:
        raise LocalWebConfigError("system background resource is missing") from exc
    if len(data) < SYSTEM_BACKGROUND_HEADER_BYTES:
        raise LocalWebConfigError("system background header is truncated")
    if len(data) > USER_IMAGE_RESOURCES_SIZE:
        raise LocalWebConfigError("system background exceeds the user-image partition")
    (
        magic,
        version,
        valid,
        image_format,
        width,
        height,
        frame_count,
        fps,
        reserved,
        frame_size,
        frames_offset,
        payload_size,
    ) = struct.unpack_from("<IHBBHHBBHIII", data, 0)
    if magic != SYSTEM_BACKGROUND_MAGIC or version != SYSTEM_BACKGROUND_VERSION:
        raise LocalWebConfigError("system background has an invalid UIMG header")
    if valid != 1 or reserved != 0:
        raise LocalWebConfigError("system background is not a canonical valid image")
    if width != SYSTEM_BACKGROUND_WIDTH or height != SYSTEM_BACKGROUND_HEIGHT:
        raise LocalWebConfigError("system background canvas size is invalid")
    if frame_count < 1 or frame_count > SYSTEM_BACKGROUND_MAX_FRAMES:
        raise LocalWebConfigError("system background frame count is invalid")
    expected_format = 1 if frame_count == 1 else 2
    if image_format != expected_format:
        raise LocalWebConfigError("system background image format is invalid")
    if (frame_count == 1 and fps != 0) or (frame_count > 1 and not 1 <= fps <= 5):
        raise LocalWebConfigError("system background frame rate is invalid")
    if (
        frame_size != SYSTEM_BACKGROUND_FRAME_BYTES
        or frames_offset != SYSTEM_BACKGROUND_HEADER_BYTES
        or payload_size != frame_count * frame_size
        or len(data) != frames_offset + payload_size
    ):
        raise LocalWebConfigError("system background payload layout is invalid")
    frame_offsets = struct.unpack_from("<10I", data, 28)
    for index, offset in enumerate(frame_offsets):
        expected = (
            frames_offset + index * frame_size if index < frame_count else 0
        )
        if offset != expected:
            raise LocalWebConfigError("system background frame index is invalid")


def _validate_local_image_resources(artifacts: Path) -> None:
    _validate_system_assets(artifacts / SYSTEM_ASSETS_FILENAME)
    _validate_system_background(artifacts / SYSTEM_BACKGROUND_FILENAME)


def build_local_image_resources(artifacts: Path) -> tuple[Path, Path]:
    """Build both screen resource partitions directly into the handoff bundle."""

    packer = TOOLS_DIR / "pack_assets.py"
    icons_dir = PROJECT_ROOT / "application" / "assets" / "sysicons"
    background_dir = PROJECT_ROOT / "application" / "assets" / "sysbg"
    if not packer.is_file():
        raise LocalWebConfigError(f"asset packer is missing: {packer}")
    if not icons_dir.is_dir():
        raise LocalWebConfigError(f"system icon sources are missing: {icons_dir}")
    if not background_dir.is_dir():
        raise LocalWebConfigError(
            f"system background sources are missing: {background_dir}"
        )
    artifacts.mkdir(parents=True, exist_ok=True)
    system_assets = artifacts / SYSTEM_ASSETS_FILENAME
    system_background = artifacts / SYSTEM_BACKGROUND_FILENAME
    _run(
        [
            sys.executable,
            str(packer),
            "--icons-dir",
            str(icons_dir),
            "--icons-output",
            str(system_assets),
            "--icons-max-size",
            hex(SYS_IMAGE_RESOURCES_SIZE),
        ]
    )
    _run(
        [
            sys.executable,
            str(packer),
            "--sysbg-dir",
            str(background_dir),
            "--sysbg-output",
            str(system_background),
            "--sysbg-max-size",
            hex(USER_IMAGE_RESOURCES_SIZE),
        ]
    )
    _validate_local_image_resources(artifacts)
    return system_assets, system_background


def normalize_target_slot(slot: str) -> str:
    normalized = str(slot).strip().upper()
    if normalized not in {"A", "B"}:
        raise LocalWebConfigError("target slot must be A or B")
    return normalized


def artifact_target_slot(manifest: dict[str, Any]) -> str:
    """Read the signed-artifact target slot, accepting old Slot A manifests."""

    return normalize_target_slot(str(manifest.get("targetSlot", "A")))


def _slot_artifact_names(slot: str) -> tuple[str, str]:
    suffix = normalize_target_slot(slot).lower()
    return f"application-slot-{suffix}.bin", f"adc-mapping-slot-{suffix}.bin"


def _slot_addresses(slot: str) -> tuple[int, int, int]:
    if normalize_target_slot(slot) == "A":
        return (
            SLOT_A_APPLICATION_ADDR,
            SLOT_A_WEBRESOURCES_ADDR,
            SLOT_A_ADC_MAPPING_ADDR,
        )
    return (
        SLOT_B_APPLICATION_ADDR,
        SLOT_B_WEBRESOURCES_ADDR,
        SLOT_B_ADC_MAPPING_ADDR,
    )


def build_local_metadata_components(
    application_source: Path,
    adc_mapping_source: Path,
    slot: str = "A",
) -> list[dict[str, Any]]:
    """Return the exact three-component production boot contract for a slot."""

    if not application_source.is_file() or application_source.stat().st_size == 0:
        raise LocalWebConfigError("application artifact is missing or empty")
    if not adc_mapping_source.is_file() or adc_mapping_source.stat().st_size == 0:
        raise LocalWebConfigError("ADC mapping artifact is missing or empty")
    application_address, webresources_address, adc_mapping_address = (
        _slot_addresses(slot)
    )
    application_name, adc_mapping_name = _slot_artifact_names(slot)
    return [
        {
            "name": "application",
            "file": application_name,
            "address": application_address,
            "size": application_source.stat().st_size,
            "sha256": _sha256(application_source),
            "active": True,
        },
        {
            "name": "webresources",
            "file": "",
            "address": webresources_address,
            "size": 0,
            "sha256": "",
            "active": False,
        },
        {
            "name": "adc_mapping",
            "file": adc_mapping_name,
            "address": adc_mapping_address,
            "size": adc_mapping_source.stat().st_size,
            "sha256": _sha256(adc_mapping_source),
            "active": True,
        },
    ]


def _decode_metadata_text(
    metadata: bytes,
    offset: int,
    width: int,
    field_name: str,
) -> str:
    encoded = metadata[offset : offset + width]
    content, separator, padding = encoded.partition(b"\0")
    if separator and any(padding):
        raise LocalWebConfigError(
            f"metadata field {field_name} is not canonically zero-padded"
        )
    try:
        return content.decode("utf-8", errors="strict")
    except UnicodeDecodeError as exc:
        raise LocalWebConfigError(
            f"metadata field {field_name} is not UTF-8"
        ) from exc


def load_verified_artifact_manifest(state_dir: Path) -> dict[str, Any]:
    """Verify every local artifact before enrollment or hardware handoff."""

    state_manifest = _load_manifest(state_dir)
    paths = _state_paths(state_dir)
    manifest_path = paths["artifacts"] / "artifact-manifest.json"
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise LocalWebConfigError(
            "verified firmware artifact manifest is missing; run local-build first"
        ) from exc
    if manifest.get("formatVersion") != ARTIFACT_MANIFEST_VERSION:
        raise LocalWebConfigError("unsupported artifact manifest version")
    if manifest.get("deviceId") != state_manifest.get("deviceId"):
        raise LocalWebConfigError("artifact device identity does not match local state")
    if manifest.get("productId") != state_manifest.get("productId"):
        raise LocalWebConfigError("artifact product ID does not match local state")
    if manifest.get("pcbRevision") != state_manifest.get("pcbRevision"):
        raise LocalWebConfigError("artifact PCB revision does not match local state")
    if manifest.get("trustHeaderSha256") != _sha256(paths["trust_header"]):
        raise LocalWebConfigError("artifact trust header hash does not match local state")

    target_slot = artifact_target_slot(manifest)
    application_name, adc_mapping_name = _slot_artifact_names(target_slot)
    application_address, _webresources_address, adc_mapping_address = (
        _slot_addresses(target_slot)
    )
    required_files = {
        "bootloader.bin",
        application_name,
        adc_mapping_name,
        "metadata.bin",
        "ch585-maintenance.bin",
        "device-certificate.bin",
        "internal-flash-provisioning.bin",
        SYSTEM_ASSETS_FILENAME,
        SYSTEM_BACKGROUND_FILENAME,
    }
    file_records = manifest.get("files")
    if not isinstance(file_records, dict) or set(file_records) != required_files:
        raise LocalWebConfigError("artifact manifest file set is incomplete")
    for name, record in file_records.items():
        if Path(name).name != name or not isinstance(record, dict):
            raise LocalWebConfigError("artifact manifest contains an unsafe filename")
        artifact = paths["artifacts"] / name
        if not artifact.is_file():
            raise LocalWebConfigError(f"artifact is missing: {name}")
        if record.get("bytes") != artifact.stat().st_size:
            raise LocalWebConfigError(f"artifact size mismatch: {name}")
        if record.get("sha256") != _sha256(artifact):
            raise LocalWebConfigError(f"artifact SHA-256 mismatch: {name}")

    expected_addresses = {
        "internalFlashProvisioning": "0x08000000",
        "application": f"0x{application_address:08X}",
        "adcMapping": f"0x{adc_mapping_address:08X}",
        "systemImageResources": f"0x{SYS_IMAGE_RESOURCES_ADDR:08X}",
        "systemBackground": f"0x{USER_IMAGE_RESOURCES_ADDR:08X}",
        "metadata": "0x90570000",
    }
    if manifest.get("addresses") != expected_addresses:
        raise LocalWebConfigError("artifact target address contract is invalid")
    _validate_local_image_resources(paths["artifacts"])

    metadata_path = paths["artifacts"] / "metadata.bin"
    metadata = metadata_path.read_bytes()
    if len(metadata) != METADATA_STRUCT_SIZE:
        raise LocalWebConfigError("metadata size is invalid")
    stored_crc = struct.unpack_from("<I", metadata, METADATA_CRC32_OFFSET)[0]
    if stored_crc == 0 or stored_crc != _crc32_skipping(
        metadata,
        METADATA_CRC32_OFFSET,
        4,
    ):
        raise LocalWebConfigError("metadata CRC32 is invalid")
    measurement = metadata[
        FIRMWARE_HASH_OFFSET : FIRMWARE_HASH_OFFSET + 32
    ]
    if measurement != hashlib.sha256(canonical_metadata(metadata)).digest():
        raise LocalWebConfigError("metadata firmware measurement is invalid")
    if manifest.get("firmwareMeasurement") != measurement.hex():
        raise LocalWebConfigError(
            "artifact manifest firmware measurement does not match metadata"
        )
    try:
        verify_signed_metadata(
            metadata,
            load_public_key(paths["firmware_public"]),
        )
    except (OSError, RuntimeError, ValueError) as exc:
        raise LocalWebConfigError("metadata signature verification failed") from exc

    if struct.unpack_from("<I", metadata, 129)[0] != FIRMWARE_COMPONENT_COUNT:
        raise LocalWebConfigError("metadata must contain all three boot components")
    expected_components = build_local_metadata_components(
        paths["artifacts"] / application_name,
        paths["artifacts"] / adc_mapping_name,
        target_slot,
    )
    component_offset = 133
    component_bytes = 170
    for index, expected in enumerate(expected_components):
        base = component_offset + index * component_bytes
        actual = {
            "name": _decode_metadata_text(metadata, base, 32, "component.name"),
            "file": _decode_metadata_text(metadata, base + 32, 64, "component.file"),
            "address": struct.unpack_from("<I", metadata, base + 96)[0],
            "size": struct.unpack_from("<I", metadata, base + 100)[0],
            "sha256": _decode_metadata_text(
                metadata,
                base + 104,
                65,
                "component.sha256",
            ),
            "active": metadata[base + 169] == 1,
        }
        if actual != expected:
            raise LocalWebConfigError(
                f"metadata component contract mismatch: {expected['name']}"
            )

    identity_slot = paths["identity_slot"].read_bytes()
    try:
        identity_summary = verify_development_identity_slot(
            identity_slot,
            paths["manufacturer_public"],
        )
    except (OSError, RuntimeError, ValueError) as exc:
        raise LocalWebConfigError("local device identity slot is invalid") from exc
    if identity_summary["deviceId"] != state_manifest["deviceId"]:
        raise LocalWebConfigError("identity slot device ID does not match local state")
    if identity_summary["productId"] != state_manifest["productId"]:
        raise LocalWebConfigError("identity slot product ID does not match local state")
    if identity_summary["pcbRevision"] != state_manifest["pcbRevision"]:
        raise LocalWebConfigError("identity slot PCB revision does not match local state")

    internal_image = (
        paths["artifacts"] / "internal-flash-provisioning.bin"
    ).read_bytes()
    if len(internal_image) != INTERNAL_FLASH_BYTES:
        raise LocalWebConfigError("internal Flash provisioning image size is invalid")
    bootloader = (paths["artifacts"] / "bootloader.bin").read_bytes()
    if internal_image[: len(bootloader)] != bootloader or any(
        value != 0xFF
        for value in internal_image[len(bootloader) : IDENTITY_OFFSET]
    ):
        raise LocalWebConfigError("internal Flash image bootloader region mismatch")
    if internal_image[
        IDENTITY_OFFSET : IDENTITY_OFFSET + len(identity_slot)
    ] != identity_slot:
        raise LocalWebConfigError("internal Flash image identity slot mismatch")
    if any(
        value != 0xFF
        for value in internal_image[
            IDENTITY_OFFSET + len(identity_slot) : SECURITY_VERSION_OFFSET
        ]
    ):
        raise LocalWebConfigError("internal Flash identity reserve is not erased")
    security_record = paths["security_record"].read_bytes()
    if len(security_record) != SECURITY_VERSION_RECORD_BYTES:
        raise LocalWebConfigError("security-version record size is invalid")
    stored_security_crc = struct.unpack_from("<I", security_record, 24)[0]
    if stored_security_crc == 0 or stored_security_crc != _crc32_skipping(
        security_record,
        24,
        4,
    ):
        raise LocalWebConfigError("security-version record CRC32 is invalid")
    if internal_image[
        SECURITY_VERSION_OFFSET : SECURITY_VERSION_OFFSET + len(security_record)
    ] != security_record:
        raise LocalWebConfigError("internal Flash image security journal mismatch")
    if any(
        value != 0xFF
        for value in internal_image[
            SECURITY_VERSION_OFFSET + len(security_record) :
        ]
    ):
        raise LocalWebConfigError("internal Flash security journal tail is not erased")
    return manifest


def build_local_artifacts(
    state_dir: Path,
    revision_id: int | None = None,
    *,
    jobs: int,
    skip_web: bool = False,
    slot: str = "A",
    unlocked_development: bool = False,
    skip_power_device_probes: bool = False,
) -> dict[str, Any]:
    slot = normalize_target_slot(slot)
    manifest = _load_manifest(state_dir)
    paths = _state_paths(state_dir)
    trust_header = paths["trust_header"].resolve()
    revision = f"0x{revision_id:04X}" if revision_id is not None else None
    # The legacy GnuWin32 make 3.81 commonly found first on Windows can return
    # before all -j children have produced their artifacts. Prefer the current
    # MinGW entry point there; POSIX environments continue to use `make`.
    make = (
        (shutil.which("mingw32-make") if os.name == "nt" else None)
        or shutil.which("make")
        or "make"
    )
    # On Windows npm is normally a .cmd shim. Passing the bare name to
    # subprocess.run(list) does not consistently resolve PATHEXT, so pin the
    # discovered executable just as we already do for make.
    npm = shutil.which("npm") or shutil.which("npm.cmd") or "npm"
    secure_boot_argument = (
        "HBOX_SECURE_BOOT_REQUIRED=0"
        if unlocked_development
        else "HBOX_SECURE_BOOT_REQUIRED=1"
    )
    power_probe_argument = (
        "POWER_DEVICE_PROBE_ENABLED=0"
        if skip_power_device_probes
        else "POWER_DEVICE_PROBE_ENABLED=1"
    )

    _run([make, "clean"], cwd=PROJECT_ROOT / "bootloader")
    _run(
        [
            make,
            f"-j{jobs}",
            f"HBOX_TRUST_HEADER={trust_header.as_posix()}",
            secure_boot_argument,
            *_silicon_revision_make_arguments(revision_id),
            "HBOX_SECURITY_VERSION_INTERNAL_FLASH_PROVIDER=1",
            "HBOX_DEVICE_IDENTITY_INTERNAL_FLASH_PROVIDER=1",
        ],
        cwd=PROJECT_ROOT / "bootloader",
    )
    slot_builder = BuildTool()
    linker_backup = slot_builder.modify_linker_script_for_slot(slot)
    if linker_backup is None:
        raise LocalWebConfigError(f"failed to prepare the Slot {slot} linker script")
    try:
        _run([make, "clean"], cwd=PROJECT_ROOT / "application")
        _run(
            [
                make,
                f"-j{jobs}",
                f"HBOX_TRUST_HEADER={trust_header.as_posix()}",
                secure_boot_argument,
                power_probe_argument,
                "APP_LOG_ENABLE=1",
            ],
            cwd=PROJECT_ROOT / "application",
        )
    finally:
        slot_builder.restore_file(linker_backup)
    _run([make, f"-j{jobs}"], cwd=PROJECT_ROOT / "RF_PHY_Hop" / "TX")
    if not skip_web:
        _run([npm, "run", "build:hosted"], cwd=PROJECT_ROOT / "application" / "www")

    artifacts = paths["artifacts"]
    artifacts.mkdir(parents=True, exist_ok=True)
    bootloader_source = PROJECT_ROOT / "bootloader" / "build" / "bootloader.bin"
    application_source = PROJECT_ROOT / "application" / "build" / "application.bin"
    adc_mapping_source = PROJECT_ROOT / "resources" / "slot_a_adc_mapping.bin"
    ch585_source = (
        PROJECT_ROOT
        / "RF_PHY_Hop"
        / "TX"
        / "build_tx"
        / "RF_PHY_Hop_TX.bin"
    )
    _copy_artifact(bootloader_source, artifacts / "bootloader.bin")
    application_name, adc_mapping_name = _slot_artifact_names(slot)
    _copy_artifact(application_source, artifacts / application_name)
    _copy_artifact(adc_mapping_source, artifacts / adc_mapping_name)
    _copy_artifact(ch585_source, artifacts / "ch585-maintenance.bin")
    _copy_artifact(paths["device_certificate"], artifacts / "device-certificate.bin")
    build_local_image_resources(artifacts)

    metadata_components = build_local_metadata_components(
        application_source,
        adc_mapping_source,
        slot,
    )
    metadata = create_metadata_binary(
        version="2.0.0",
        slot=slot,
        build_date=datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M:%S"),
        components=metadata_components,
        signing_key=paths["firmware_private"],
        security_version=FIRMWARE_SECURITY_VERSION,
        webresources_optional=True,
    )
    _atomic_write(artifacts / "metadata.bin", metadata)

    internal_image = build_internal_flash_provisioning_image(
        bootloader_source.read_bytes(),
        paths["identity_slot"].read_bytes(),
        paths["security_record"].read_bytes(),
    )
    _atomic_write(
        artifacts / "internal-flash-provisioning.bin",
        internal_image,
        private=True,
    )

    artifact_files = [
        "bootloader.bin",
        application_name,
        adc_mapping_name,
        "metadata.bin",
        "ch585-maintenance.bin",
        "device-certificate.bin",
        "internal-flash-provisioning.bin",
        SYSTEM_ASSETS_FILENAME,
        SYSTEM_BACKGROUND_FILENAME,
    ]
    artifact_manifest = {
        "formatVersion": ARTIFACT_MANIFEST_VERSION,
        "createdAt": datetime.now(timezone.utc).isoformat(),
        "deviceId": manifest["deviceId"],
        "productId": manifest["productId"],
        "pcbRevision": manifest["pcbRevision"],
        "hardwareVersion": manifest["hardwareVersion"],
        "targetSlot": slot,
        "siliconRevisionQualification": _silicon_revision_manifest(revision_id),
        "securityVersion": FIRMWARE_SECURITY_VERSION,
        "bootSecurityMode": (
            "unlocked-development"
            if unlocked_development
            else "secure-production"
        ),
        "powerDeviceProbeMode": (
            "disabled-for-board-bringup"
            if skip_power_device_probes
            else "enabled"
        ),
        "firmwareMeasurement": metadata[
            FIRMWARE_HASH_OFFSET : FIRMWARE_HASH_OFFSET + 32
        ].hex(),
        "trustHeaderSha256": _sha256(paths["trust_header"]),
        "addresses": {
            "internalFlashProvisioning": "0x08000000",
            "application": f"0x{_slot_addresses(slot)[0]:08X}",
            "adcMapping": f"0x{_slot_addresses(slot)[2]:08X}",
            "systemImageResources": f"0x{SYS_IMAGE_RESOURCES_ADDR:08X}",
            "systemBackground": f"0x{USER_IMAGE_RESOURCES_ADDR:08X}",
            "metadata": "0x90570000",
        },
        "files": {
            name: {
                "bytes": (artifacts / name).stat().st_size,
                "sha256": _sha256(artifacts / name),
            }
            for name in artifact_files
        },
        "requiresManualLifecycleProvisioning": not unlocked_development,
        "requiredLifecycle": (
            []
            if unlocked_development
            else _required_lifecycle(revision_id)
        ),
    }
    _atomic_write_text(
        artifacts / "artifact-manifest.json",
        json.dumps(artifact_manifest, indent=2) + "\n",
        private=True,
    )
    load_verified_artifact_manifest(state_dir)
    print(f"Local WebConfig artifacts are ready: {artifacts}")
    if revision is None:
        print("Exact STM32 silicon REV_ID qualification is disabled (default).")
    else:
        print(f"Exact STM32 silicon REV_ID qualification is enabled: {revision}")
    print("No hardware was flashed and no option bytes were changed.")
    if unlocked_development:
        print(
            "UNLOCKED DEVELOPMENT build: RDP, SECURITY, and SCAR are neither "
            "required nor programmed."
        )
    if skip_power_device_probes:
        print(
            "BOARD BRING-UP build: BQ25895/MAX17048 probes are disabled; "
            "charging remains off and telemetry reports offline."
        )
    return artifact_manifest


def _health_url(port: int) -> str:
    return f"http://localhost:{port}/health"


def _wait_for_server(process: subprocess.Popen[Any], port: int) -> None:
    deadline = time.monotonic() + 20.0
    last_error: Exception | None = None
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise LocalWebConfigError(
                f"local server exited with code {process.returncode}"
            )
        try:
            with urllib.request.urlopen(_health_url(port), timeout=1.0) as response:
                if response.status == 200:
                    return
        except (urllib.error.URLError, TimeoutError) as exc:
            last_error = exc
            time.sleep(0.2)
    raise LocalWebConfigError(f"local server did not become ready: {last_error}")


def _enroll_local_device(
    state_dir: Path,
    port: int,
    admin_username: str,
    admin_password: str,
) -> None:
    paths = _state_paths(state_dir)
    manifest = _load_manifest(state_dir)
    artifact_manifest = load_verified_artifact_manifest(state_dir)
    firmware_measurement = artifact_manifest["firmwareMeasurement"]
    if not re.fullmatch(r"[0-9a-f]{64}", str(firmware_measurement)):
        raise LocalWebConfigError("artifact firmware measurement is invalid")
    certificate = base64.b64encode(paths["device_certificate"].read_bytes()).decode(
        "ascii"
    )
    payload = json.dumps(
        {
            "deviceCertificate": certificate,
            "deviceName": "HBox Local WebConfig",
            "minSecurityVersion": FIRMWARE_SECURITY_VERSION,
            "allowedFirmwareMeasurements": [firmware_measurement],
        }
    ).encode("utf-8")
    credentials = base64.b64encode(
        f"{admin_username}:{admin_password}".encode("utf-8")
    ).decode("ascii")
    request = urllib.request.Request(
        f"http://localhost:{port}/api/v2/devices",
        data=payload,
        method="POST",
        headers={
            "Authorization": f"Basic {credentials}",
            "Content-Type": "application/json",
        },
    )
    try:
        with urllib.request.urlopen(request, timeout=5.0) as response:
            if response.status not in (200, 201):
                raise LocalWebConfigError(
                    f"unexpected enrollment response: HTTP {response.status}"
                )
    except urllib.error.HTTPError as exc:
        detail = exc.read().decode("utf-8", errors="replace")
        raise LocalWebConfigError(
            f"device enrollment failed: HTTP {exc.code}: {detail}"
        ) from exc

    policy_payload = json.dumps(
        {
            "minSecurityVersion": FIRMWARE_SECURITY_VERSION,
            "allowedFirmwareMeasurements": [firmware_measurement],
        }
    ).encode("utf-8")
    policy_request = urllib.request.Request(
        f"http://localhost:{port}/api/v2/devices/{manifest['deviceId']}/policy",
        data=policy_payload,
        method="PUT",
        headers={
            "Authorization": f"Basic {credentials}",
            "Content-Type": "application/json",
        },
    )
    try:
        with urllib.request.urlopen(policy_request, timeout=5.0) as response:
            if response.status != 200:
                raise LocalWebConfigError(
                    f"unexpected policy response: HTTP {response.status}"
                )
    except urllib.error.HTTPError as exc:
        detail = exc.read().decode("utf-8", errors="replace")
        raise LocalWebConfigError(
            f"device policy update failed: HTTP {exc.code}: {detail}"
        ) from exc
    print(
        "Local device certificate and exact firmware measurement are enrolled."
    )


def serve_local_webconfig(
    state_dir: Path,
    *,
    port: int,
    admin_username: str,
    admin_password: str,
) -> int:
    _load_manifest(state_dir)
    # Refuse before spawning the server if any handoff artifact is missing or
    # no longer matches its signed metadata/manifest.  Enrollment must never
    # race ahead with a stale firmware measurement.
    load_verified_artifact_manifest(state_dir)
    paths = _state_paths(state_dir)
    static_dir = PROJECT_ROOT / "application" / "www" / "build"
    if not (static_dir / "index.html").is_file():
        raise LocalWebConfigError(
            "hosted WebConfig build is missing; run the local build command first"
        )
    environment = os.environ.copy()
    environment.update(
        {
            "NODE_ENV": "development",
            "PORT": str(port),
            "SERVER_PORT": str(port),
            "SERVER_ADDRESS": "127.0.0.1",
            "LISTEN_HOST": "127.0.0.1",
            "DOMAIN_NAME": "localhost",
            "TRUST_PROXY_HOPS": "0",
            "WEB_CONFIG_ORIGINS": f"http://localhost:{port}",
            "WEB_CONFIG_STATIC_DIR": str(static_dir.resolve()),
            "WEB_CONFIG_REQUIRE_STATIC": "1",
            "HBOX_SERVER_DATA_DIR": str((state_dir / "server-data").resolve()),
            "HBOX_SERVER_UPLOAD_DIR": str((state_dir / "server-uploads").resolve()),
            "DEVICE_CA_PUBLIC_KEY_FILE": str(paths["manufacturer_public"].resolve()),
            "FIRMWARE_RELEASE_PUBLIC_KEY_FILE": str(
                paths["firmware_public"].resolve()
            ),
            "WEB_CONFIG_AUTH_PRIVATE_KEY_FILE": str(
                paths["authorization_private"].resolve()
            ),
            "WEB_CONFIG_AUTH_KEY_SLOT": "0",
        }
    )
    process = subprocess.Popen(
        ["node", "src/server.js"],
        cwd=PROJECT_ROOT / "server",
        env=environment,
    )
    try:
        _wait_for_server(process, port)
        _enroll_local_device(
            state_dir, port, admin_username, admin_password
        )
        print()
        print(f"WebConfig is ready: http://localhost:{port}")
        print("Use Chrome or Edge, enter Web Config on the device screen, then click Connect.")
        print("Press Ctrl+C to stop the local server.")
        return process.wait()
    except KeyboardInterrupt:
        return 0
    finally:
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=5.0)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5.0)


def probe_revision(openocd: str) -> int:
    config = PROJECT_ROOT / "bootloader" / "Openocd_Script" / "ST-LINK-FLASH.cfg"
    result = _run(
        [
            openocd,
            "-d1",
            "-f",
            str(config),
            "-c",
            "init",
            "-c",
            "halt",
            "-c",
            "reset halt",
            "-c",
            "mdw 0x5C001000 1",
            "-c",
            "reset run",
            "-c",
            "shutdown",
        ],
        cwd=PROJECT_ROOT / "bootloader",
        capture=True,
    )
    match = re.search(
        r"0x5c001000:\s+([0-9a-f]{8})",
        result.stdout or "",
        flags=re.IGNORECASE,
    )
    if not match:
        raise LocalWebConfigError("OpenOCD did not return DBGMCU_IDCODE")
    idcode = int(match.group(1), 16)
    device_id = idcode & 0xFFF
    revision_id = (idcode >> 16) & 0xFFFF
    print(f"DBGMCU_IDCODE=0x{idcode:08X}")
    print(f"DEV_ID=0x{device_id:03X}")
    print(f"REV_ID=0x{revision_id:04X}")
    if device_id != 0x450:
        raise LocalWebConfigError("connected MCU is not the qualified STM32H750 target")
    return revision_id


def show_status(state_dir: Path) -> None:
    manifest = _load_manifest(state_dir)
    paths = _state_paths(state_dir)
    print(json.dumps(manifest, indent=2))
    print(f"State directory: {state_dir}")
    print(f"Trust header present: {paths['trust_header'].is_file()}")
    print(
        "Hosted page present: "
        + str((PROJECT_ROOT / "application" / "www" / "build" / "index.html").is_file())
    )
    try:
        load_verified_artifact_manifest(state_dir)
        artifact_status = "verified"
    except LocalWebConfigError:
        artifact_status = "not ready"
    print(f"Firmware artifacts: {artifact_status}")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Prepare production-protocol WebConfig V2 local laboratory artifacts"
    )
    parser.add_argument(
        "--state-dir",
        type=Path,
        default=DEFAULT_STATE_DIR,
        help="ignored directory for local keys and artifacts",
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    subparsers.add_parser("init", help="create an isolated local PKI and device identity")

    build = subparsers.add_parser(
        "build", help="build trust-injected STM32/CH585/static WebConfig artifacts"
    )
    build.add_argument(
        "--qualify-silicon-revision",
        "--revision-id",
        dest="qualified_revision_id",
        metavar="REV_ID",
        help=(
            "optional exact STM32 REV_ID compatibility gate; omitted by default "
            "and never used as product identity"
        ),
    )
    build.add_argument("--jobs", type=int, default=max(2, min(8, os.cpu_count() or 2)))
    build.add_argument("--skip-web", action="store_true")
    build.add_argument(
        "--unlocked-development",
        action="store_true",
        help=(
            "build a direct-handoff development image that neither requires "
            "nor programs RDP, SECURITY, or SCAR"
        ),
    )
    build.add_argument(
        "--skip-power-device-probes",
        action="store_true",
        help=(
            "keep charging disabled and skip BQ25895/MAX17048 traffic for "
            "initial PCB display/menu bring-up"
        ),
    )
    build.add_argument(
        "--slot",
        choices=("A", "B", "a", "b"),
        default="A",
        help="application slot to build and sign (default: A)",
    )

    serve = subparsers.add_parser(
        "serve", help="serve the static page and V2 API from one localhost origin"
    )
    serve.add_argument("--port", type=int, default=3000)
    serve.add_argument(
        "--admin-username",
        default=os.environ.get("HBOX_LOCAL_ADMIN_USERNAME", "admin"),
    )
    serve.add_argument(
        "--admin-password",
        default=os.environ.get("HBOX_LOCAL_ADMIN_PASSWORD", "admin123"),
        help="local server admin password (or HBOX_LOCAL_ADMIN_PASSWORD)",
    )

    probe = subparsers.add_parser(
        "probe-revision",
        help="optionally inspect DEV_ID/REV_ID through ST-Link (not required to build)",
    )
    probe.add_argument("--openocd", default="openocd")
    subparsers.add_parser("status", help="show non-secret local readiness state")
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    state_dir = args.state_dir.expanduser().resolve()
    try:
        if args.command == "init":
            initialize_local_state(state_dir)
            return 0
        if args.command == "build":
            if args.jobs <= 0 or args.jobs > 64:
                raise LocalWebConfigError("--jobs must be between 1 and 64")
            build_local_artifacts(
                state_dir,
                (
                    _validate_revision_id(args.qualified_revision_id)
                    if args.qualified_revision_id is not None
                    else None
                ),
                jobs=args.jobs,
                skip_web=args.skip_web,
                slot=args.slot,
                unlocked_development=args.unlocked_development,
                skip_power_device_probes=args.skip_power_device_probes,
            )
            return 0
        if args.command == "serve":
            if args.port <= 0 or args.port > 65535:
                raise LocalWebConfigError("--port must be between 1 and 65535")
            return serve_local_webconfig(
                state_dir,
                port=args.port,
                admin_username=args.admin_username,
                admin_password=args.admin_password,
            )
        if args.command == "probe-revision":
            probe_revision(args.openocd)
            return 0
        if args.command == "status":
            show_status(state_dir)
            return 0
    except (LocalWebConfigError, OSError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
