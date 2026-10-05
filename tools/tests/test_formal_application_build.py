"""Host-only signer migration/preflight tests with ephemeral keys and mocked builds."""

import contextlib
import hashlib
import io
import json
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tools import formal_application_build as formal, hbox


class FormalApplicationBuildTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="xora-formal-app-tests-")
        cls.root = Path(cls.temp.name)
        cls.seed = cls.root / "local"
        with contextlib.redirect_stdout(io.StringIO()):
            formal.local.initialize_local_state(cls.seed)
        cls.key = cls.root / "formal-test-key.pem"
        subprocess.run(["openssl", "genpkey", "-algorithm", "EC", "-pkeyopt",
                        "ec_paramgen_curve:P-256", "-out", str(cls.key)],
                       check=True, capture_output=True, timeout=30)
        cls.public = formal.draft.export_uncompressed_public_key(cls.key)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def setUp(self):
        self.output = self.root / self._testMethodName
        self.server = "https://manager.example.test"
        self.patch = mock.patch.object(formal.local, "DEFAULT_STATE_DIR", self.seed)
        self.patch.start()
        self.addCleanup(self.patch.stop)

    def fake_compile(self, _name, command, _log, timeout):
        self.assertEqual(timeout, 600)
        self.assertIn("--unlocked-development", command)
        self.assertIn("--skip-web", command)
        self.assertEqual(command[command.index("--signing-key") + 1], str(self.key))
        state = Path(command[command.index("--state-dir") + 1])
        slot = command[command.index("--slot") + 1]
        artifacts = state / "artifacts"
        artifacts.mkdir(exist_ok=True)
        (artifacts / f"application-slot-{slot.lower()}.bin").write_bytes(b"test-app" + self.public)
        (artifacts / "artifact-manifest.json").write_text(json.dumps({
            "targetSlot": slot, "bootSecurityMode": "unlocked-development",
            "requiresManualLifecycleProvisioning": False, "requiredLifecycle": [],
        }))

    def read_manifest(self, state):
        return json.loads((state / "artifacts/artifact-manifest.json").read_text())

    def build_fixture(self, slot="A"):
        with mock.patch.object(formal.draft, "resolve_release_key", return_value=self.key), \
                mock.patch.object(formal.draft, "verify_server_release_key") as remote, \
                mock.patch.object(formal.draft, "run_stage", side_effect=self.fake_compile), \
                mock.patch.object(formal.local, "load_verified_artifact_manifest", side_effect=self.read_manifest), \
                contextlib.redirect_stdout(io.StringIO()):
            result = formal.build_artifacts(self.output, slot, self.server, 4)
        self.assertEqual(remote.call_args_list, [mock.call(self.server, self.public)] * 2)
        return result

    def test_build_both_slots_preserves_development_pki_and_uses_external_formal_key(self):
        before = {p.relative_to(self.seed): p.read_bytes() for p in self.seed.rglob("*") if p.is_file()}
        for slot in ("A", "B"):
            result = self.build_fixture(slot)
            self.assertEqual(result["releaseSigning"], {
                "server": self.server, "publicKeySha256": hashlib.sha256(self.public).hexdigest()})
            self.assertFalse((self.output / "pki/firmware-release-private.pem").exists())
            self.assertFalse((self.output / "pki/manufacturer-ca-private.pem").exists())
            self.assertEqual(formal.local.load_public_key(self.output / "pki/firmware-release-public.pem"), self.public)
        self.assertEqual(before, {p.relative_to(self.seed): p.read_bytes() for p in self.seed.rglob("*") if p.is_file()})

    def test_missing_formal_key_and_server_mismatch_stop_before_state_or_build(self):
        for failure in ("No release signing key configured", "does not match", "Cannot verify"):
            with mock.patch.object(formal.draft, "resolve_release_key", return_value=self.key) as resolve, \
                    mock.patch.object(formal.draft, "verify_server_release_key", side_effect=RuntimeError(failure)), \
                    mock.patch.object(formal.draft, "run_stage") as build:
                if failure.startswith("No "):
                    resolve.side_effect = RuntimeError(failure)
                with self.assertRaisesRegex(RuntimeError, failure):
                    formal.build_artifacts(self.output, "A", self.server, 4)
                build.assert_not_called()
                self.assertFalse(self.output.exists())

    def test_existing_artifacts_reject_wrong_slot_protected_mode_and_missing_signing_record(self):
        self.build_fixture()
        path = self.output / "artifacts/artifact-manifest.json"
        original = self.read_manifest(self.output)
        cases = ({"targetSlot": "B"}, {"bootSecurityMode": "secure-production"},
                 {"requiresManualLifecycleProvisioning": True}, {"requiredLifecycle": ["RDP1"]},
                 {"releaseSigning": None})
        for change in cases:
            path.write_text(json.dumps({**original, **change}))
            with mock.patch.object(formal.local, "load_verified_artifact_manifest", side_effect=self.read_manifest), \
                    mock.patch.object(formal.draft, "verify_server_release_key") as remote:
                with self.assertRaises(RuntimeError):
                    formal.check_artifacts(self.output, "A", self.server)
                remote.assert_not_called()

    def test_existing_artifacts_reject_wrong_compiled_key_and_online_key(self):
        self.build_fixture()
        with mock.patch.object(formal.local, "load_verified_artifact_manifest", side_effect=self.read_manifest), \
                mock.patch.object(formal.draft, "verify_server_release_key", side_effect=RuntimeError("key changed")):
            with self.assertRaisesRegex(RuntimeError, "key changed"):
                formal.check_artifacts(self.output, "A", self.server)
        (self.output / "artifacts/application-slot-a.bin").write_bytes(b"development image")
        with mock.patch.object(formal.local, "load_verified_artifact_manifest", side_effect=self.read_manifest):
            with self.assertRaisesRegex(RuntimeError, "absent"):
                formal.check_artifacts(self.output, "A", self.server)

    def test_formal_output_cannot_replace_development_state(self):
        with self.assertRaisesRegex(RuntimeError, "separate"):
            formal.build_artifacts(self.seed, "A", self.server, 4)

    def test_pending_flash_transactions_block_state_migration(self):
        transaction = self.output / "flash-transactions/test/transaction.json"
        transaction.parent.mkdir(parents=True)
        for status in formal.flash.BLOCKING_TRANSACTION_STATUSES:
            transaction.write_text(json.dumps({"status": status}))
            with self.assertRaisesRegex(RuntimeError, "existing flash transaction"):
                formal.reject_pending_transactions(self.output)
        transaction.write_text(json.dumps({"status": "completed"}))
        formal.reject_pending_transactions(self.output)

    def test_failed_preflight_or_build_never_enters_flash_child(self):
        for args in (["flash", "app", "A"], ["flash", "app", "B", "--build"]):
            with mock.patch.object(hbox, "_run_python_tool", return_value=2) as child, \
                    mock.patch.object(hbox, "_local_artifacts_are_unlocked_development") as validate, \
                    contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(hbox.main(args), 2)
                self.assertEqual(child.call_count, 1)
                self.assertEqual(child.call_args.args[0], "formal_application_build.py")
                validate.assert_not_called()


if __name__ == "__main__":
    unittest.main()
