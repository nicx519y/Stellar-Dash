"""Host-only checks: mock child execution so no build or device access occurs."""

import contextlib
import io
import os
import unittest
from pathlib import Path
from unittest import mock

from tools import hbox


class BootProfileCliTests(unittest.TestCase):
    def test_bootloader_flag_reaches_build_child_without_changing_parent(self):
        with mock.patch.dict(os.environ, {"HBOX_BOOT_PROFILE": "0"}), mock.patch.object(
            hbox.subprocess, "call", return_value=0
        ) as child:
            self.assertEqual(hbox.main([
                "flash", "bootloader", "--build", "--boot-profile",
            ]), 0)
            self.assertEqual(os.environ["HBOX_BOOT_PROFILE"], "0")
            command = child.call_args.args[0]
            self.assertEqual(Path(command[1]).name, "flash_bootloader_unlocked.py")
            self.assertEqual(command[2:], ["--build"])
            self.assertEqual(child.call_args.kwargs["env"]["HBOX_BOOT_PROFILE"], "1")
            self.assertEqual(child.call_args.kwargs["env"]["PATH"], os.environ["PATH"])

    def test_app_flag_reaches_paired_build_and_keeps_existing_flash_route(self):
        for slot in ("A", "B"):
            with self.subTest(slot=slot), mock.patch.object(
                hbox, "_local_webconfig_state_is_initialized", return_value=True
            ), mock.patch.object(
                hbox, "_local_artifacts_are_unlocked_development", return_value=True
            ) as validate, mock.patch.object(
                hbox.subprocess, "call", return_value=0
            ) as child:
                self.assertEqual(hbox.main([
                    "flash", "app", slot, "--build", "--boot-profile",
                ]), 0)
                self.assertEqual(child.call_count, 2)
                build, flash = child.call_args_list
                self.assertEqual(Path(build.args[0][1]).name, "webconfig_local.py")
                self.assertEqual(build.args[0][2:], [
                    "build", "--slot", slot, "--skip-web", "--jobs", "4",
                    "--unlocked-development",
                ])
                self.assertEqual(build.kwargs["env"]["HBOX_BOOT_PROFILE"], "1")
                validate.assert_called_once_with(slot)
                self.assertEqual(Path(flash.args[0][1]).name, "webconfig_flash.py")
                self.assertEqual(flash.args[0][2:], ["--simple-execute"])
                self.assertNotIn("env", flash.kwargs)

    def test_profile_build_failure_never_starts_application_flash(self):
        with mock.patch.object(
            hbox, "_local_webconfig_state_is_initialized", return_value=True
        ), mock.patch.object(
            hbox, "_local_artifacts_are_unlocked_development"
        ) as validate, mock.patch.object(
            hbox.subprocess, "call", return_value=7
        ) as child:
            self.assertEqual(hbox.main([
                "flash", "app", "A", "--build", "--boot-profile",
            ]), 7)
            child.assert_called_once()
            validate.assert_not_called()

    def test_profile_keeps_manifest_rejection_before_flash(self):
        with mock.patch.object(
            hbox, "_local_webconfig_state_is_initialized", return_value=True
        ), mock.patch.object(
            hbox, "_local_artifacts_are_unlocked_development", return_value=False
        ), mock.patch.object(hbox.subprocess, "call", return_value=0) as child:
            self.assertEqual(hbox.main([
                "flash", "app", "A", "--build", "--boot-profile",
            ]), 2)
            child.assert_called_once()

    def test_invalid_profile_combinations_never_start_child(self):
        for arguments in (
            ["flash", "bootloader", "--boot-profile"],
            ["flash", "app", "A", "--boot-profile"],
            ["flash", "tx", "--build", "--boot-profile"],
            ["flash", "code", "A", "--build", "--boot-profile"],
            ["flash", "appAll", "A", "--build", "--boot-profile"],
            ["flash", "app", "--build", "--boot-profile"],
        ):
            with self.subTest(arguments=arguments), mock.patch.object(
                hbox.subprocess, "call"
            ) as child, contextlib.redirect_stderr(io.StringIO()):
                if arguments[1] == "app" and "A" not in arguments:
                    self.assertEqual(hbox.main(arguments), 2)
                else:
                    with self.assertRaises(SystemExit) as error:
                        hbox.main(arguments)
                    self.assertEqual(error.exception.code, 2)
                child.assert_not_called()

    def test_normal_command_does_not_inject_diagnostic_environment(self):
        with mock.patch.object(hbox.subprocess, "call", return_value=0) as child:
            self.assertEqual(hbox.main(["flash", "bootloader", "--build"]), 0)
            self.assertNotIn("env", child.call_args.kwargs)


if __name__ == "__main__":
    unittest.main()
