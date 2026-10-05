"""Host-only checks for the local release command selection."""

import struct
import base64
import json
import shutil
import io
import secrets
import contextlib
import tempfile
import subprocess
import unittest
import zipfile
from unittest.mock import Mock, patch
from pathlib import Path

from tools.local_firmware_draft import (
    final_stage_command, release_identity, save_local_token, main,
    read_config_version, make_install_contract, make_stm32_package,
    resolve_release_key, verify_server_release_key, prepare_release_signing_state,
    verify_draft_version_available, run_stage,
)
from tools.firmware_signing import export_uncompressed_public_key
from tools.device_identity_provisioning import load_public_key


class LocalFirmwareDraftCommandTests(unittest.TestCase):
    def test_version_preflight_rejects_same_target_in_every_release_status(self):
        with tempfile.TemporaryDirectory() as directory:
            token = Path(directory) / "token"
            token.write_text("stsvc_" + secrets.token_urlsafe(32))
            for status in ("draft", "published", "withdrawn"):
                release = {"id": "existing", "status": status,
                           "manifest": {"version": "1.0.0", "deviceModel": "STM32H750_HBOX", "hardwareVersion": "2.0.0"}}
                response = io.BytesIO(json.dumps({"success": True, "data": {"items": [release], "total": 1}}).encode())
                with patch("tools.local_firmware_draft.urllib.request.urlopen", return_value=response):
                    with self.assertRaisesRegex(RuntimeError, f"1.0.0 already exists.*status={status}.*new --version"):
                        verify_draft_version_available("https://manager.example.test", token, "1.0.0")

    def test_version_preflight_checks_all_pages_and_ignores_other_targets(self):
        with tempfile.TemporaryDirectory() as directory:
            token = Path(directory) / "token"; token.write_text("stsvc_" + secrets.token_urlsafe(32))
            other = {"manifest": {"version": "1.0.0", "deviceModel": "OTHER", "hardwareVersion": "2.0.0"}}
            target = {"id": "existing", "status": "draft",
                      "manifest": {"version": "1.0.0", "deviceModel": "STM32H750_HBOX", "hardwareVersion": "2.0.0"}}
            def response(items, total):
                return io.BytesIO(json.dumps({"success": True, "data": {"items": items, "total": total}}).encode())
            with patch("tools.local_firmware_draft.urllib.request.urlopen",
                       side_effect=[response([other], 2), response([target], 2)]) as request:
                with self.assertRaisesRegex(RuntimeError, "already exists"):
                    verify_draft_version_available("https://manager.example.test", token, "1.0.0")
                self.assertEqual(request.call_count, 2)
                self.assertIn("offset=1", request.call_args.args[0].full_url)
            with patch("tools.local_firmware_draft.urllib.request.urlopen", return_value=response([other], 1)), \
                    contextlib.redirect_stdout(io.StringIO()):
                verify_draft_version_available("https://manager.example.test", token, "1.0.0")

    def test_version_preflight_fails_closed_on_invalid_token_or_catalog(self):
        with tempfile.TemporaryDirectory() as directory:
            token = Path(directory) / "token"; token.write_text("invalid")
            with patch("tools.local_firmware_draft.urllib.request.urlopen") as request:
                with self.assertRaisesRegex(RuntimeError, "Invalid.*token"):
                    verify_draft_version_available("https://manager.example.test", token, "1.0.0")
                request.assert_not_called()
            token.write_text("stsvc_" + secrets.token_urlsafe(32))
            for data in ({"success": False}, {"success": True, "data": {"items": [], "total": 1}}):
                with patch("tools.local_firmware_draft.urllib.request.urlopen",
                           return_value=io.BytesIO(json.dumps(data).encode())):
                    with self.assertRaisesRegex(RuntimeError, "Cannot check"):
                        verify_draft_version_available("https://manager.example.test", token, "1.0.0")
            with patch("tools.local_firmware_draft.time.monotonic", side_effect=[0, 31]), \
                    patch("tools.local_firmware_draft.urllib.request.urlopen") as request:
                with self.assertRaisesRegex(RuntimeError, "timed out"):
                    verify_draft_version_available("https://manager.example.test", token, "1.0.0")
                request.assert_not_called()

    def test_duplicate_version_stops_main_before_identity_build_or_upload(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory); state = work / "state"
            for name in ("manifest.json", "pki/firmware-release-private.pem", "pki/firmware-release-public.pem",
                         "public/hbox-local-trust.h", "device/device-certificate.bin"):
                file = state / name; file.parent.mkdir(parents=True, exist_ok=True); file.touch()
            token = save_local_token("stsvc_" + secrets.token_urlsafe(32), state)
            header = work / "identity.h"; header.write_bytes(b"original identity")
            with patch("tools.local_firmware_draft.STATE", state), patch("tools.local_firmware_draft.ROOT", work), \
                    patch("tools.local_firmware_draft.HEADER", header), \
                    patch("tools.local_firmware_draft.resolve_release_key", return_value=state / "pki/firmware-release-private.pem"), \
                    patch("tools.local_firmware_draft.export_uncompressed_public_key", return_value=b"test-public"), \
                    patch("tools.local_firmware_draft.verify_server_release_key"), \
                    patch("tools.local_firmware_draft.verify_draft_version_available", side_effect=RuntimeError("already exists")), \
                    patch("tools.local_firmware_draft.run_stage") as stage, contextlib.redirect_stdout(io.StringIO()):
                with self.assertRaisesRegex(RuntimeError, "already exists"):
                    main(["--version", "1.0.0", "--token-file", str(token)])
                stage.assert_not_called()
            self.assertEqual(header.read_bytes(), b"original identity")
            self.assertFalse((work / ".hbox/firmware-drafts").exists())

    def test_upload_stage_surfaces_server_reason_in_outer_error(self):
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / "stage.log"
            def failed_process(*args, **kwargs):
                kwargs["stdout"].write(b"Release draft failed: Server rejected the package: version already exists\n")
                return Mock(wait=Mock(return_value=1))
            with patch("tools.local_firmware_draft.subprocess.Popen", side_effect=failed_process), \
                    contextlib.redirect_stdout(io.StringIO()):
                with self.assertRaisesRegex(RuntimeError, "bundle-and-draft failed:.*version already exists"):
                    run_stage("bundle-and-draft", ["test"], log)

    def test_release_key_selection_keeps_local_and_remote_signers_separate(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            state = work / "state"
            local = state / "pki" / "firmware-release-private.pem"
            local.parent.mkdir(parents=True); local.touch()
            official = work / "official.pem"; official.touch()
            readiness = work / "readiness.json"
            readiness.write_text(json.dumps({"domains": ["manager.example.test"],
                                            "private_key_local_path": str(official)}))
            for server in ("http://localhost:3001", "http://127.0.0.1:3001", "http://[::1]:3001"):
                self.assertEqual(resolve_release_key(server, state=state, readiness=readiness), local)
            server = "https://manager.example.test"
            self.assertEqual(resolve_release_key(server, state=state, readiness=readiness), official)
            self.assertEqual(resolve_release_key(server, official, state=state,
                                                readiness=work / "missing"), official)
            for server in ("https://other.example.test", "http://manager.example.test",
                           "https://manager.example.test/path", "https://user:pass@manager.example.test"):
                with self.assertRaises(RuntimeError):
                    resolve_release_key(server, state=state, readiness=readiness)
            official.unlink()
            with self.assertRaisesRegex(RuntimeError, "not found"):
                resolve_release_key("https://manager.example.test", state=state, readiness=readiness)

    def test_server_key_preflight_rejects_mismatch_missing_key_and_unavailable_server(self):
        public = b"\x04" + bytes(range(64))
        def response(key=public, curve="P-256"):
            coordinate = lambda value: base64.urlsafe_b64encode(value).decode().rstrip("=")
            return io.BytesIO(json.dumps({"success": True, "data": {"kty": "EC", "crv": curve,
                    "x": coordinate(key[1:33]), "y": coordinate(key[33:])}}).encode())
        with patch("tools.local_firmware_draft.urllib.request.urlopen", return_value=response()) as request, \
                contextlib.redirect_stdout(io.StringIO()):
            verify_server_release_key("https://manager.example.test", public)
        request.assert_called_once_with("https://manager.example.test/api/firmware-releases/verification-key", timeout=15)
        with patch("tools.local_firmware_draft.urllib.request.urlopen", return_value=response(b"\x04" + b"x" * 64)):
            with self.assertRaisesRegex(RuntimeError, "does not match"):
                verify_server_release_key("https://manager.example.test", public)
        for result in (response(curve="P-384"), io.BytesIO(b'{"success":true,"data":null}')):
            with patch("tools.local_firmware_draft.urllib.request.urlopen", return_value=result):
                with self.assertRaisesRegex(RuntimeError, "Cannot verify"):
                    verify_server_release_key("https://manager.example.test", public)
        with patch("tools.local_firmware_draft.urllib.request.urlopen", side_effect=TimeoutError("test")):
            with self.assertRaisesRegex(RuntimeError, "Cannot verify"):
                verify_server_release_key("https://manager.example.test", public)

    def test_signing_state_updates_only_isolated_release_trust_and_keypair(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            original = work / "original"
            (original / "public").mkdir(parents=True); (original / "pki").mkdir()
            header = ("#define HBOX_FIRMWARE_RELEASE_PUBLIC_KEY_PROVISIONED 1u\n"
                      "static const uint8_t hbox_firmware_release_public_key[65] = {0u};\n"
                      "// existing manufacturer and authorization trust remains unchanged\n")
            (original / "public" / "hbox-local-trust.h").write_text(header)
            (original / "manifest.json").write_text(json.dumps({"trustHeaderSha256": "old", "productId": "test"}))
            (original / "pki" / "firmware-release-private.pem").write_text("existing-development-key")
            key = work / "official-key.pem"
            subprocess.run(["openssl", "genpkey", "-algorithm", "EC", "-pkeyopt",
                            "ec_paramgen_curve:P-256", "-out", str(key)],
                           check=True, capture_output=True, timeout=30)
            public = export_uncompressed_public_key(key)
            isolated = work / "isolated"; shutil.copytree(original, isolated)
            trust_hash = prepare_release_signing_state(isolated, key, public)
            changed = (isolated / "public" / "hbox-local-trust.h").read_text()
            self.assertIn(", ".join(f"0x{value:02x}" for value in public), changed)
            self.assertTrue(changed.endswith(header.split("};", 1)[1]))
            self.assertEqual(len(trust_hash), 64)
            self.assertEqual(json.loads((isolated / "manifest.json").read_text()),
                             {"trustHeaderSha256": trust_hash, "productId": "test"})
            self.assertEqual(json.loads((original / "manifest.json").read_text())["trustHeaderSha256"], "old")
            self.assertEqual(export_uncompressed_public_key(isolated / "pki" / "firmware-release-private.pem"), public)
            self.assertEqual(load_public_key(isolated / "pki" / "firmware-release-public.pem"), public)
            self.assertEqual((original / "public" / "hbox-local-trust.h").read_text(), header)
            self.assertEqual((original / "pki" / "firmware-release-private.pem").read_text(), "existing-development-key")
            (isolated / "public" / "hbox-local-trust.h").write_text("missing release trust")
            with self.assertRaisesRegex(RuntimeError, "unique provisioned"):
                prepare_release_signing_state(isolated, key, public)

    def test_config_format_tracks_header_instead_of_fixed_release_number(self):
        with tempfile.TemporaryDirectory() as directory:
            header = Path(directory) / "board_cfg.h"
            for definition, expected in (("(uint32_t)0x000022", 34),
                                         ("(uint32_t)0x000023", 35), ("36u", 36)):
                with self.subTest(definition=definition):
                    header.write_text(f"#define CONFIG_VERSION {definition} // format\n", encoding="utf-8")
                    version = read_config_version(header)
                    self.assertEqual(version, expected)
                    contract = make_install_contract(version)
                    self.assertEqual(contract["configRead"], {"min": expected, "max": expected})
                    self.assertEqual(contract["configWrite"], expected)
                    self.assertEqual(contract["protocol"], 2)

    def test_invalid_config_definition_fails_closed(self):
        with tempfile.TemporaryDirectory() as directory:
            header = Path(directory) / "board_cfg.h"
            for text in ("", "#define CONFIG_VERSION OTHER\n", "#define CONFIG_VERSION 35 + 1\n",
                         "#define CONFIG_VERSION 35\n#define CONFIG_VERSION 36\n",
                         "#define CONFIG_VERSION 0\n", "#define CONFIG_VERSION 0x100000000\n"):
                with self.subTest(text=text):
                    header.write_text(text, encoding="utf-8")
                    with self.assertRaisesRegex(RuntimeError, "CONFIG_VERSION"):
                        read_config_version(header)

    def test_signed_package_accepts_new_config_and_rejects_stale_or_wrong_identity(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            key = work / "test-key.pem"
            subprocess.run(["openssl", "genpkey", "-algorithm", "EC", "-pkeyopt",
                            "ec_paramgen_curve:P-256", "-out", str(key)],
                           check=True, capture_output=True, timeout=30)
            public = export_uncompressed_public_key(key)
            app, adc = work / "app.bin", work / "adc.bin"
            adc.write_bytes(b"test-adc")
            def executable(version, config):
                return struct.pack("<8sIIII32s65s", b"XORAFW2\0", 1, 2, 2, config,
                                   version.encode("ascii"), b"a" * 64)
            for slot in ("A", "B"):
                app.write_bytes(executable("1.0.0", 35))
                dest = work / f"stm32-{slot}.zip"
                with contextlib.redirect_stdout(io.StringIO()):
                    identity = make_stm32_package(slot, app, adc, "1.0.0", key, public,
                                                 "b" * 64, dest, config_version=35)
                self.assertEqual(identity, ("1.0.0", "a" * 64, 35))
                with zipfile.ZipFile(dest) as archive:
                    self.assertEqual(archive.read("application.bin"), app.read_bytes())
                for version, config in (("1.0.0", 34), ("1.0.1", 35)):
                    app.write_bytes(executable(version, config))
                    rejected = work / "rejected.zip"
                    with self.assertRaisesRegex(RuntimeError,
                            rf"{slot}.*1\.0\.0/35; executable declares {version}/{config}"):
                        make_stm32_package(slot, app, adc, "1.0.0", key, public,
                                           "b" * 64, rejected, config_version=35)
                    self.assertFalse(rejected.exists())

    def test_default_remote_server_and_explicit_local_override(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory); token = work / "token"; token.touch()
            args = (work / "source", work / "key", work, "1.0.2", token)
            _, command = final_stage_command(*args)
            self.assertEqual(command[command.index("--server") + 1], "https://manager.st-dash.com")
            _, command = final_stage_command(*args, server="http://localhost:3001")
            self.assertEqual(command[command.index("--server") + 1], "http://localhost:3001")

    def test_local_token_save_replaces_old_token_without_backups(self):
        with tempfile.TemporaryDirectory() as directory:
            state = Path(directory) / "state"
            first = "stsvc_" + secrets.token_urlsafe(32)
            second = "stsvc_" + secrets.token_urlsafe(32)
            token_file = save_local_token(first + "\r\n", state)
            self.assertEqual(token_file.read_text().strip(), first)
            save_local_token(second, state)
            self.assertEqual(token_file.read_text().strip(), second)
            self.assertEqual(list(state.iterdir()), [token_file])
            stage, command = final_stage_command(state / "source", state / "key", state, "1.0.4", token_file)
            self.assertEqual(stage, "bundle-and-draft")
            self.assertEqual(command[command.index("--service-token-file") + 1], str(token_file))
            self.assertNotIn(first, " ".join(command))
            self.assertNotIn(second, " ".join(command))

    def test_invalid_local_token_preserves_previous_content(self):
        with tempfile.TemporaryDirectory() as directory:
            state = Path(directory); secret = "stsvc_" + secrets.token_urlsafe(32)
            token_file = save_local_token(secret, state)
            for invalid in ("", "invalid", secret + "\nextra", secret[:-1], secret + "x"):
                with self.assertRaisesRegex(RuntimeError, "Invalid service token"):
                    save_local_token(invalid, state)
                self.assertEqual(token_file.read_text().strip(), secret)
                self.assertEqual(list(state.iterdir()), [token_file])

    def test_replace_failure_preserves_token_and_cleans_temporary_file(self):
        with tempfile.TemporaryDirectory() as directory:
            state = Path(directory); original = "stsvc_" + secrets.token_urlsafe(32)
            token_file = save_local_token(original, state)
            with patch("tools.local_firmware_draft.os.replace", side_effect=PermissionError("test failure")):
                with self.assertRaises(PermissionError):
                    save_local_token("stsvc_" + secrets.token_urlsafe(32), state)
            self.assertEqual(token_file.read_text().strip(), original)
            self.assertEqual(list(state.iterdir()), [token_file])

    def test_save_command_uses_stdin_and_exits_without_build_or_upload(self):
        with tempfile.TemporaryDirectory() as directory:
            state = Path(directory) / "state"; secret = "stsvc_" + secrets.token_urlsafe(32)
            output = io.StringIO()
            with patch("tools.local_firmware_draft.STATE", state), patch("sys.stdin", io.StringIO(secret)), \
                    patch("tools.local_firmware_draft.run_stage") as stage, \
                    patch("tools.local_firmware_draft.export_uncompressed_public_key") as key, contextlib.redirect_stdout(output):
                self.assertEqual(main(["--save-token"]), 0)
            stage.assert_not_called(); key.assert_not_called()
            self.assertEqual((state / "firmware-manage-token.txt").read_text().strip(), secret)
            self.assertNotIn(secret, output.getvalue())

    def test_no_token_still_packages_later_version(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            stage, command = final_stage_command(
                work / "source" / "release-source.json", work / "key.pem",
                work, "1.0.1", work / "missing-token.txt",
            )
            self.assertEqual(stage, "bundle-and-notes")
            self.assertTrue(command[1].endswith("create-firmware-draft.js"))
            self.assertIn("--dry-run", command)
            self.assertIn("--allow-worktree", command)
            self.assertNotIn("--service-token-file", command)
            self.assertNotIn("--initial-release", command)

    def test_token_uses_initial_notes_only_for_first_version(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            token = work / "token.txt"
            token.touch()
            args = (work / "source.json", work / "key.pem", work)
            first_stage, first_command = final_stage_command(*args, "1.0.0", token)
            later_stage, later_command = final_stage_command(*args, "1.0.1", token)
            _, explicit_command = final_stage_command(*args, "1.0.1", token, "xora-v1.0.0")
            self.assertEqual(first_stage, "bundle-and-draft")
            self.assertEqual(later_stage, "bundle-and-draft")
            self.assertIn("--initial-release", first_command)
            self.assertNotIn("--initial-release", later_command)
            self.assertEqual(explicit_command[-2:], ["--since", "xora-v1.0.0"])

    def test_no_upload_overrides_existing_token(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory); token = work / 'token'; token.touch()
            stage, command = final_stage_command(work/'source',work/'key',work,'1.0.2',token,no_upload=True)
            self.assertEqual(stage,'bundle-and-notes'); self.assertIn('--dry-run',command)
            self.assertNotIn('--service-token-file',command)

    def test_identity_requires_unique_protocol_two_component(self):
        def image(component, protocol):
            return struct.pack('<8sIIII32s65s',b'XORAFW2\0',component,protocol,protocol,34,b'1.0.2',b'a'*64)
        for component in (1,2):
            good=image(component,2)
            self.assertEqual(release_identity(good,component),('1.0.2','a'*64,34))
            for invalid in (image(component,1),image(3-component,2),good+good):
                with self.assertRaisesRegex(RuntimeError,'unique'):
                    release_identity(invalid,component)


if __name__ == "__main__":
    unittest.main()
