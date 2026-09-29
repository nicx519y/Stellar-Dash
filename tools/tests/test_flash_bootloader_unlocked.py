"""Host-only checks for the unlocked bootloader's sector write command."""

import importlib.util
import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from tools import hbox


SCRIPT = Path(__file__).resolve().parents[1] / "flash_bootloader_unlocked.py"
spec = importlib.util.spec_from_file_location("flash_bootloader_unlocked", SCRIPT)
flash = importlib.util.module_from_spec(spec)
spec.loader.exec_module(flash)


class FlashBootloaderUnlockedTests(unittest.TestCase):
    def test_hbox_bootloader_commands_separate_build_and_flash(self):
        with patch.object(hbox, "_run_python_tool", return_value=0) as run_tool:
            self.assertEqual(hbox.main(["build", "bootloader"]), 0)
            self.assertEqual(hbox.main(["flash", "bootloader"]), 0)
            self.assertEqual(hbox.main(["flash", "bootloader", "--build"]), 0)
        self.assertEqual(
            [call.args for call in run_tool.call_args_list],
            [
                ("flash_bootloader_unlocked.py", ["--build-only"]),
                ("flash_bootloader_unlocked.py", []),
                ("flash_bootloader_unlocked.py", ["--build"]),
            ],
        )

    def test_flash_artifact_requires_unlocked_manifest_and_exact_image(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            directory = root / "build-unlocked"
            payload = struct.pack("<II", 0x20020000, 0x08000101)
            payload += b"\xff" * (flash.SECTOR_BYTES - len(payload))
            with patch.object(flash, "BOOTLOADER", root):
                with self.assertRaisesRegex(ValueError, "no unlocked bootloader artifact"):
                    flash.load_artifact(directory)
                image = flash.save_artifact(payload, 8, directory)
                self.assertEqual(flash.load_artifact(directory), image)
                manifest = directory / flash.MANIFEST_NAME
                record = json.loads(manifest.read_text(encoding="utf-8"))
                record["bootSecurityMode"] = "secure-production"
                manifest.write_text(json.dumps(record), encoding="utf-8")
                with self.assertRaisesRegex(ValueError, "manifest/image validation"):
                    flash.load_artifact(directory)

    def test_flash_without_build_never_compiles_and_rejects_missing_artifact(self):
        with tempfile.TemporaryDirectory() as temporary:
            with patch.object(flash, "ARTIFACT_DIR", Path(temporary)):
                with patch.object(flash, "build_image") as build:
                    with patch.object(flash, "flash_image") as burn:
                        with patch.object(sys, "argv", ["flash_bootloader_unlocked.py"]):
                            with self.assertRaisesRegex(ValueError, "no unlocked bootloader artifact"):
                                flash.main()
                build.assert_not_called()
                burn.assert_not_called()

    def test_full_sector_command_checks_target_then_writes_and_verifies(self):
        with tempfile.TemporaryDirectory() as directory:
            image = Path(directory) / "sector.bin"
            image.write_bytes(b"\xff" * flash.SECTOR_BYTES)
            with patch.object(flash.subprocess, "run") as run:
                flash.flash_image("openocd", image, "ABC123")

        command = run.call_args.args[0]
        script = [command[index + 1] for index, value in enumerate(command[:-1])
                  if value == "-c"]
        self.assertIn("hla_serial ABC123", script)
        self.assertLess(next(i for i, item in enumerate(script) if "DBGMCU" in item or "hbox_idcode" in item),
                        script.index("flash erase_sector 0 0 0"))
        self.assertEqual(script.count("flash erase_sector 0 0 0"), 1)
        self.assertFalse(any(item.startswith("flash info") or
                             item.startswith("flash probe") for item in script))
        self.assertEqual(sum(item.startswith("flash write_image ") for item in script), 1)
        self.assertEqual(sum(item.startswith("verify_image ") for item in script), 1)
        self.assertLess(script.index("flash erase_sector 0 0 0"),
                        next(i for i, item in enumerate(script) if item.startswith("flash write_image ")))
        self.assertLess(next(i for i, item in enumerate(script) if item.startswith("flash write_image ")),
                        next(i for i, item in enumerate(script) if item.startswith("verify_image ")))
        self.assertFalse(any("option" in item.lower() or "unprotect" in item.lower() or
                             "mass_erase" in item.lower() for item in script))

    def test_wrong_image_size_never_invokes_openocd(self):
        with tempfile.TemporaryDirectory() as directory:
            image = Path(directory) / "partial.bin"
            image.write_bytes(b"\xff" * 8)
            with patch.object(flash.subprocess, "run") as run:
                with self.assertRaisesRegex(ValueError, "exactly 128 KiB"):
                    flash.flash_image("openocd", image, None)
            run.assert_not_called()


if __name__ == "__main__":
    unittest.main()
