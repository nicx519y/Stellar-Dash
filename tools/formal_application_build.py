"""Build/check formally signed, unlocked application artifacts; never flash."""

import argparse
import hashlib
import json
import re
import sys
from datetime import datetime, timezone
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS))
import local_firmware_draft as draft
import webconfig_local as local
import webconfig_flash as flash

DEFAULT_STATE = TOOLS.parent / ".hbox" / "webconfig-formal"


def reject_pending_transactions(state_dir):
    # Changing state directories must not bypass an interrupted old transaction.
    for path in (state_dir / "flash-transactions").glob("*/transaction.json"):
        state = json.loads(path.read_text(encoding="utf-8"))
        if state.get("status") in flash.BLOCKING_TRANSACTION_STATUSES:
            raise RuntimeError(f"Resolve the existing flash transaction before migrating/rebuilding: {path}")


def check_artifacts(state_dir, slot, server):
    reject_pending_transactions(local.DEFAULT_STATE_DIR)
    manifest = local.load_verified_artifact_manifest(state_dir)
    if (manifest.get("bootSecurityMode") != "unlocked-development" or
            manifest.get("requiresManualLifecycleProvisioning") is not False or
            manifest.get("requiredLifecycle") != [] or
            local.artifact_target_slot(manifest) != slot):
        raise RuntimeError("Formal application artifacts must be unlocked and match the requested slot")
    paths = local._state_paths(state_dir)
    public = local.load_public_key(paths["firmware_public"])
    record = manifest.get("releaseSigning")
    expected = {"server": server, "publicKeySha256": hashlib.sha256(public).hexdigest()}
    if record != expected:
        raise RuntimeError("Missing/mismatched formal release signing record; rebuild application artifacts")
    header = paths["trust_header"].read_text(encoding="utf-8")
    match = re.search(r"static const uint8_t hbox_firmware_release_public_key\[65\]\s*=\s*\{([^}]*)\}", header)
    if (not match or bytes(int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]{2})", match[1])) != public or
            not re.search(r"#define\s+HBOX_FIRMWARE_RELEASE_PUBLIC_KEY_PROVISIONED\s+1u\b", header)):
        raise RuntimeError("Compiled trust header does not match the formal verification key")
    application = paths["artifacts"] / f"application-slot-{slot.lower()}.bin"
    if public not in application.read_bytes():
        raise RuntimeError("Formal verification key is absent from the application image")
    draft.verify_server_release_key(server, public)
    print(f"Formal application slot {slot} verified; public-key SHA-256: {expected['publicKeySha256']}", flush=True)
    return manifest


def build_artifacts(state_dir, slot, server, jobs):
    if state_dir.resolve() == local.DEFAULT_STATE_DIR.resolve():
        raise RuntimeError("Formal output must be separate from the local development PKI")
    key = draft.resolve_release_key(server)
    public = draft.export_uncompressed_public_key(key)
    draft.verify_server_release_key(server, public)
    reject_pending_transactions(local.DEFAULT_STATE_DIR)
    reject_pending_transactions(state_dir)
    if not (local.DEFAULT_STATE_DIR / "manifest.json").is_file():
        local.initialize_local_state(local.DEFAULT_STATE_DIR)
    source, target = local._state_paths(local.DEFAULT_STATE_DIR), local._state_paths(state_dir)
    # Copy only material needed by the existing build/verification contract.
    # The formal signing private key remains at its configured external path.
    for name in ("manifest", "manufacturer_public", "trust_header", "device_certificate",
                 "identity_slot", "security_record"):
        local._atomic_write(target[name], source[name].read_bytes(), private=True)
    draft.prepare_release_signing_state(state_dir, key, public, copy_private_key=False)
    log = state_dir / "logs" / (datetime.now(timezone.utc).strftime("build-%Y%m%d-%H%M%S-%f.log"))
    log.parent.mkdir(parents=True, exist_ok=True)
    draft.run_stage("formal-application-build", [sys.executable, str(TOOLS / "webconfig_local.py"),
                    "--state-dir", str(state_dir), "build", "--slot", slot, "--skip-web",
                    "--jobs", str(jobs), "--unlocked-development", "--signing-key", str(key)], log, 600)
    manifest = local.load_verified_artifact_manifest(state_dir)
    manifest["releaseSigning"] = {"server": server, "publicKeySha256": hashlib.sha256(public).hexdigest()}
    local._atomic_write_text(target["artifacts"] / "artifact-manifest.json",
                             json.dumps(manifest, indent=2) + "\n", private=True)
    return check_artifacts(state_dir, slot, server)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--state-dir", type=Path, default=DEFAULT_STATE)
    parser.add_argument("--slot", choices=("A", "B"), required=True)
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--verify-only", action="store_true")
    args = parser.parse_args(argv)
    if not 1 <= args.jobs <= 64:
        parser.error("--jobs must be between 1 and 64")
    state_dir = args.state_dir.expanduser().resolve()
    try:
        if args.verify_only:
            check_artifacts(state_dir, args.slot, draft.DEFAULT_ADMIN_SERVER)
        else:
            build_artifacts(state_dir, args.slot, draft.DEFAULT_ADMIN_SERVER, args.jobs)
        print("No hardware was flashed; no protection bits or locking state were changed.")
        return 0
    except (OSError, RuntimeError, ValueError) as exc:
        print(f"Formal application preflight failed: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
