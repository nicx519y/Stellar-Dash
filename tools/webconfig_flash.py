#!/usr/bin/env python3
"""Fail-closed STM32 handoff for local WebConfig production-protocol artifacts.

The default mode is a dry run.  A hardware transaction requires ``--execute``
plus the exact manifest device ID, selected ST-Link serial, and user-confirmed
STM32 UID.  It snapshots artifacts into a token-authenticated durable
transaction, backs up the complete 128 KiB internal Flash sector, and refuses
to replace a different provisioned identity.  An interrupted internal-sector
write is retried only from the authenticated original, a fully erased sector,
or an already complete desired image.  Signed metadata is the final commit.

This tool deliberately has no STM32 option-byte operation.  RDP/Secure/SCAR
lifecycle provisioning remains a separate, reviewed factory procedure.
"""

from __future__ import annotations

import argparse
import hashlib
import hmac
import json
import os
import re
import secrets
import shutil
import sys
import tempfile
from contextlib import contextmanager
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Sequence


TOOLS_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = TOOLS_DIR.parent
sys.path.insert(0, str(TOOLS_DIR))
sys.path.insert(0, str(PROJECT_ROOT / "common"))

from build import BuildTool  # noqa: E402
from firmware_metadata import (  # noqa: E402
    EXTERNAL_FLASH_BASE,
    EXTERNAL_FLASH_SIZE,
    METADATA_ADDR,
    METADATA_SIZE,
    METADATA_STRUCT_SIZE,
    SLOT_A_ADC_MAPPING_ADDR,
    SLOT_A_ADC_MAPPING_SIZE,
    SLOT_A_APPLICATION_ADDR,
    SLOT_A_APPLICATION_SIZE,
    SLOT_B_ADC_MAPPING_ADDR,
    SLOT_B_ADC_MAPPING_SIZE,
    SLOT_B_APPLICATION_ADDR,
    SLOT_B_APPLICATION_SIZE,
    SYS_IMAGE_RESOURCES_ADDR,
    SYS_IMAGE_RESOURCES_SIZE,
    USER_IMAGE_RESOURCES_ADDR,
    USER_IMAGE_RESOURCES_SIZE,
)
import webconfig_local as local  # noqa: E402


INTERNAL_FLASH_ADDRESS = 0x08000000
INTERNAL_FLASH_BYTES = 0x00020000
INTERNAL_SECURITY_TAIL_OFFSET = 0x0001C000
STM32_UID_ADDRESS = 0x1FF1E800
QSPI_SECTOR_BYTES = 0x00010000
MINIMUM_OPENOCD_VERSION = (0, 11)
AUTOMATIC_STLINK_BINDING = "AUTO"
DEFAULT_OPENOCD_CANDIDATES = (
    Path(
        r"D:\MounRiverStudio2\MounRiver_Studio2\resources\app\resources\win32"
        r"\components\WCH\OpenOCD\OpenOCD\bin\openocd.exe"
    ),
    Path(r"D:\Program Files\msys64\mingw64\bin\openocd.exe"),
    Path(r"C:\msys64\mingw64\bin\openocd.exe"),
)
TRANSACTION_FORMAT_VERSION = 2
MAX_INTERNAL_ATTEMPTS = 3
MATCHING_SECURITY_TAIL_FALSE_POSITIVE_ERROR = (
    "authenticated original contains an existing identity/security tail that "
    "differs from the desired image; refusing replacement"
)
ACTIVE_TRANSACTION_STATUSES = {
    "staged",
    "preflight-complete",
    "payloads-programming",
    "payloads-verified",
    "internal-programming",
    "internal-verified",
    "metadata-programming",
}
BLOCKING_TRANSACTION_STATUSES = ACTIVE_TRANSACTION_STATUSES | {
    "manual-recovery-required"
}


@dataclass(frozen=True)
class FlashStage:
    """One immutable write in the local STM32 provisioning transaction."""

    name: str
    filename: str
    address_key: str
    address: int
    maximum_bytes: int
    exact_bytes: int | None = None
    qspi: bool = True
    reset_after: bool = False


@dataclass(frozen=True)
class TargetIdentity:
    """Hardware identity observed through the selected or automatic ST-Link."""

    stlink_serial: str
    dbgmcu_idcode: int
    device_id: int
    revision_id: int
    uid: str


def flash_stages_for_slot(slot: str) -> tuple[FlashStage, ...]:
    normalized = local.normalize_target_slot(slot)
    suffix = normalized.lower()
    if normalized == "A":
        application_address = SLOT_A_APPLICATION_ADDR
        application_size = SLOT_A_APPLICATION_SIZE
        adc_mapping_address = SLOT_A_ADC_MAPPING_ADDR
        adc_mapping_size = SLOT_A_ADC_MAPPING_SIZE
    else:
        application_address = SLOT_B_APPLICATION_ADDR
        application_size = SLOT_B_APPLICATION_SIZE
        adc_mapping_address = SLOT_B_ADC_MAPPING_ADDR
        adc_mapping_size = SLOT_B_ADC_MAPPING_SIZE
    return (
    FlashStage(
        f"Slot {normalized} application",
        f"application-slot-{suffix}.bin",
        "application",
        application_address,
        application_size,
    ),
    FlashStage(
        f"Slot {normalized} ADC mapping",
        f"adc-mapping-slot-{suffix}.bin",
        "adcMapping",
        adc_mapping_address,
        adc_mapping_size,
    ),
    FlashStage(
        "system image resources",
        local.SYSTEM_ASSETS_FILENAME,
        "systemImageResources",
        SYS_IMAGE_RESOURCES_ADDR,
        SYS_IMAGE_RESOURCES_SIZE,
    ),
    FlashStage(
        "system background",
        local.SYSTEM_BACKGROUND_FILENAME,
        "systemBackground",
        USER_IMAGE_RESOURCES_ADDR,
        USER_IMAGE_RESOURCES_SIZE,
    ),
    FlashStage(
        "internal Flash provisioning",
        "internal-flash-provisioning.bin",
        "internalFlashProvisioning",
        INTERNAL_FLASH_ADDRESS,
        INTERNAL_FLASH_BYTES,
        exact_bytes=INTERNAL_FLASH_BYTES,
        qspi=False,
    ),
    FlashStage(
        "signed metadata commit",
        "metadata.bin",
        "metadata",
        METADATA_ADDR,
        METADATA_SIZE,
        exact_bytes=METADATA_STRUCT_SIZE,
        reset_after=True,
    ),
)


# Backward-compatible public constant used by existing Slot A tests/tools.
FLASH_STAGES = flash_stages_for_slot("A")


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _normalize_stlink_serial(value: str) -> str:
    serial = value.strip().upper()
    if re.fullmatch(r"[0-9A-F]{24}", serial) is None:
        raise local.LocalWebConfigError(
            "--stlink-serial must be exactly 24 hexadecimal characters"
        )
    return serial


def _normalize_stlink_binding(value: str) -> str:
    binding = value.strip().upper()
    if binding == AUTOMATIC_STLINK_BINDING:
        return binding
    return _normalize_stlink_serial(binding)


def _openocd_serial_argument(stlink_binding: str) -> str | None:
    if stlink_binding == AUTOMATIC_STLINK_BINDING:
        return None
    return _normalize_stlink_serial(stlink_binding)


def _describe_stlink_binding(stlink_binding: str) -> str:
    if stlink_binding == AUTOMATIC_STLINK_BINDING:
        return "automatic (the single connected ST-Link)"
    return stlink_binding


def _normalize_target_uid(value: str) -> str:
    uid = value.strip().upper()
    if re.fullmatch(r"[0-9A-F]{24}", uid) is None:
        raise local.LocalWebConfigError(
            "--confirm-target-uid must be exactly 24 hexadecimal characters"
        )
    return uid


def _artifact_bundle_digest(
    manifest: dict[str, Any],
    plan: list[dict[str, Any]],
) -> str:
    contract = {
        "manifestFormatVersion": manifest.get("formatVersion"),
        "deviceId": manifest.get("deviceId"),
        "productId": manifest.get("productId"),
        "pcbRevision": manifest.get("pcbRevision"),
        "firmwareMeasurement": manifest.get("firmwareMeasurement"),
        "siliconRevisionQualification": manifest.get(
            "siliconRevisionQualification"
        ),
        "stages": [
            {
                "filename": item["stage"].filename,
                "address": item["stage"].address,
                "bytes": item["bytes"],
                "sha256": item["sha256"],
            }
            for item in plan
        ],
    }
    encoded = json.dumps(
        contract,
        sort_keys=True,
        separators=(",", ":"),
    ).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def _transaction_manifest_binding(
    manifest: dict[str, Any],
) -> dict[str, Any]:
    """Persist only the signed/verified fields needed for offline resume."""

    return {
        "formatVersion": manifest.get("formatVersion"),
        "deviceId": manifest.get("deviceId"),
        "productId": manifest.get("productId"),
        "pcbRevision": manifest.get("pcbRevision"),
        "firmwareMeasurement": manifest.get("firmwareMeasurement"),
        "siliconRevisionQualification": manifest.get(
            "siliconRevisionQualification"
        ),
        "addresses": manifest.get("addresses"),
    }


def _expected_addresses(slot: str = "A") -> dict[str, str]:
    return {
        stage.address_key: f"0x{stage.address:08X}"
        for stage in flash_stages_for_slot(slot)
    }


def build_flash_plan(
    state_dir: Path,
    manifest: dict[str, Any],
) -> list[dict[str, Any]]:
    """Turn a verified manifest into an address- and size-locked write plan."""

    target_slot = local.artifact_target_slot(manifest)
    stages = flash_stages_for_slot(target_slot)
    if manifest.get("addresses") != _expected_addresses(target_slot):
        raise local.LocalWebConfigError(
            "artifact target address contract does not match the STM32 flash plan"
        )
    records = manifest.get("files")
    if not isinstance(records, dict):
        raise local.LocalWebConfigError("artifact file records are missing")

    artifacts = (state_dir / "artifacts").resolve()
    plan: list[dict[str, Any]] = []
    for stage in stages:
        record = records.get(stage.filename)
        if not isinstance(record, dict):
            raise local.LocalWebConfigError(
                f"artifact record is missing: {stage.filename}"
            )
        try:
            path = (artifacts / stage.filename).resolve(strict=True)
        except OSError as exc:
            raise local.LocalWebConfigError(
                f"artifact is missing: {stage.filename}"
            ) from exc
        if path.parent != artifacts:
            raise local.LocalWebConfigError("artifact path escapes the handoff directory")
        size = path.stat().st_size
        if size <= 0 or size > stage.maximum_bytes:
            raise local.LocalWebConfigError(
                f"artifact does not fit its target partition: {stage.filename}"
            )
        if stage.exact_bytes is not None and size != stage.exact_bytes:
            raise local.LocalWebConfigError(
                f"artifact has an invalid exact size: {stage.filename}"
            )
        digest = _sha256(path)
        if record.get("bytes") != size or record.get("sha256") != digest:
            raise local.LocalWebConfigError(
                f"artifact record mismatch: {stage.filename}"
            )
        if stage.qspi:
            end = stage.address + size
            if (
                stage.address < EXTERNAL_FLASH_BASE
                or end > EXTERNAL_FLASH_BASE + EXTERNAL_FLASH_SIZE
                or stage.address % QSPI_SECTOR_BYTES != 0
            ):
                raise local.LocalWebConfigError(
                    f"unsafe QSPI target range: {stage.filename}"
                )
        plan.append(
            {
                "stage": stage,
                "path": path,
                "bytes": size,
                "sha256": digest,
            }
        )

    if plan[-1]["stage"].filename != "metadata.bin":
        raise local.LocalWebConfigError("signed metadata must be the final write")
    return plan


def _make_private_directory(path: Path) -> None:
    path.mkdir(mode=0o700, parents=True, exist_ok=True)
    try:
        path.chmod(0o700)
    except OSError:
        pass


def _fsync_directory(path: Path) -> None:
    try:
        descriptor = os.open(path, os.O_RDONLY)
    except OSError:
        return
    try:
        os.fsync(descriptor)
    except OSError:
        pass
    finally:
        os.close(descriptor)


def _fsync_file(path: Path) -> None:
    # Windows' _commit rejects a read-only descriptor, so open the newly
    # created private file read/write before it is made immutable.
    with path.open("r+b") as source:
        os.fsync(source.fileno())


def _durable_write_text(path: Path, value: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(
        f".{path.name}.{os.getpid()}.{secrets.token_hex(8)}.tmp"
    )
    try:
        descriptor = os.open(
            temporary,
            os.O_WRONLY | os.O_CREAT | os.O_EXCL,
            0o600,
        )
    except OSError as exc:
        raise local.LocalWebConfigError(
            f"cannot create durable transaction state temporary: {temporary}"
        ) from exc
    try:
        with os.fdopen(descriptor, "wb") as output:
            output.write(value.encode("utf-8"))
            output.flush()
            os.fsync(output.fileno())
        temporary.replace(path)
        _fsync_directory(path.parent)
    finally:
        try:
            temporary.unlink(missing_ok=True)
        except OSError:
            pass


def _verify_snapshot_item(item: dict[str, Any]) -> None:
    path: Path = item["path"]
    try:
        size = path.stat().st_size
        digest = _sha256(path)
    except OSError as exc:
        raise local.LocalWebConfigError(
            f"immutable staging artifact is unavailable: {path.name}"
        ) from exc
    if size != item["bytes"] or digest != item["sha256"]:
        raise local.LocalWebConfigError(
            f"immutable staging artifact changed: {path.name}"
        )


def _stage_flash_plan(
    source_plan: list[dict[str, Any]],
    staging_dir: Path,
) -> list[dict[str, Any]]:
    """Create and verify a private read-only snapshot used by every write."""

    staging_dir.mkdir(mode=0o700, parents=False, exist_ok=False)
    staged_plan: list[dict[str, Any]] = []
    for item in source_plan:
        destination = staging_dir / item["stage"].filename
        temporary = destination.with_suffix(destination.suffix + ".tmp")
        shutil.copyfile(item["path"], temporary)
        staged = dict(item)
        staged["path"] = temporary
        _verify_snapshot_item(staged)
        _fsync_file(temporary)
        try:
            temporary.chmod(0o400)
        except OSError:
            pass
        temporary.replace(destination)
        _fsync_directory(staging_dir)
        staged["path"] = destination
        _verify_snapshot_item(staged)
        staged_plan.append(staged)
    try:
        staging_dir.chmod(0o500)
    except OSError:
        pass
    return staged_plan


def _load_staged_plan(
    source_plan: list[dict[str, Any]],
    staging_dir: Path,
) -> list[dict[str, Any]]:
    staged_plan: list[dict[str, Any]] = []
    for item in source_plan:
        staged = dict(item)
        staged["path"] = staging_dir / item["stage"].filename
        _verify_snapshot_item(staged)
        staged_plan.append(staged)
    return staged_plan


def _transaction_state_mac(state: dict[str, Any], resume_token: str) -> str:
    unsigned = dict(state)
    unsigned.pop("transactionMac", None)
    encoded = json.dumps(
        unsigned,
        sort_keys=True,
        separators=(",", ":"),
    ).encode("utf-8")
    return hmac.new(
        resume_token.encode("ascii", errors="strict"),
        encoded,
        hashlib.sha256,
    ).hexdigest()


def _write_transaction_state(
    path: Path,
    state: dict[str, Any],
    resume_token: str,
) -> None:
    updated = dict(state)
    updated["updatedAt"] = datetime.now(timezone.utc).isoformat()
    updated["transactionMac"] = _transaction_state_mac(
        updated,
        resume_token,
    )
    _durable_write_text(
        path,
        json.dumps(updated, indent=2, sort_keys=True) + "\n",
    )
    state.clear()
    state.update(updated)


def _read_transaction_state(path: Path) -> dict[str, Any]:
    try:
        state = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise local.LocalWebConfigError(
            f"flash transaction state is missing or corrupt: {path}"
        ) from exc
    if not isinstance(state, dict) or state.get("formatVersion") != (
        TRANSACTION_FORMAT_VERSION
    ):
        raise local.LocalWebConfigError("unsupported flash transaction state")
    return state


def _resume_token_hash(token: str) -> str:
    return hashlib.sha256(token.encode("ascii", errors="strict")).hexdigest()


def _verify_resume_token_and_state_mac(
    state: dict[str, Any],
    resume_token: str,
) -> None:
    try:
        actual_token_hash = _resume_token_hash(resume_token)
        actual_mac = _transaction_state_mac(state, resume_token)
    except UnicodeEncodeError as exc:
        raise local.LocalWebConfigError("resume token is not valid ASCII") from exc
    if not secrets.compare_digest(
        actual_token_hash,
        str(state.get("resumeTokenSha256", "")),
    ):
        raise local.LocalWebConfigError("resume token does not match transaction")
    if not secrets.compare_digest(
        actual_mac,
        str(state.get("transactionMac", "")),
    ):
        raise local.LocalWebConfigError(
            "flash transaction state authentication failed"
        )


@contextmanager
def _global_hardware_lock(name: str):
    """Cross-state, cross-bundle OS lock released automatically after a crash."""

    lock_root = Path(tempfile.gettempdir()) / "hbox-webconfig-flash-locks"
    _make_private_directory(lock_root)
    lock_path = lock_root / f"{name}.lock"
    handle = lock_path.open("a+b")
    locked = False
    try:
        if handle.seek(0, os.SEEK_END) == 0:
            handle.write(b"\0")
            handle.flush()
        handle.seek(0)
        try:
            if os.name == "nt":
                import msvcrt

                msvcrt.locking(handle.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl

                fcntl.flock(handle.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
            locked = True
        except (OSError, ImportError) as exc:
            raise local.LocalWebConfigError(
                f"another process owns the hardware flash lock: {name}"
            ) from exc
        yield
    finally:
        if locked:
            try:
                handle.seek(0)
                if os.name == "nt":
                    import msvcrt

                    msvcrt.locking(handle.fileno(), msvcrt.LK_UNLCK, 1)
                else:
                    import fcntl

                    fcntl.flock(handle.fileno(), fcntl.LOCK_UN)
            except (OSError, ImportError):
                pass
        handle.close()


def _stlink_lock(stlink_serial: str):
    return _global_hardware_lock(f"stlink-{stlink_serial}")


def _uid_lock(target_uid: str):
    return _global_hardware_lock(f"stm32-{target_uid}")


def _openocd_lock():
    # This is a per-OS-account lock rooted in that account's temp directory.
    # It serializes every hardware-touching OpenOCD call made by this tool.
    return _global_hardware_lock("openocd-all")


def _reject_other_active_transactions(
    state_dir: Path,
    target_uid: str,
    expected_transaction_id: str,
) -> None:
    transactions = state_dir / "flash-transactions"
    if not transactions.is_dir():
        return
    for directory in transactions.iterdir():
        if (
            not directory.is_dir()
            or directory.name.startswith(".creating-")
            or directory.name == expected_transaction_id
        ):
            continue
        state_path = directory / "transaction.json"
        if not state_path.is_file():
            continue
        state = _read_transaction_state(state_path)
        if (
            state.get("targetUid") == target_uid
            and state.get("status") in BLOCKING_TRANSACTION_STATUSES
        ):
            raise local.LocalWebConfigError(
                "a different artifact bundle has an active transaction for this "
                f"target: {state_path}"
            )


def _stage_records(plan: list[dict[str, Any]]) -> list[dict[str, Any]]:
    return [
        {
            "filename": item["stage"].filename,
            "address": f"0x{item['stage'].address:08X}",
            "bytes": item["bytes"],
            "sha256": item["sha256"],
        }
        for item in plan
    ]


def _normalize_transaction_id(value: str) -> str:
    candidate = value.strip()
    match = re.fullmatch(
        r"([0-9A-Fa-f]{24})-([0-9A-Fa-f]{64})",
        candidate,
    )
    if match is None:
        raise local.LocalWebConfigError(
            "--resume-transaction must be a UID-bundle transaction ID"
        )
    return f"{match.group(1).upper()}-{match.group(2).lower()}"


def _validate_manifest_binding(binding: Any) -> dict[str, Any]:
    if not isinstance(binding, dict):
        raise local.LocalWebConfigError(
            "flash transaction manifest binding is missing"
        )
    if binding.get("formatVersion") != local.ARTIFACT_MANIFEST_VERSION:
        raise local.LocalWebConfigError(
            "flash transaction artifact manifest version is unsupported"
        )
    if binding.get("addresses") != _expected_addresses():
        raise local.LocalWebConfigError(
            "flash transaction address contract is invalid"
        )
    if re.fullmatch(
        r"[0-9a-f]{32}",
        str(binding.get("deviceId", "")),
    ) is None:
        raise local.LocalWebConfigError(
            "flash transaction device ID binding is invalid"
        )
    if binding.get("productId") != "HBOX":
        raise local.LocalWebConfigError(
            "flash transaction product binding is invalid"
        )
    if not isinstance(binding.get("pcbRevision"), str) or not binding.get(
        "pcbRevision"
    ):
        raise local.LocalWebConfigError(
            "flash transaction PCB revision binding is invalid"
        )
    if re.fullmatch(
        r"[0-9a-f]{64}",
        str(binding.get("firmwareMeasurement", "")),
    ) is None:
        raise local.LocalWebConfigError(
            "flash transaction firmware measurement binding is invalid"
        )
    qualification = binding.get("siliconRevisionQualification")
    if not isinstance(qualification, dict) or not isinstance(
        qualification.get("enabled"), bool
    ):
        raise local.LocalWebConfigError(
            "flash transaction silicon qualification binding is invalid"
        )
    if qualification.get("enabled"):
        revision = qualification.get("stm32RevisionId")
        if not isinstance(revision, int) or not 0 <= revision <= 0xFFFF:
            raise local.LocalWebConfigError(
                "flash transaction qualified REV_ID is invalid"
            )
    elif qualification.get("stm32RevisionId") is not None:
        raise local.LocalWebConfigError(
            "disabled silicon qualification must not bind a REV_ID"
        )
    return binding


def _load_transaction_staged_plan(
    transaction_dir: Path,
    state: dict[str, Any],
) -> list[dict[str, Any]]:
    records = state.get("stages")
    if not isinstance(records, list) or len(records) != len(FLASH_STAGES):
        raise local.LocalWebConfigError(
            "flash transaction stage contract is invalid"
        )
    staging_dir = transaction_dir / "staging"
    try:
        resolved_staging = staging_dir.resolve(strict=True)
    except OSError as exc:
        raise local.LocalWebConfigError(
            "flash transaction staging directory is missing"
        ) from exc
    plan: list[dict[str, Any]] = []
    for stage, record in zip(FLASH_STAGES, records, strict=True):
        if not isinstance(record, dict):
            raise local.LocalWebConfigError(
                "flash transaction stage record is invalid"
            )
        size = record.get("bytes")
        digest = record.get("sha256")
        if (
            record.get("filename") != stage.filename
            or record.get("address") != f"0x{stage.address:08X}"
            or not isinstance(size, int)
            or size <= 0
            or size > stage.maximum_bytes
            or (
                stage.exact_bytes is not None
                and size != stage.exact_bytes
            )
            or re.fullmatch(r"[0-9a-f]{64}", str(digest)) is None
        ):
            raise local.LocalWebConfigError(
                f"flash transaction stage contract is invalid: {stage.filename}"
            )
        try:
            path = (staging_dir / stage.filename).resolve(strict=True)
        except OSError as exc:
            raise local.LocalWebConfigError(
                f"flash transaction staged file is missing: {stage.filename}"
            ) from exc
        if path.parent != resolved_staging:
            raise local.LocalWebConfigError(
                "flash transaction staged path escapes its private directory"
            )
        item = {
            "stage": stage,
            "path": path,
            "bytes": size,
            "sha256": digest,
        }
        _verify_snapshot_item(item)
        plan.append(item)
    return plan


def _validate_stlink_serial_history(state: dict[str, Any]) -> None:
    history = state.get("stlinkSerialHistory")
    if not isinstance(history, list) or not history:
        raise local.LocalWebConfigError(
            "flash transaction ST-Link history is missing"
        )
    first = history[0]
    if (
        not isinstance(first, dict)
        or first.get("event") != "initial-binding"
        or not isinstance(first.get("at"), str)
        or not first.get("at")
    ):
        raise local.LocalWebConfigError(
            "flash transaction ST-Link history is invalid"
        )
    current = _normalize_stlink_binding(str(first.get("serial", "")))
    for entry in history[1:]:
        if (
            not isinstance(entry, dict)
            or entry.get("event") != "replacement"
            or not isinstance(entry.get("at"), str)
            or not entry.get("at")
            or _normalize_stlink_binding(str(entry.get("oldSerial", "")))
            != current
        ):
            raise local.LocalWebConfigError(
                "flash transaction ST-Link replacement history is invalid"
            )
        current = _normalize_stlink_binding(
            str(entry.get("newSerial", ""))
        )
    if current != _normalize_stlink_binding(
        str(state.get("stlinkSerial", ""))
    ):
        raise local.LocalWebConfigError(
            "flash transaction ST-Link binding disagrees with its history"
        )


def _validate_transaction_state_contract(
    transaction_dir: Path,
    state: dict[str, Any],
    resume_token: str,
) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    _verify_resume_token_and_state_mac(state, resume_token)
    transaction_id = _normalize_transaction_id(
        str(state.get("transactionId", ""))
    )
    if transaction_dir.name != transaction_id:
        raise local.LocalWebConfigError(
            "flash transaction directory/name binding is invalid"
        )
    target_uid = _normalize_target_uid(str(state.get("targetUid", "")))
    if not transaction_id.startswith(target_uid + "-"):
        raise local.LocalWebConfigError(
            "flash transaction UID/name binding is invalid"
        )
    _validate_stlink_serial_history(state)
    manifest = _validate_manifest_binding(state.get("artifactManifest"))
    plan = _load_transaction_staged_plan(transaction_dir, state)
    bundle_digest = _artifact_bundle_digest(manifest, plan)
    if (
        state.get("artifactBundleSha256") != bundle_digest
        or transaction_id != f"{target_uid}-{bundle_digest}"
        or state.get("deviceId") != manifest.get("deviceId")
        or state.get("productId") != manifest.get("productId")
        or state.get("pcbRevision") != manifest.get("pcbRevision")
    ):
        raise local.LocalWebConfigError(
            "flash transaction artifact bundle binding is invalid"
        )
    internal_item = next(item for item in plan if not item["stage"].qspi)
    if state.get("internalImageSha256") != internal_item["sha256"]:
        raise local.LocalWebConfigError(
            "flash transaction internal image binding is invalid"
        )
    attempts = state.get("internalAttempts")
    if (
        state.get("maximumInternalAttempts") != MAX_INTERNAL_ATTEMPTS
        or not isinstance(attempts, int)
        or not 0 <= attempts <= MAX_INTERNAL_ATTEMPTS
    ):
        raise local.LocalWebConfigError(
            "flash transaction internal attempt counter is invalid"
        )
    status = state.get("status")
    if status == "completed":
        raise local.LocalWebConfigError(
            "this target/artifact flash transaction is already completed"
        )
    matching_tail_false_positive_recovery = (
        status == "manual-recovery-required"
        and attempts == 0
        and state.get("lastError")
        == MATCHING_SECURITY_TAIL_FALSE_POSITIVE_ERROR
        and state.get("originalBackup") is not None
    )
    if status not in ACTIVE_TRANSACTION_STATUSES and not (
        matching_tail_false_positive_recovery
    ):
        raise local.LocalWebConfigError(
            f"flash transaction is not recoverable: {status}"
        )
    if state.get("originalBackup") is not None:
        original_backup = _validate_original_backup(transaction_dir, state)
        if matching_tail_false_positive_recovery:
            original_tail = original_backup.read_bytes()[
                INTERNAL_SECURITY_TAIL_OFFSET:
            ]
            desired_tail = internal_item["path"].read_bytes()[
                INTERNAL_SECURITY_TAIL_OFFSET:
            ]
            if not hmac.compare_digest(original_tail, desired_tail):
                raise local.LocalWebConfigError(
                    "flash transaction is not recoverable: authenticated "
                    "identity/security tail differs from the desired image"
                )
    return manifest, plan


def _prepare_transaction(
    state_dir: Path,
    manifest: dict[str, Any],
    source_plan: list[dict[str, Any]],
    target: TargetIdentity,
    bundle_digest: str,
) -> tuple[Path, dict[str, Any], list[dict[str, Any]], str]:
    transactions = state_dir / "flash-transactions"
    _make_private_directory(transactions)
    transaction_id = f"{target.uid}-{bundle_digest}"
    transaction_dir = transactions / transaction_id
    if transaction_dir.exists():
        raise local.LocalWebConfigError(
            "a transaction already exists; resume it explicitly with "
            f"--resume-transaction {transaction_id} and its token"
        )
    creation_dir = Path(
        tempfile.mkdtemp(
            prefix=f".creating-{transaction_id}-",
            dir=transactions,
        )
    )
    try:
        creation_dir.chmod(0o700)
    except OSError:
        pass
    published = False
    try:
        staged_plan = _stage_flash_plan(
            source_plan,
            creation_dir / "staging",
        )
        issued_resume_token = secrets.token_hex(16)
        created_at = datetime.now(timezone.utc).isoformat()
        state = {
            "formatVersion": TRANSACTION_FORMAT_VERSION,
            "transactionId": transaction_id,
            "createdAt": created_at,
            "status": "staged",
            "targetUid": target.uid,
            "stlinkSerial": target.stlink_serial,
            "stlinkSerialHistory": [
                {
                    "event": "initial-binding",
                    "serial": target.stlink_serial,
                    "at": created_at,
                }
            ],
            "deviceId": manifest.get("deviceId"),
            "productId": manifest.get("productId"),
            "pcbRevision": manifest.get("pcbRevision"),
            "artifactManifest": _transaction_manifest_binding(manifest),
            "artifactBundleSha256": bundle_digest,
            "internalImageSha256": next(
                item["sha256"]
                for item in staged_plan
                if not item["stage"].qspi
            ),
            "resumeTokenSha256": _resume_token_hash(issued_resume_token),
            "originalBackup": None,
            "internalAttempts": 0,
            "maximumInternalAttempts": MAX_INTERNAL_ATTEMPTS,
            "lastError": None,
            "stages": _stage_records(staged_plan),
        }
        _durable_write_text(
            creation_dir / "resume-token.txt",
            issued_resume_token + "\n",
        )
        _write_transaction_state(
            creation_dir / "transaction.json",
            state,
            issued_resume_token,
        )
        _fsync_directory(creation_dir)
        creation_dir.replace(transaction_dir)
        published = True
        _fsync_directory(transactions)
        plan = _load_transaction_staged_plan(transaction_dir, state)
        return transaction_dir, state, plan, issued_resume_token
    finally:
        if not published and creation_dir.exists():
            # This exact private directory was created by this invocation and
            # has never been published or used for a hardware write.
            try:
                shutil.rmtree(creation_dir)
            except OSError:
                # A crash residue has a .creating-* name and is ignored by all
                # transaction lookups, so it cannot block a safe retry.
                pass


def _load_resume_transaction(
    state_dir: Path,
    transaction_argument: str,
    resume_token: str,
    confirmed_target_uid: str,
) -> tuple[Path, dict[str, Any], dict[str, Any], list[dict[str, Any]]]:
    transaction_id = _normalize_transaction_id(transaction_argument)
    transaction_dir = state_dir / "flash-transactions" / transaction_id
    try:
        resolved = transaction_dir.resolve(strict=True)
        transactions = (state_dir / "flash-transactions").resolve(strict=True)
    except OSError as exc:
        raise local.LocalWebConfigError(
            "the requested flash transaction does not exist"
        ) from exc
    if resolved.parent != transactions or resolved.name != transaction_id:
        raise local.LocalWebConfigError(
            "flash transaction path escapes the selected state directory"
        )
    state = _read_transaction_state(resolved / "transaction.json")
    manifest, plan = _validate_transaction_state_contract(
        resolved,
        state,
        resume_token,
    )
    if state.get("targetUid") != confirmed_target_uid:
        raise local.LocalWebConfigError(
            "confirmed STM32 UID does not match the requested transaction"
        )
    return resolved, state, manifest, plan


def _parse_stlink_replacement(
    value: str | None,
) -> tuple[str, str] | None:
    if value is None:
        return None
    parts = value.split(":")
    if len(parts) != 2:
        raise local.LocalWebConfigError(
            "--confirm-stlink-replacement must be OLD_SERIAL:NEW_SERIAL"
        )
    old_serial = _normalize_stlink_serial(parts[0])
    new_serial = _normalize_stlink_serial(parts[1])
    if old_serial == new_serial:
        raise local.LocalWebConfigError(
            "ST-Link replacement must name two different serials"
        )
    return old_serial, new_serial


def _replacement_required(
    transaction_dir: Path,
    state: dict[str, Any],
    selected_serial: str,
    replacement: tuple[str, str] | None,
) -> bool:
    current_serial = _normalize_stlink_binding(
        str(state.get("stlinkSerial", ""))
    )
    if selected_serial == current_serial:
        if replacement is not None:
            raise local.LocalWebConfigError(
                "ST-Link replacement confirmation was provided but is not needed"
            )
        return False
    if replacement != (current_serial, selected_serial):
        raise local.LocalWebConfigError(
            "resuming with a different ST-Link requires exact explicit "
            "--confirm-stlink-replacement OLD_SERIAL:NEW_SERIAL"
        )
    if state.get("originalBackup") is None:
        raise local.LocalWebConfigError(
            "ST-Link replacement requires a recorded original internal Flash backup"
        )
    _validate_original_backup(transaction_dir, state)
    return True


def _record_stlink_replacement(
    state_path: Path,
    state: dict[str, Any],
    replacement: tuple[str, str],
    resume_token: str,
) -> None:
    old_serial, new_serial = replacement
    if state.get("stlinkSerial") != old_serial:
        raise local.LocalWebConfigError(
            "flash transaction ST-Link binding changed before replacement audit"
        )
    history = list(state.get("stlinkSerialHistory", []))
    history.append(
        {
            "event": "replacement",
            "oldSerial": old_serial,
            "newSerial": new_serial,
            "at": datetime.now(timezone.utc).isoformat(),
        }
    )
    state["stlinkSerial"] = new_serial
    state["stlinkSerialHistory"] = history
    _write_transaction_state(state_path, state, resume_token)
    _validate_stlink_serial_history(state)


def validate_openocd_executable(openocd: Path) -> Path:
    """Require an explicit modern OpenOCD binary instead of trusting PATH."""

    expanded = openocd.expanduser()
    if not expanded.is_absolute():
        raise local.LocalWebConfigError(
            "--openocd must be an explicit absolute path; PATH lookup is disabled"
        )
    try:
        resolved = expanded.resolve(strict=True)
    except OSError as exc:
        raise local.LocalWebConfigError(
            f"OpenOCD executable does not exist: {expanded}"
        ) from exc
    if not resolved.is_file():
        raise local.LocalWebConfigError(f"OpenOCD path is not a file: {resolved}")
    if os.name != "nt" and not os.access(resolved, os.X_OK):
        raise local.LocalWebConfigError(f"OpenOCD file is not executable: {resolved}")

    result = local._run(
        [str(resolved), "--version"],
        capture=True,
        quiet=True,
    )
    match = re.search(
        r"Open On-Chip Debugger\s+(\d+)\.(\d+)",
        result.stdout or "",
        flags=re.IGNORECASE,
    )
    if match is None:
        raise local.LocalWebConfigError(
            "the selected executable did not identify itself as OpenOCD"
        )
    version = (int(match.group(1)), int(match.group(2)))
    if version < MINIMUM_OPENOCD_VERSION:
        minimum = ".".join(str(part) for part in MINIMUM_OPENOCD_VERSION)
        raise local.LocalWebConfigError(
            f"OpenOCD {minimum} or newer is required; selected {version[0]}.{version[1]}"
        )

    for config in (
        PROJECT_ROOT / "bootloader" / "Openocd_Script" / "ST-LINK-FLASH.cfg",
        PROJECT_ROOT / "application" / "Openocd_Script" / "ST-LINK-QSPIFLASH.cfg",
    ):
        if not config.is_file():
            raise local.LocalWebConfigError(
                f"required OpenOCD configuration is missing: {config}"
            )
    return resolved


def resolve_openocd_executable(
    openocd: Path | None,
    *,
    allow_automatic: bool,
) -> Path:
    if openocd is not None:
        return validate_openocd_executable(openocd)
    if not allow_automatic:
        raise local.LocalWebConfigError(
            "--openocd is required unless --simple-execute is used"
        )

    environment_candidate = str(os.environ.get("HBOX_OPENOCD", "")).strip()
    candidates = (
        *((Path(environment_candidate),) if environment_candidate else ()),
        *DEFAULT_OPENOCD_CANDIDATES,
    )
    errors: list[str] = []
    for candidate in candidates:
        if not candidate.expanduser().is_file():
            continue
        try:
            selected = validate_openocd_executable(candidate)
        except local.LocalWebConfigError as exc:
            errors.append(str(exc))
            continue
        print(f"Automatically selected OpenOCD: {selected}")
        return selected

    detail = f" ({'; '.join(errors)})" if errors else ""
    raise local.LocalWebConfigError(
        "no supported OpenOCD 0.11+ installation was found automatically; "
        "set HBOX_OPENOCD or pass --openocd" + detail
    )


def _tcl_path(path: Path, *, must_exist: bool) -> str:
    try:
        return BuildTool._openocd_tcl_braced_path(
            path,
            must_exist=must_exist,
        )
    except ValueError as exc:
        raise local.LocalWebConfigError(str(exc)) from exc


def _internal_openocd_prefix(
    openocd: Path,
    stlink_serial: str,
    *,
    debug_level: int = 0,
) -> list[str]:
    config = PROJECT_ROOT / "bootloader" / "Openocd_Script" / "ST-LINK-FLASH.cfg"
    command = [
        str(openocd),
        f"-d{debug_level}",
        "-f",
        str(config),
    ]
    selected_serial = _openocd_serial_argument(stlink_serial)
    if selected_serial is not None:
        command.extend(["-c", f"adapter serial {selected_serial}"])
    command.extend([
        "-c",
        "gdb_port disabled",
        "-c",
        "tcl_port disabled",
        "-c",
        "telnet_port disabled",
        "-c",
        "init",
        "-c",
        "reset halt",
    ])
    return command


def _openocd_target_assert_arguments(expected_target_uid: str) -> list[str]:
    arguments: list[str] = []
    for command in BuildTool._openocd_target_assert_commands(
        expected_target_uid
    ):
        arguments.extend(["-c", command])
    return arguments


def probe_target_identity(
    openocd: Path,
    stlink_serial: str,
    *,
    run_after: bool,
) -> TargetIdentity:
    """Read MCU family/revision and 96-bit UID through the selected probe."""

    result = local._run(
        _internal_openocd_prefix(
            openocd,
            stlink_serial,
            debug_level=1,
        )
        + [
            "-c",
            "mdw 0x5C001000 1",
            "-c",
            f"mdw 0x{STM32_UID_ADDRESS:08X} 3",
        ]
        + (["-c", "reset run"] if run_after else [])
        + ["-c", "shutdown"],
        cwd=PROJECT_ROOT / "bootloader",
        capture=True,
        quiet=True,
    )
    output = result.stdout or ""
    idcode_match = re.search(
        r"0x5c001000:\s+([0-9a-f]{8})",
        output,
        flags=re.IGNORECASE,
    )
    uid_match = re.search(
        r"0x1ff1e800:\s+([0-9a-f]{8})\s+([0-9a-f]{8})\s+([0-9a-f]{8})",
        output,
        flags=re.IGNORECASE,
    )
    if idcode_match is None or uid_match is None:
        raise local.LocalWebConfigError(
            "OpenOCD did not return DBGMCU_IDCODE and the complete 96-bit UID"
        )
    idcode = int(idcode_match.group(1), 16)
    device_id = idcode & 0xFFF
    revision_id = (idcode >> 16) & 0xFFFF
    # Canonical UID is the three 32-bit values in increasing address order,
    # matching UID_BASE, UID_BASE+4, UID_BASE+8 in the STM32H750 CMSIS header.
    uid = "".join(uid_match.groups()).upper()
    if uid in {"0" * 24, "F" * 24}:
        raise local.LocalWebConfigError("connected STM32 returned an invalid UID")
    if device_id != 0x450:
        raise local.LocalWebConfigError(
            "connected MCU is not the qualified STM32H750 target"
        )
    identity = TargetIdentity(
        stlink_serial=stlink_serial,
        dbgmcu_idcode=idcode,
        device_id=device_id,
        revision_id=revision_id,
        uid=uid,
    )
    print(f"ST-Link serial={identity.stlink_serial}")
    print(f"DBGMCU_IDCODE=0x{identity.dbgmcu_idcode:08X}")
    print(f"DEV_ID=0x{identity.device_id:03X}")
    print(f"REV_ID=0x{identity.revision_id:04X}")
    print(f"STM32_UID={identity.uid}")
    return identity


def _new_observation_path(transaction_dir: Path) -> Path:
    timestamp = datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S-%fZ")
    observations = transaction_dir / "observations"
    _make_private_directory(observations)
    return observations / f"internal-flash-{timestamp}.bin"


def _backup_internal_flash(
    openocd: Path,
    stlink_serial: str,
    expected_target_uid: str,
    destination: Path,
    *,
    replace_existing: bool = False,
) -> Path:
    """Read and validate the complete sector before any destructive command."""

    destination.parent.mkdir(parents=True, exist_ok=True)
    partial = destination.with_name(
        f".{destination.name}.{os.getpid()}.{secrets.token_hex(8)}.partial"
    )
    if destination.exists() and not replace_existing:
        raise local.LocalWebConfigError("refusing to overwrite an existing backup")
    try:
        local._run(
            _internal_openocd_prefix(openocd, stlink_serial)
            + [
                *_openocd_target_assert_arguments(expected_target_uid),
                "-c",
                (
                    f"dump_image {_tcl_path(partial, must_exist=False)} "
                    f"0x{INTERNAL_FLASH_ADDRESS:08X} 0x{INTERNAL_FLASH_BYTES:X}"
                ),
                "-c",
                "shutdown",
            ],
            cwd=PROJECT_ROOT / "bootloader",
            quiet=True,
        )
        try:
            size = partial.stat().st_size
        except OSError:
            size = -1
        if size != INTERNAL_FLASH_BYTES:
            raise local.LocalWebConfigError(
                f"internal Flash backup has {size} bytes; expected {INTERNAL_FLASH_BYTES}"
            )
        _fsync_file(partial)
        try:
            partial.chmod(0o600)
        except OSError:
            pass
        partial.replace(destination)
        _fsync_directory(destination.parent)
    finally:
        try:
            partial.unlink(missing_ok=True)
        except OSError:
            pass
    return destination


def _capture_current_internal_flash(
    openocd: Path,
    stlink_serial: str,
    target_uid: str,
    transaction_dir: Path,
    state: dict[str, Any],
    resume_token: str,
) -> Path:
    """Preserve the original once; later attempts create separate observations."""

    state_path = transaction_dir / "transaction.json"
    if state.get("originalBackup") is None:
        if state.get("status") != "staged":
            raise local.LocalWebConfigError(
                "active transaction lost its original internal Flash backup record"
            )
        original = transaction_dir / "internal-before.bin"
        # A crash may leave an unrecorded final backup or any number of old
        # .partial files. State is still authenticated as `staged`, so no
        # hardware write could have started. Never trust/adopt that residue:
        # re-read the locked UID into a unique observation and atomically
        # replace the final backup only after exact-size validation and fsync.
        _backup_internal_flash(
            openocd,
            stlink_serial,
            target_uid,
            original,
            replace_existing=original.exists(),
        )
        data = original.read_bytes()
        state["originalBackup"] = {
            "file": "internal-before.bin",
            "bytes": len(data),
            "sha256": _sha256(original),
            "securityTailBlank": all(
                value == 0xFF
                for value in data[INTERNAL_SECURITY_TAIL_OFFSET:]
            ),
        }
        state["status"] = "preflight-complete"
        _write_transaction_state(state_path, state, resume_token)
        return original

    _validate_original_backup(transaction_dir, state)
    observation = _new_observation_path(transaction_dir)
    return _backup_internal_flash(
        openocd,
        stlink_serial,
        target_uid,
        observation,
    )


def _validate_original_backup(
    transaction_dir: Path,
    state: dict[str, Any],
) -> Path:
    record = state.get("originalBackup")
    if not isinstance(record, dict) or record.get("file") != "internal-before.bin":
        raise local.LocalWebConfigError("transaction original backup record is invalid")
    backup = transaction_dir / "internal-before.bin"
    try:
        size = backup.stat().st_size
        digest = _sha256(backup)
    except OSError as exc:
        raise local.LocalWebConfigError(
            "transaction original internal Flash backup is missing"
        ) from exc
    if (
        size != INTERNAL_FLASH_BYTES
        or record.get("bytes") != size
        or record.get("sha256") != digest
    ):
        raise local.LocalWebConfigError(
            "transaction original internal Flash backup hash/size mismatch"
        )
    tail_blank = all(
        value == 0xFF
        for value in backup.read_bytes()[INTERNAL_SECURITY_TAIL_OFFSET:]
    )
    if record.get("securityTailBlank") is not tail_blank:
        raise local.LocalWebConfigError(
            "transaction original backup security-tail record is inconsistent"
        )
    return backup


def _internal_flash_needs_programming(
    current_backup: Path,
    image: Path,
    *,
    transaction_dir: Path | None = None,
    transaction_state: dict[str, Any] | None = None,
) -> bool:
    """Decide first-write/skip/recovery without accepting identity replacement.

    Without a separately authenticated erase-intent journal, arbitrary partial
    programming is not proof that this transaction produced the bytes.  An
    interrupted retry is therefore accepted only when the current sector is
    byte-for-byte the recorded original (erase never started), completely
    erased, or already the complete desired image.
    """

    current = current_backup.read_bytes()
    desired = image.read_bytes()
    if len(current) != INTERNAL_FLASH_BYTES:
        raise local.LocalWebConfigError("internal Flash backup size changed unexpectedly")
    if len(desired) != INTERNAL_FLASH_BYTES:
        raise local.LocalWebConfigError(
            "internal Flash provisioning image is not exactly 128 KiB"
        )
    if current == desired:
        return False

    if transaction_dir is not None and transaction_state is not None:
        original = _validate_original_backup(
            transaction_dir,
            transaction_state,
        )
        original_bytes = original.read_bytes()
        original_record = transaction_state["originalBackup"]
        status = transaction_state.get("status")
        if status == "manual-recovery-required":
            matching_tail_false_positive_recovery = (
                transaction_state.get("internalAttempts") == 0
                and transaction_state.get("lastError")
                == MATCHING_SECURITY_TAIL_FALSE_POSITIVE_ERROR
                and current == original_bytes
                and hmac.compare_digest(
                    original_bytes[INTERNAL_SECURITY_TAIL_OFFSET:],
                    desired[INTERNAL_SECURITY_TAIL_OFFSET:],
                )
            )
            if matching_tail_false_positive_recovery:
                print(
                    "Recovering a pre-write matching-tail false positive; "
                    "the authenticated target is unchanged and no internal "
                    "Flash write was attempted."
                )
                return True
            raise local.LocalWebConfigError(
                "manual-recovery transaction does not meet the restricted "
                "pre-write matching-tail recovery conditions"
            )
        if status in {
            "staged",
            "preflight-complete",
            "payloads-programming",
            "payloads-verified",
        }:
            if current == original_bytes:
                original_tail = original_bytes[INTERNAL_SECURITY_TAIL_OFFSET:]
                desired_tail = desired[INTERNAL_SECURITY_TAIL_OFFSET:]
                if original_record.get("securityTailBlank") is True:
                    return True
                if hmac.compare_digest(original_tail, desired_tail):
                    print(
                        "Authenticated identity/security tail exactly matches "
                        "the desired image; protected bootloader update allowed."
                    )
                    return True
                raise local.LocalWebConfigError(
                    MATCHING_SECURITY_TAIL_FALSE_POSITIVE_ERROR
                )
            raise local.LocalWebConfigError(
                "internal Flash changed after its authenticated preflight backup; "
                "manual recovery is required"
            )
        if status in {"internal-verified", "metadata-programming"}:
            raise local.LocalWebConfigError(
                "internal Flash differs after a previously verified phase; "
                "manual recovery is required"
            )
        if status != "internal-programming":
            raise local.LocalWebConfigError(
                "internal Flash transaction phase is not safe for programming"
            )
        recovery_binding_valid = (
            original_record.get("securityTailBlank") is True
            and transaction_state.get("internalImageSha256") == _sha256(image)
        )
        if not recovery_binding_valid:
            raise local.LocalWebConfigError(
                "interrupted internal Flash transaction lost its blank-original/image binding"
            )
        if current == original_bytes:
            print(
                "Internal Flash still matches the authenticated original backup; "
                "the interrupted erase did not start."
            )
            return True
        if all(value == 0xFF for value in current):
            print(
                "Internal Flash is completely erased under the authenticated "
                "interrupted transaction; retry is allowed."
            )
            return True
        raise local.LocalWebConfigError(
            "current internal Flash is neither the authenticated original, fully "
            "erased, nor the complete desired image; manual recovery is required"
        )

    if any(
        value != 0xFF for value in current[INTERNAL_SECURITY_TAIL_OFFSET:]
    ):
        raise local.LocalWebConfigError(
            "target identity/security tail differs from this artifact; refusing to erase "
            f"the internal sector (backup preserved at {current_backup})"
        )
    return True


def _program_internal_flash(
    openocd: Path,
    stlink_serial: str,
    expected_target_uid: str,
    image: Path,
) -> None:
    """Erase sector 0, write all 128 KiB, and verify the complete raw image."""

    if image.stat().st_size != INTERNAL_FLASH_BYTES:
        raise local.LocalWebConfigError(
            "internal Flash provisioning image is not exactly 128 KiB"
        )
    encoded = _tcl_path(image, must_exist=True)
    local._run(
        _internal_openocd_prefix(openocd, stlink_serial)
        + [
            *_openocd_target_assert_arguments(expected_target_uid),
            "-c",
            "flash probe 0",
            "-c",
            "flash info 0",
            "-c",
            "flash erase_sector 0 0 0",
            "-c",
            f"flash write_image {encoded} 0x{INTERNAL_FLASH_ADDRESS:08X} bin",
            "-c",
            f"verify_image {encoded} 0x{INTERNAL_FLASH_ADDRESS:08X} bin",
            "-c",
            "shutdown",
        ],
        cwd=PROJECT_ROOT / "bootloader",
        quiet=True,
    )


def _new_build_tool(openocd: Path) -> BuildTool:
    tool = BuildTool.__new__(BuildTool)
    tool.application_dir = PROJECT_ROOT / "application"
    tool.config = {"openocd_path": str(openocd)}
    return tool


def _print_plan(manifest: dict[str, Any], plan: list[dict[str, Any]]) -> None:
    print("Local WebConfig STM32 flash plan")
    print(f"  Device ID: {manifest.get('deviceId')}")
    print(f"  Product: {manifest.get('productId')}")
    print(f"  PCB revision: {manifest.get('pcbRevision')}")
    for index, item in enumerate(plan, start=1):
        stage: FlashStage = item["stage"]
        suffix = " (final commit)" if stage.reset_after else ""
        print(
            f"  {index}. {stage.name}: {stage.filename}, {item['bytes']} bytes, "
            f"0x{stage.address:08X}{suffix}"
        )
        print(f"     SHA-256 {item['sha256']}")
    print("  CH585 maintenance firmware is not part of this STM32 transaction.")
    print("  Option bytes are never read-modify-written by this command.")


def _execute_prepared_transaction(
    transaction_dir: Path,
    manifest: dict[str, Any],
    plan: list[dict[str, Any]],
    openocd: Path,
    target: TargetIdentity,
    resume_token: str,
    *,
    issued_resume_token: str | None,
) -> list[dict[str, Any]]:
    """Run one authenticated transaction while all hardware locks are held."""

    state_path = transaction_dir / "transaction.json"
    state = _read_transaction_state(state_path)
    authenticated_manifest, authenticated_plan = (
        _validate_transaction_state_contract(
            transaction_dir,
            state,
            resume_token,
        )
    )
    if (
        authenticated_manifest != _transaction_manifest_binding(manifest)
        or _stage_records(authenticated_plan) != _stage_records(plan)
    ):
        raise local.LocalWebConfigError(
            "flash transaction changed after hardware lock acquisition"
        )
    plan = authenticated_plan
    print(f"Flash transaction: {state['transactionId']}")
    print(f"Transaction state: {state_path}")
    if issued_resume_token is not None:
        print(f"Resume token: {issued_resume_token}")
        print(f"Resume token file: {transaction_dir / 'resume-token.txt'}")

    current_backup = _capture_current_internal_flash(
        openocd,
        target.stlink_serial,
        target.uid,
        transaction_dir,
        state,
        resume_token,
    )
    original_backup = _validate_original_backup(transaction_dir, state)
    print(f"Original internal Flash backup: {original_backup}")
    internal_item = next(item for item in plan if not item["stage"].qspi)
    _verify_snapshot_item(internal_item)
    starting_status = str(state.get("status"))
    try:
        program_internal = _internal_flash_needs_programming(
            current_backup,
            internal_item["path"],
            transaction_dir=transaction_dir,
            transaction_state=state,
        )
    except (local.LocalWebConfigError, OSError, ValueError) as exc:
        state["status"] = "manual-recovery-required"
        state["lastError"] = str(exc)
        _write_transaction_state(state_path, state, resume_token)
        raise

    qspi_tool = _new_build_tool(openocd)
    preserve_recovery_phase = starting_status in {
        "internal-programming",
        "internal-verified",
        "metadata-programming",
    }
    if not preserve_recovery_phase:
        state["status"] = "payloads-programming"
        state["lastError"] = None
        _write_transaction_state(state_path, state, resume_token)
    for item in plan:
        stage = item["stage"]
        if not stage.qspi or stage.reset_after:
            continue
        _verify_snapshot_item(item)
        if not qspi_tool._flash_qspi_file_in_chunks(
            item["path"],
            stage.address,
            stage.name,
            reset_after=False,
            stlink_serial=_openocd_serial_argument(target.stlink_serial),
            expected_target_uid=target.uid,
        ):
            state["lastError"] = f"QSPI payload failed: {stage.name}"
            _write_transaction_state(state_path, state, resume_token)
            raise local.LocalWebConfigError(
                f"STM32 flash transaction stopped before completing {stage.name}; "
                "resume with the recorded token after correcting the fault"
            )
    if not preserve_recovery_phase:
        state["status"] = "payloads-verified"
        state["lastError"] = None
        _write_transaction_state(state_path, state, resume_token)

    if starting_status in {"internal-verified", "metadata-programming"}:
        if program_internal:
            state["status"] = "manual-recovery-required"
            state["lastError"] = (
                "internal Flash differs after a previously verified phase"
            )
            _write_transaction_state(state_path, state, resume_token)
            raise local.LocalWebConfigError(
                "transaction says internal Flash was verified but target bytes differ"
            )
    elif program_internal:
        attempts = int(state.get("internalAttempts", 0))
        if attempts >= MAX_INTERNAL_ATTEMPTS:
            state["status"] = "manual-recovery-required"
            state["lastError"] = "maximum internal Flash recovery attempts exceeded"
            _write_transaction_state(state_path, state, resume_token)
            raise local.LocalWebConfigError(
                "internal Flash recovery attempt limit reached; manual recovery required"
            )
        state["status"] = "internal-programming"
        state["internalAttempts"] = attempts + 1
        state["lastError"] = None
        # This durable state commit happens before the first erase command.
        _write_transaction_state(state_path, state, resume_token)
        _verify_snapshot_item(internal_item)
        try:
            _program_internal_flash(
                openocd,
                target.stlink_serial,
                target.uid,
                internal_item["path"],
            )
        except (local.LocalWebConfigError, OSError, ValueError) as exc:
            state["lastError"] = str(exc)
            if int(state["internalAttempts"]) >= MAX_INTERNAL_ATTEMPTS:
                state["status"] = "manual-recovery-required"
            _write_transaction_state(state_path, state, resume_token)
            raise
        print(
            "Internal Flash 128 KiB image written and verified; target remains halted."
        )
        state["status"] = "internal-verified"
        state["lastError"] = None
        _write_transaction_state(state_path, state, resume_token)
    else:
        print(
            "Internal Flash already exactly matches the immutable artifact; "
            "sector erase safely skipped."
        )
        state["status"] = "internal-verified"
        state["lastError"] = None
        _write_transaction_state(state_path, state, resume_token)

    metadata_item = plan[-1]
    metadata_stage = metadata_item["stage"]
    if not metadata_stage.reset_after or metadata_stage.filename != "metadata.bin":
        raise local.LocalWebConfigError("signed metadata is not the final flash stage")
    _verify_snapshot_item(metadata_item)
    state["status"] = "metadata-programming"
    state["lastError"] = None
    _write_transaction_state(state_path, state, resume_token)
    if not qspi_tool._flash_qspi_file_in_chunks(
        metadata_item["path"],
        metadata_stage.address,
        metadata_stage.name,
        reset_after=True,
        stlink_serial=_openocd_serial_argument(target.stlink_serial),
        expected_target_uid=target.uid,
    ):
        state["lastError"] = "signed metadata commit failed"
        _write_transaction_state(state_path, state, resume_token)
        raise local.LocalWebConfigError(
            "STM32 flash transaction failed while committing signed metadata; "
            "resume with the recorded token"
        )
    state["status"] = "completed"
    state["lastError"] = None
    state["completedAt"] = datetime.now(timezone.utc).isoformat()
    _write_transaction_state(state_path, state, resume_token)

    print("STM32 artifacts were written from the immutable snapshot and verified.")
    print("Signed metadata committed last; completed audit state was retained.")
    print(f"Recoverable original internal Flash backup: {original_backup}")
    print("No STM32 option bytes were changed.")
    if manifest.get("requiresManualLifecycleProvisioning", True):
        print(
            "RDP1, Secure mode, and full-sector SCAR remain mandatory manual lifecycle steps."
        )
    else:
        print(
            "Unlocked development artifact: RDP, SECURITY, and SCAR remain unchanged and are not required."
        )
    return plan


def _create_and_execute_transaction(
    state_dir: Path,
    manifest: dict[str, Any],
    source_plan: list[dict[str, Any]],
    openocd: Path,
    target: TargetIdentity,
) -> list[dict[str, Any]]:
    bundle_digest = _artifact_bundle_digest(manifest, source_plan)
    transaction_id = f"{target.uid}-{bundle_digest}"
    _reject_other_active_transactions(
        state_dir,
        target.uid,
        transaction_id,
    )
    transaction_dir, _state, plan, resume_token = _prepare_transaction(
        state_dir,
        manifest,
        source_plan,
        target,
        bundle_digest,
    )
    return _execute_prepared_transaction(
        transaction_dir,
        manifest,
        plan,
        openocd,
        target,
        resume_token,
        issued_resume_token=resume_token,
    )


def flash_stm32(
    state_dir: Path,
    openocd_argument: Path | None,
    *,
    execute: bool,
    probe_target_only: bool,
    simple_execute: bool = False,
    stlink_serial_argument: str | None,
    confirmed_device_id: str | None,
    confirmed_target_uid: str | None,
    resume_token: str | None,
    resume_transaction_argument: str | None,
    stlink_replacement_argument: str | None,
) -> list[dict[str, Any]]:
    """Validate or execute the one-time local STM32 provisioning transaction."""

    state_dir = state_dir.expanduser().resolve()
    automatic_adapter_allowed = simple_execute or (
        resume_transaction_argument is not None
    )
    if stlink_serial_argument is None:
        if not automatic_adapter_allowed:
            raise local.LocalWebConfigError(
                "--stlink-serial is required unless --simple-execute is used"
            )
        stlink_serial = AUTOMATIC_STLINK_BINDING
    else:
        stlink_serial = _normalize_stlink_binding(stlink_serial_argument)
        if (
            stlink_serial == AUTOMATIC_STLINK_BINDING
            and not simple_execute
            and confirmed_target_uid is None
        ):
            raise local.LocalWebConfigError(
                "automatic ST-Link selection requires --simple-execute or an "
                "already confirmed target UID"
            )
    replacement = _parse_stlink_replacement(stlink_replacement_argument)
    openocd = resolve_openocd_executable(
        openocd_argument,
        allow_automatic=simple_execute,
    )

    if simple_execute:
        if not execute or probe_target_only:
            raise local.LocalWebConfigError(
                "--simple-execute must be used as its own execution mode"
            )
        if (
            confirmed_device_id is not None
            or confirmed_target_uid is not None
            or resume_token is not None
            or resume_transaction_argument is not None
            or replacement is not None
        ):
            raise local.LocalWebConfigError(
                "--simple-execute does not accept confirmation, resume, or "
                "ST-Link replacement options"
            )
        manifest = local.load_verified_artifact_manifest(state_dir)
        source_plan = build_flash_plan(state_dir, manifest)
        artifact_device_id = manifest.get("deviceId")
        if not isinstance(artifact_device_id, str) or not artifact_device_id:
            raise local.LocalWebConfigError(
                "verified artifact manifest has no device ID"
            )

        # The first probe discovers the UID without requiring a copy/paste
        # step. Release both locks before entering the regular transaction
        # path, whose canonical order is UID -> global OpenOCD -> ST-Link.
        # That path probes again under the UID lock, so changing the target in
        # this gap fails closed before any backup, erase, or write.
        with _openocd_lock(), _stlink_lock(stlink_serial):
            discovered_target = probe_target_identity(
                openocd,
                stlink_serial,
                run_after=True,
            )
        print(
            "SIMPLE LAB PREFLIGHT discovered STM32 UID "
            f"{discovered_target.uid}; entering the protected transaction "
            "with a second target check."
        )
        bundle_digest = _artifact_bundle_digest(manifest, source_plan)
        automatic_resume_transaction = (
            f"{discovered_target.uid}-{bundle_digest}"
        )
        automatic_resume_dir = (
            state_dir
            / "flash-transactions"
            / automatic_resume_transaction
        )
        automatic_resume_token: str | None = None
        if automatic_resume_dir.exists():
            token_path = automatic_resume_dir / "resume-token.txt"
            try:
                automatic_resume_token = token_path.read_text(
                    encoding="utf-8"
                ).strip()
            except OSError as exc:
                raise local.LocalWebConfigError(
                    "the matching interrupted transaction exists, but its "
                    "private resume token cannot be read"
                ) from exc
            if not automatic_resume_token:
                raise local.LocalWebConfigError(
                    "the matching interrupted transaction has an empty "
                    "resume token"
                )
            print(
                "SIMPLE LAB found the exact interrupted transaction; "
                "resuming it after authenticated state and target checks."
            )
        return flash_stm32(
            state_dir,
            openocd,
            execute=True,
            probe_target_only=False,
            simple_execute=False,
            stlink_serial_argument=stlink_serial,
            confirmed_device_id=artifact_device_id,
            confirmed_target_uid=discovered_target.uid,
            resume_token=automatic_resume_token,
            resume_transaction_argument=(
                automatic_resume_transaction
                if automatic_resume_token is not None
                else None
            ),
            stlink_replacement_argument=None,
        )

    if not execute:
        if (
            resume_token is not None
            or resume_transaction_argument is not None
            or replacement is not None
        ):
            raise local.LocalWebConfigError(
                "resume/replacement options require --execute"
            )
        manifest = local.load_verified_artifact_manifest(state_dir)
        source_plan = build_flash_plan(state_dir, manifest)
        _print_plan(manifest, source_plan)
        print(
            "  Selected ST-Link: "
            f"{_describe_stlink_binding(stlink_serial)}"
        )
        if not probe_target_only:
            print(
                "DRY RUN complete: no target was opened and no hardware was changed."
            )
            print(
                "Run once with --probe-target to read the selected STM32 UID before "
                "using --execute."
            )
            return source_plan
        # UID is unknown in probe-only mode. This per-account global OpenOCD
        # lock prevents the probe/reset from interleaving with any transaction
        # run by this tool, which holds it for its complete hardware lifetime.
        with _openocd_lock(), _stlink_lock(stlink_serial):
            target = probe_target_identity(
                openocd,
                stlink_serial,
                run_after=True,
            )
            print("TARGET PROBE complete: no Flash or option bytes were changed.")
            print(
                "Execute only after reviewing the identifiers, with "
                f"--confirm-target-uid {target.uid}"
            )
            return source_plan

    if confirmed_device_id is None:
        raise local.LocalWebConfigError(
            "--execute requires --confirm-device-id"
        )
    if confirmed_target_uid is None:
        raise local.LocalWebConfigError(
            "--execute requires --confirm-target-uid from --probe-target"
        )
    expected_target_uid = _normalize_target_uid(confirmed_target_uid)

    def validate_confirmed_device(manifest: dict[str, Any]) -> None:
        if str(confirmed_device_id).lower() != str(
            manifest.get("deviceId", "")
        ).lower():
            raise local.LocalWebConfigError(
                "--execute requires --confirm-device-id matching the artifact transaction"
            )

    def validate_physical_target(
        manifest: dict[str, Any],
        target: TargetIdentity,
    ) -> None:
        if not secrets.compare_digest(expected_target_uid, target.uid):
            raise local.LocalWebConfigError(
                "confirmed STM32 UID does not match the selected physical target"
            )
        qualification = manifest.get("siliconRevisionQualification")
        if not isinstance(qualification, dict):
            raise local.LocalWebConfigError(
                "silicon revision qualification is missing"
            )
        if qualification.get("enabled"):
            expected_revision = qualification.get("stm32RevisionId")
            if (
                not isinstance(expected_revision, int)
                or target.revision_id != expected_revision
            ):
                raise local.LocalWebConfigError(
                    "connected STM32 REV_ID does not match the explicitly qualified build"
                )

    # The user-confirmed UID determines the first hardware lock. No probe,
    # halt, reset, erase, or write occurs before this lock is acquired.
    with _uid_lock(expected_target_uid):
        if resume_transaction_argument is not None:
            if resume_token is None:
                raise local.LocalWebConfigError(
                    "--resume-transaction requires --resume-token"
                )
            (
                transaction_dir,
                state,
                manifest,
                plan,
            ) = _load_resume_transaction(
                state_dir,
                resume_transaction_argument,
                resume_token,
                expected_target_uid,
            )
            validate_confirmed_device(manifest)
            transaction_id = str(state["transactionId"])
            _reject_other_active_transactions(
                state_dir,
                expected_target_uid,
                transaction_id,
            )
            needs_replacement = _replacement_required(
                transaction_dir,
                state,
                stlink_serial,
                replacement,
            )
            _print_plan(manifest, plan)
            print(
                "  Selected ST-Link: "
                f"{_describe_stlink_binding(stlink_serial)}"
            )
            print(f"  Resuming transaction: {transaction_id}")
            with _openocd_lock(), _stlink_lock(stlink_serial):
                target = probe_target_identity(
                    openocd,
                    stlink_serial,
                    run_after=False,
                )
                validate_physical_target(manifest, target)
                if needs_replacement:
                    assert replacement is not None
                    _record_stlink_replacement(
                        transaction_dir / "transaction.json",
                        state,
                        replacement,
                        resume_token,
                    )
                    print(
                        "Recorded explicit ST-Link replacement: "
                        f"{replacement[0]} -> {replacement[1]}"
                    )
                return _execute_prepared_transaction(
                    transaction_dir,
                    manifest,
                    plan,
                    openocd,
                    target,
                    resume_token,
                    issued_resume_token=None,
                )

        if resume_token is not None:
            raise local.LocalWebConfigError(
                "--resume-token requires --resume-transaction"
            )
        if replacement is not None:
            raise local.LocalWebConfigError(
                "ST-Link replacement is valid only for an explicit resume transaction"
            )
        manifest = local.load_verified_artifact_manifest(state_dir)
        source_plan = build_flash_plan(state_dir, manifest)
        validate_confirmed_device(manifest)
        _print_plan(manifest, source_plan)
        print(
            "  Selected ST-Link: "
            f"{_describe_stlink_binding(stlink_serial)}"
        )
        bundle_digest = _artifact_bundle_digest(manifest, source_plan)
        transaction_id = f"{expected_target_uid}-{bundle_digest}"
        _reject_other_active_transactions(
            state_dir,
            expected_target_uid,
            transaction_id,
        )
        existing_transaction = (
            state_dir / "flash-transactions" / transaction_id
        )
        if existing_transaction.exists():
            raise local.LocalWebConfigError(
                "a transaction already exists; resume it explicitly with "
                f"--resume-transaction {transaction_id} and its token"
            )
        with _openocd_lock(), _stlink_lock(stlink_serial):
            target = probe_target_identity(
                openocd,
                stlink_serial,
                run_after=False,
            )
            validate_physical_target(manifest, target)
            return _create_and_execute_transaction(
                state_dir,
                manifest,
                source_plan,
                openocd,
                target,
            )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Safely hand local WebConfig artifacts to an STM32 target"
    )
    parser.add_argument(
        "--state-dir",
        type=Path,
        default=local.DEFAULT_STATE_DIR,
        help="local WebConfig state containing the verified artifact bundle",
    )
    parser.add_argument(
        "--openocd",
        type=Path,
        help=(
            "absolute path to OpenOCD 0.12+; --simple-execute can discover "
            "the known installation automatically"
        ),
    )
    parser.add_argument(
        "--stlink-serial",
        help=(
            "exact 24-hex-character ST-Link serial; optional for "
            "--simple-execute when only one ST-Link is connected"
        ),
    )
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument(
        "--dry-run",
        action="store_true",
        help="validate and print the plan without opening the target (default)",
    )
    mode.add_argument(
        "--probe-target",
        action="store_true",
        help="read DEV_ID/REV_ID/96-bit UID through the selected ST-Link; no writes",
    )
    mode.add_argument(
        "--execute",
        action="store_true",
        help="perform the destructive STM32 write transaction after preflight",
    )
    mode.add_argument(
        "--simple-execute",
        action="store_true",
        help=(
            "laboratory first-flash convenience: discover UID automatically, "
            "then use the same protected write transaction"
        ),
    )
    parser.add_argument(
        "--confirm-device-id",
        help="exact artifact device ID; mandatory with --execute",
    )
    parser.add_argument(
        "--confirm-target-uid",
        help="exact 96-bit STM32 UID printed by --probe-target; mandatory with --execute",
    )
    parser.add_argument(
        "--resume-token",
        help="explicit token for resuming the matching interrupted transaction",
    )
    parser.add_argument(
        "--resume-transaction",
        help=(
            "exact UID-bundle transaction ID; resumes only its authenticated "
            "private staging snapshot"
        ),
    )
    parser.add_argument(
        "--confirm-stlink-replacement",
        metavar="OLD_SERIAL:NEW_SERIAL",
        help=(
            "explicit audited probe replacement for a resume transaction; "
            "NEW_SERIAL must equal --stlink-serial"
        ),
    )
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        if (
            args.resume_token is not None
            or args.resume_transaction is not None
            or args.confirm_stlink_replacement is not None
        ) and not args.execute:
            raise local.LocalWebConfigError(
                "resume/replacement options are valid only together with --execute"
            )
        if (args.resume_token is None) != (args.resume_transaction is None):
            raise local.LocalWebConfigError(
                "--resume-token and --resume-transaction must be provided together"
            )
        if (
            args.confirm_stlink_replacement is not None
            and args.resume_transaction is None
        ):
            raise local.LocalWebConfigError(
                "--confirm-stlink-replacement requires --resume-transaction"
            )
        flash_stm32(
            args.state_dir,
            args.openocd,
            execute=args.execute or args.simple_execute,
            probe_target_only=args.probe_target,
            simple_execute=args.simple_execute,
            stlink_serial_argument=args.stlink_serial,
            confirmed_device_id=args.confirm_device_id,
            confirmed_target_uid=args.confirm_target_uid,
            resume_token=args.resume_token,
            resume_transaction_argument=args.resume_transaction,
            stlink_replacement_argument=args.confirm_stlink_replacement,
        )
        return 0
    except (local.LocalWebConfigError, OSError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
