"""Host-only checks for the local release command selection."""

import struct
import io
import secrets
import contextlib
import tempfile
import unittest
from unittest.mock import patch
from pathlib import Path

from tools.local_firmware_draft import final_stage_command, release_identity, save_local_token, main


class LocalFirmwareDraftCommandTests(unittest.TestCase):
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
