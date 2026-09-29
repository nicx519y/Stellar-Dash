#!/usr/bin/env python3
"""Build a signed, unlocked XORA v2 bundle and import it into local admin only."""

import argparse
import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import zipfile
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
STATE = ROOT / ".hbox" / "webconfig-local"
HEADER = ROOT / "common" / "release_build_identity.h"
sys.path.insert(0, str(ROOT / "tools"))
from firmware_signing import export_uncompressed_public_key  # noqa: E402
from release import (  # noqa: E402
    FIRMWARE_HASH_OFFSET,
    FIRMWARE_SECURITY_VERSION,
    FIRMWARE_SIGNATURE_ALGORITHM_OFFSET,
    FIRMWARE_SIGNATURE_OFFSET,
    SIGNED_METADATA_FILENAME,
    create_metadata_binary,
    validate_stm32_ota_manifest,
    validate_signed_metadata_matches_manifest,
    validate_stm32_ota_package,
)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def run_stage(name, command, log_file, timeout=600):
    print(f"[{name}] start; log: {log_file}", flush=True)
    started = time.monotonic()
    with log_file.open("wb") as log:
        process = subprocess.Popen(command, cwd=ROOT, stdout=log,
                                   stderr=subprocess.STDOUT,
                                   creationflags=subprocess.CREATE_NEW_PROCESS_GROUP if os.name == "nt" else 0,
                                   start_new_session=os.name != "nt")
        try:
            code = process.wait(timeout=timeout)
        except subprocess.TimeoutExpired as exc:
            if os.name == "nt":
                subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                               timeout=15, check=False)
            else:
                import signal
                os.killpg(process.pid, signal.SIGTERM)
            process.wait(timeout=15)
            raise RuntimeError(f"{name} timed out after {timeout}s; see {log_file}") from exc
    print(f"[{name}] exit={code} elapsed={time.monotonic() - started:.1f}s", flush=True)
    if code:
        raise RuntimeError(f"{name} failed; see {log_file}")


def release_identity(data, component):
    magic = b"XORAFW2\0"
    matches = []
    offset = data.find(magic)
    while offset >= 0:
        if offset + 121 <= len(data) and struct.unpack_from("<III", data, offset + 8) == (component, 1, 1):
            version = data[offset + 24:offset + 56].split(b"\0", 1)[0].decode("ascii")
            build_id = data[offset + 56:offset + 121].split(b"\0", 1)[0].decode("ascii")
            config = struct.unpack_from("<I", data, offset + 20)[0]
            matches.append((version, build_id, config))
        offset = data.find(magic, offset + 1)
    if len(matches) != 1 or matches[0][1] == "unidentified":
        raise RuntimeError("Executable has no unique prepared XORA release identity")
    return matches[0]


def make_stm32_package(slot, app, adc, version, key, public, trust_hash, dest):
    app_data, adc_data = app.read_bytes(), adc.read_bytes()
    identity = release_identity(app_data, 1)
    if identity[0] != version or identity[2] != 34:
        raise RuntimeError(f"STM32 {slot} release identity/config does not match {version}/34")
    app_address = 0x90000000 if slot == "A" else 0x902B0000
    web_address = 0x90100000 if slot == "A" else 0x903B0000
    adc_address = 0x90280000 if slot == "A" else 0x90530000
    components = [
        {"name": "application", "file": "application.bin", "address": f"0x{app_address:08X}",
         "size": len(app_data), "sha256": digest(app_data), "file_type": "bin", "active": True},
        {"name": "webresources", "file": "", "address": f"0x{web_address:08X}",
         "size": 0, "sha256": "0" * 64, "file_type": "none", "active": False},
        {"name": "adc_mapping", "file": "adc_mapping.bin", "address": f"0x{adc_address:08X}",
         "size": len(adc_data), "sha256": digest(adc_data), "file_type": "bin", "active": True},
    ]
    now = datetime.now(timezone.utc)
    build_date = now.strftime("%Y-%m-%d %H:%M:%S")
    build_timestamp = int(now.timestamp())
    metadata = create_metadata_binary(version, slot, build_date, components,
                                      signing_key=key, security_version=FIRMWARE_SECURITY_VERSION,
                                      webresources_optional=True, build_timestamp=build_timestamp)
    manifest = {
        "version": version, "slot": slot, "build_date": build_date,
        "build_timestamp": build_timestamp, "hardware_version": "2.0.0",
        "hardware_version_code": 0x00020000, "ota_scope": "STM32_ONLY",
        "ch585_update": "MANUAL_INDEPENDENT_FLASH", "security_version": FIRMWARE_SECURITY_VERSION,
        "webresources_optional": True, "trust_bundle_sha256": trust_hash,
        "components": components,
        "signature_algorithm": struct.unpack_from("<I", metadata, FIRMWARE_SIGNATURE_ALGORITHM_OFFSET)[0],
        "firmware_hash": metadata[FIRMWARE_HASH_OFFSET:FIRMWARE_HASH_OFFSET + 32].hex(),
        "signature": metadata[FIRMWARE_SIGNATURE_OFFSET:FIRMWARE_SIGNATURE_OFFSET + 64].hex(),
        "metadata": {"file": SIGNED_METADATA_FILENAME, "size": len(metadata), "sha256": digest(metadata)},
    }
    validate_stm32_ota_manifest(manifest)
    validate_signed_metadata_matches_manifest(metadata, manifest, public)
    with zipfile.ZipFile(dest, "x", zipfile.ZIP_DEFLATED) as archive:
        archive.writestr("manifest.json", json.dumps(manifest, separators=(",", ":")))
        archive.writestr("metadata.bin", metadata)
        archive.writestr("application.bin", app_data)
        archive.writestr("adc_mapping.bin", adc_data)
    validate_stm32_ota_package(dest, public)
    return identity


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", default="1.0.0")
    parser.add_argument("--token-file", type=Path, default=STATE / "firmware-manage-token.txt")
    args = parser.parse_args()
    if not all((STATE / item).is_file() for item in (
        "manifest.json", "pki/firmware-release-private.pem", "pki/firmware-release-public.pem",
        "public/hbox-local-trust.h", "device/device-certificate.bin")):
        raise RuntimeError("Local WebConfig state/PKI is incomplete")
    key = STATE / "pki" / "firmware-release-private.pem"
    public = export_uncompressed_public_key(key)
    trust_hash = digest((STATE / "public" / "hbox-local-trust.h").read_bytes())
    timestamp = datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S")
    work = ROOT / ".hbox" / "firmware-drafts" / f"XORA-{args.version}-{timestamp}"
    work.mkdir(parents=True, exist_ok=False)
    source = work / "source"
    source.mkdir()
    original_header = HEADER.read_bytes()
    try:
        run_stage("identity", [sys.executable, str(ROOT / "tools" / "prepare_release_identity.py"),
                               "--version", args.version], work / "identity.log", 60)
        with tempfile.TemporaryDirectory(prefix="xora-release-build-", dir=ROOT / ".hbox") as temporary:
            isolated = Path(temporary)
            for directory in ("pki", "public", "device"):
                shutil.copytree(STATE / directory, isolated / directory)
            shutil.copy2(STATE / "manifest.json", isolated / "manifest.json")
            for slot in ("A", "B"):
                run_stage(f"build-{slot}", [sys.executable, str(ROOT / "tools" / "webconfig_local.py"),
                           "--state-dir", str(isolated), "build", "--unlocked-development",
                           "--skip-web", "--slot", slot], work / f"build-{slot}.log")
                artifacts = isolated / "artifacts"
                shutil.copy2(artifacts / f"application-slot-{slot.lower()}.bin", source / f"application-{slot}.bin")
                shutil.copy2(artifacts / f"adc-mapping-slot-{slot.lower()}.bin", source / f"adc-{slot}.bin")
                if slot == "B":
                    shutil.copy2(artifacts / "ch585-maintenance.bin", source / "RF_PHY_Hop_TX.bin")
    finally:
        HEADER.write_bytes(original_header)

    identities = []
    for slot in ("A", "B"):
        identities.append(make_stm32_package(slot, source / f"application-{slot}.bin",
                                             source / f"adc-{slot}.bin", args.version,
                                             key, public, trust_hash, source / f"stm32-{slot}.zip"))
    tx = (source / "RF_PHY_Hop_TX.bin").read_bytes()
    if len(tx) <= 4096 or len(tx) % 4 or len(tx) > 0x70000:
        raise RuntimeError("TX combined image boundary is invalid")
    tx_identity = release_identity(tx[4096:], 2)
    if any(item[:2] != identities[0][:2] for item in [identities[1], tx_identity]):
        raise RuntimeError("STM32 A/B and TX do not share version/build identity")
    build_id = identities[0][1]
    common = {"hardwareVersion": "2.0.0", "bootSecurityMode": "unlocked-development",
              "requiresManualLifecycleProvisioning": False}
    artifacts = [
        {"component": "stm32", "slot": slot, "file": f"stm32-{slot}.zip",
         "version": args.version, "buildId": build_id, **common}
        for slot in ("A", "B")
    ] + [{"component": "tx", "file": "RF_PHY_Hop_TX.bin", "version": args.version,
          "buildId": build_id, "imageFormat": "ch585-tx-combined", **common}]
    manifest = {
        "schemaVersion": 2, "buildId": build_id, "product": "XORA",
        "deviceModel": "STM32H750_HBOX", "hardwareVersion": "2.0.0",
        "version": args.version, "bootSecurityMode": "unlocked-development",
        "requiresManualLifecycleProvisioning": False,
        "compatibility": {"stm32Tx": f"STM32/TX {args.version}; local acceptance pending",
                          "txRx": "RX compatibility requires local acceptance"},
        "install": {"protocol": 1, "order": "tx-then-stm32",
                    "configRead": {"min": 34, "max": 34}, "configWrite": 34,
                    "stm32Maintenance": {"min": 1, "max": 1},
                    "txMaintenance": {"min": 1, "max": 1}},
        "artifacts": artifacts,
    }
    source_path = source / "release-source.json"
    source_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    token_file = args.token_file.expanduser().resolve()
    command = ["node", str(ROOT / "server" / "scripts" / "create-firmware-draft.js"),
               "--source", str(source_path), "--signing-key", str(key),
               "--out-dir", str(work / "package"), "--initial-release"]
    if token_file.is_file():
        command += ["--service-token-file", str(token_file)]
    else:
        command += ["--dry-run"]
        print(f"No local firmware.manage token file at {token_file}; packaging only.", flush=True)
    run_stage("bundle-and-draft", command, work / "bundle-and-draft.log", 180)
    print(f"Release workspace: {work}")
    print((work / "bundle-and-draft.log").read_text(encoding="utf-8"))
    if not token_file.is_file():
        print("Local draft was not uploaded: create a firmware.manage token in local admin and save it to the path above.")
        return 2
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"Local release failed: {exc}", file=sys.stderr)
        raise SystemExit(1)
