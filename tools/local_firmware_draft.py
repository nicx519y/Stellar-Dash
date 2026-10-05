#!/usr/bin/env python3
"""Build a signed, unlocked XORA v2 bundle; upload a draft to the XORA admin service."""

import argparse
import base64
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import urllib.parse
import urllib.request
import zipfile
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
STATE = ROOT / ".hbox" / "webconfig-local"
HEADER = ROOT / "common" / "release_build_identity.h"
DEFAULT_ADMIN_SERVER = "https://manager.st-dash.com"
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


def read_config_version(header=ROOT / "application" / "Inc" / "system" / "board_cfg.h"):
    """Use the firmware's declared format; reject missing or nonliteral definitions."""
    values = re.findall(
        r"^[ \t]*#define[ \t]+CONFIG_VERSION[ \t]+(?:\(uint32_t\)[ \t]*)?"
        r"(0[xX][0-9a-fA-F]+|[0-9]+)[uUlL]*[ \t]*(?://[^\n]*)?$",
        header.read_text(encoding="utf-8"), re.MULTILINE,
    )
    if len(values) != 1:
        raise RuntimeError(f"Expected one literal CONFIG_VERSION in {header}")
    value = int(values[0], 16 if values[0].lower().startswith("0x") else 10)
    if not 0 < value <= 0xffffffff:
        raise RuntimeError(f"CONFIG_VERSION must be a positive uint32 in {header}")
    return value


def make_install_contract(config_version):
    # The installer preserves the exact format; migration needs separate acceptance.
    return {"protocol": 2, "order": "tx-then-stm32",
            "configRead": {"min": config_version, "max": config_version},
            "configWrite": config_version,
            "stm32Maintenance": {"min": 2, "max": 2},
            "txMaintenance": {"min": 2, "max": 2}}


def resolve_release_key(server, explicit=None, *, state=STATE,
                        readiness=ROOT / ".hbox" / "deploy" / "server-readiness.json"):
    origin = urllib.parse.urlsplit(server)
    loopback = origin.hostname in ("localhost", "127.0.0.1", "::1")
    if (origin.scheme not in ("http", "https") or (not loopback and origin.scheme != "https") or
            origin.username or origin.password or origin.path not in ("", "/") or origin.query or origin.fragment):
        raise RuntimeError("Draft server must be an HTTPS origin (HTTP is allowed for loopback)")
    if explicit is not None:
        key = explicit.expanduser().resolve()
    elif loopback:
        key = state / "pki" / "firmware-release-private.pem"
    else:
        deployment = json.loads(readiness.read_text(encoding="utf-8")) if readiness.is_file() else {}
        path = deployment.get("private_key_local_path")
        if origin.hostname not in deployment.get("domains", []) or not isinstance(path, str) or not path:
            raise RuntimeError("No release signing key configured for this remote server; use --signing-key "
                               "with the private key matching its release verification key")
        key = Path(path).expanduser().resolve()
    if not key.is_file():
        raise RuntimeError(f"Release signing key file not found: {key}")
    return key


def verify_server_release_key(server, public):
    """Public, read-only preflight. Never send a token or private key."""
    url = server.rstrip("/") + "/api/firmware-releases/verification-key"
    try:
        with urllib.request.urlopen(url, timeout=15) as response:
            result = json.loads(response.read(16385))
        jwk = result["data"]
        if result.get("success") is not True or jwk.get("kty") != "EC" or jwk.get("crv") != "P-256":
            raise ValueError("Expected a P-256 verification key")
        coordinates = [base64.urlsafe_b64decode(jwk[name] + "=" * (-len(jwk[name]) % 4)) for name in ("x", "y")]
        if any(len(value) != 32 for value in coordinates):
            raise ValueError("Invalid verification key coordinates")
        remote = b"\x04" + b"".join(coordinates)
    except Exception as exc:
        raise RuntimeError(f"Cannot verify the draft server's release key before building: {exc}") from exc
    if remote != public:
        raise RuntimeError("Release signing key does not match the draft server's verification key; "
                           "select the matching --signing-key. No build or upload was started")
    print("Draft server release verification key matches the selected signing key.", flush=True)


def verify_draft_version_available(server, token_file, version):
    """Reject existing versions before compiling; the server keeps the final guard."""
    token = token_file.read_text(encoding="utf-8").strip()
    if not re.fullmatch(r"stsvc_[A-Za-z0-9_-]{43}", token):
        raise RuntimeError("Invalid firmware.manage service token file; no build or upload was started")
    offset = 0
    deadline = time.monotonic() + 30
    while True:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise RuntimeError("Draft version preflight timed out; no build or upload was started")
        query = urllib.parse.urlencode({"query": version, "limit": 100, "offset": offset})
        request = urllib.request.Request(server.rstrip("/") + "/api/admin/firmware/releases?" + query,
                                         headers={"Authorization": "Bearer " + token})
        try:
            with urllib.request.urlopen(request, timeout=min(15, remaining)) as response:
                result = json.loads(response.read(4 * 1024 * 1024 + 1))
            data = result["data"]
            items, total = data["items"], data["total"]
            if (result.get("success") is not True or not isinstance(items, list) or
                    type(total) is not int or total < 0 or len(items) > 100 or
                    (not items and offset < total)):
                raise ValueError("Invalid release catalog response")
        except Exception as exc:
            raise RuntimeError(f"Cannot check the draft version before building: {exc}") from exc
        for release in items:
            manifest = release.get("manifest", {})
            if (manifest.get("version") == version and manifest.get("deviceModel") == "STM32H750_HBOX" and
                    manifest.get("hardwareVersion") == "2.0.0"):
                raise RuntimeError(f"XORA {version} already exists on {server} "
                                   f"(status={release.get('status')}, id={release.get('id')}). "
                                   f"Review it at {server.rstrip('/')}/admin/firmware/; "
                                   "use a new --version for another build. No build or upload was started")
        offset += len(items)
        if offset >= total:
            break
    print(f"Draft version preflight: XORA {version} is available.", flush=True)


def prepare_release_signing_state(isolated, key, public, *, copy_private_key=True):
    """Change only the release signer in the disposable build state."""
    header = isolated / "public" / "hbox-local-trust.h"
    original = header.read_text(encoding="utf-8")
    pattern = r"(static const uint8_t hbox_firmware_release_public_key\[65\]\s*=\s*\{)[^}]*(\};)"
    if len(re.findall(pattern, original)) != 1 or not re.search(
            r"#define\s+HBOX_FIRMWARE_RELEASE_PUBLIC_KEY_PROVISIONED\s+1u\b", original):
        raise RuntimeError("Build trust header has no unique provisioned release public key")
    replacement = "\n    " + ", ".join(f"0x{value:02x}" for value in public) + "\n"
    header.write_text(re.sub(pattern, lambda match: match[1] + replacement + match[2], original), encoding="utf-8")
    if copy_private_key:
        shutil.copy2(key, isolated / "pki" / "firmware-release-private.pem")
    subprocess.run(["openssl", "pkey", "-in", str(key), "-pubout", "-out",
                    str(isolated / "pki" / "firmware-release-public.pem")],
                   check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30)
    trust_hash = digest(header.read_bytes())
    manifest_path = isolated / "manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    manifest["trustHeaderSha256"] = trust_hash
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return trust_hash


def save_local_token(secret, state_dir):
    """Atomically replace the local firmware token, without retaining a backup."""
    secret = secret.strip()
    if not re.fullmatch(r"stsvc_[A-Za-z0-9_-]{43}", secret):
        raise RuntimeError("Invalid service token; copy the local save script from the token creation dialog again")
    state_dir.mkdir(parents=True, exist_ok=True)
    token_file = state_dir / "firmware-manage-token.txt"
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", dir=state_dir,
                                         prefix="firmware-token-", suffix=".tmp", delete=False) as output:
            temporary = Path(output.name)
            output.write(secret)
            output.write("\n")
        os.replace(temporary, token_file)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
    return token_file


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
        if name.startswith("bundle-and-"):
            for line in reversed(log_file.read_text(encoding="utf-8", errors="replace").splitlines()):
                if line.startswith("Release draft failed: "):
                    raise RuntimeError(f"{name} failed: {line.removeprefix('Release draft failed: ')[:2000]}; "
                                       f"see {log_file}")
        raise RuntimeError(f"{name} failed; see {log_file}")


def release_identity(data, component):
    magic = b"XORAFW2\0"
    matches = []
    offset = data.find(magic)
    while offset >= 0:
        if offset + 121 <= len(data) and struct.unpack_from("<III", data, offset + 8) == (component, 2, 2):
            version = data[offset + 24:offset + 56].split(b"\0", 1)[0].decode("ascii")
            build_id = data[offset + 56:offset + 121].split(b"\0", 1)[0].decode("ascii")
            config = struct.unpack_from("<I", data, offset + 20)[0]
            matches.append((version, build_id, config))
        offset = data.find(magic, offset + 1)
    if len(matches) != 1 or matches[0][1] == "unidentified":
        raise RuntimeError("Executable has no unique prepared XORA release identity")
    return matches[0]


def make_stm32_package(slot, app, adc, version, key, public, trust_hash, dest, *, config_version):
    app_data, adc_data = app.read_bytes(), adc.read_bytes()
    identity = release_identity(app_data, 1)
    if identity[0] != version or identity[2] != config_version:
        raise RuntimeError(f"STM32 {slot} release identity/config does not match {version}/{config_version}; "
                           f"executable declares {identity[0]}/{identity[2]}")
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


def final_stage_command(source_path, key, work, version, token_file, since=None, *, no_upload=False,
                        server=DEFAULT_ADMIN_SERVER):
    package = work / "package"
    command = [
        "node", str(ROOT / "server" / "scripts" / "create-firmware-draft.js"),
        "--source", str(source_path), "--signing-key", str(key),
        "--out-dir", str(package), "--allow-worktree",
        "--local-history", str(ROOT / ".hbox" / "firmware-drafts"),
        "--server", server,
    ]
    offline = no_upload or not token_file.is_file()
    if offline:
        command += ["--dry-run"]
    else:
        command += ["--service-token-file", str(token_file)]
    if since:
        command += ["--since", since]
    elif version == "1.0.0":
        command += ["--initial-release"]
    return "bundle-and-notes" if offline else "bundle-and-draft", command


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", default="1.0.0")
    parser.add_argument("--server", default=DEFAULT_ADMIN_SERVER,
                        help="admin service origin (default: %(default)s); remote servers require HTTPS")
    parser.add_argument("--signing-key", type=Path,
                        help="release private key matching the target server; defaults to deployment config for remote, local PKI for loopback")
    token_options = parser.add_mutually_exclusive_group()
    token_options.add_argument("--token-file", type=Path, default=STATE / "firmware-manage-token.txt", help="override the local token file for this run")
    token_options.add_argument("--save-token", action="store_true", help="replace the local firmware token from stdin and exit without building or uploading")
    parser.add_argument("--since", help="previous release Git commit or tag for later versions")
    parser.add_argument("--no-upload", action="store_true", help="build package and notes without contacting admin")
    args = parser.parse_args(argv)
    if args.save_token:
        if (args.version != "1.0.0" or args.since is not None or args.no_upload or
                args.server != DEFAULT_ADMIN_SERVER or args.signing_key is not None):
            parser.error("--save-token is a standalone setup command; do not combine it with build options")
        save_local_token(sys.stdin.read(256), STATE)
        print("Local firmware token saved. Previous local token replaced; future packages will use the new token automatically.")
        print("No package was built or uploaded. Server tokens were not revoked.")
        return 0
    token_file = args.token_file.expanduser().resolve()
    config_version = read_config_version()
    print(f"Firmware configuration version: {config_version}", flush=True)
    if args.no_upload:
        print("Upload disabled by --no-upload.", flush=True)
    elif token_file.is_file():
        print(f"Draft import server: {args.server}", flush=True)
        print(f"Draft import will use token file: {token_file}", flush=True)
    else:
        print(f"Token file not found: {token_file}; packaging only. Copy the local save script from the token creation dialog to enable draft import.", flush=True)
    if not all((STATE / item).is_file() for item in (
        "manifest.json", "pki/firmware-release-private.pem", "pki/firmware-release-public.pem",
        "public/hbox-local-trust.h", "device/device-certificate.bin")):
        raise RuntimeError("Local WebConfig state/PKI is incomplete")
    key = resolve_release_key(args.server, args.signing_key)
    public = export_uncompressed_public_key(key)
    print(f"Release signing key: {key}", flush=True)
    if not args.no_upload and token_file.is_file():
        verify_server_release_key(args.server, public)
        verify_draft_version_available(args.server, token_file, args.version)
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
            trust_hash = prepare_release_signing_state(isolated, key, public)
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
                                             key, public, trust_hash, source / f"stm32-{slot}.zip",
                                             config_version=config_version))
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
        "install": make_install_contract(config_version),
        "artifacts": artifacts,
    }
    source_path = source / "release-source.json"
    source_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    stage, command = final_stage_command(source_path, key, work, args.version, token_file, args.since,
                                         no_upload=args.no_upload, server=args.server)
    if stage == "bundle-and-notes":
        print("Creating the package and release notes locally; nothing will be uploaded.", flush=True)
    run_stage(stage, command, work / f"{stage}.log", 180)
    print(f"Release workspace: {work}")
    print((work / f"{stage}.log").read_text(encoding="utf-8"))
    if stage == "bundle-and-notes":
        print(f"Signed package: {work / 'package' / f'XORA-{args.version}-release.zip'}")
        print(f"Package and Markdown notes are ready. Nothing was uploaded; import the ZIP and .md in {args.server}/admin/firmware/, or provide a firmware.manage token issued by that server.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"Local release failed: {exc}", file=sys.stderr)
        raise SystemExit(1)
