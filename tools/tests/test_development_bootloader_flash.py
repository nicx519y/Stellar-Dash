import re
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools"
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

from build import BuildTool  # noqa: E402


LAYOUT_HEADER = """\
#define HBOX_INTERNAL_FLASH_BASE_ADDRESS 0x08000000u
#define HBOX_INTERNAL_FLASH_TOTAL_BYTES 0x00020000u
#define HBOX_DEVICE_IDENTITY_REGION_ADDRESS 0x0801C000u
"""


class DevelopmentBootloaderFlashTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(
            prefix="hbox-bootloader-dev-flash-"
        )
        self.root = Path(self.temporary.name)
        (self.root / "common").mkdir()
        (self.root / "common" / "internal_flash_security_layout.h").write_text(
            LAYOUT_HEADER,
            encoding="utf-8",
        )
        self.bootloader = self.root / "bootloader"
        (self.bootloader / "build").mkdir(parents=True)
        (self.bootloader / "build" / "bootloader.elf").write_bytes(b"ELF")

        self.tool = BuildTool.__new__(BuildTool)
        self.tool.project_root = self.root
        self.tool.bootloader_dir = self.bootloader
        self.tool.config = {"openocd_path": "openocd"}
        self.commands: list[list[str]] = []

    def tearDown(self) -> None:
        self.temporary.cleanup()

    @staticmethod
    def _dump_path(command: list[str]) -> Path | None:
        match = re.search(r'dump_image "([^"]+)"', " ".join(command))
        return Path(match.group(1)) if match else None

    def test_blank_security_tail_allows_backup_and_flash(self) -> None:
        def run_command(command, _cwd):
            self.commands.append(command)
            joined = " ".join(command)
            dump_path = self._dump_path(command)
            if dump_path is not None:
                dump_path.parent.mkdir(parents=True, exist_ok=True)
                if "flash erase_sector" in joined:
                    dump_path.write_bytes(b"\xFF" * 0x4000)
                elif "device-backups" in joined:
                    dump_path.write_bytes(b"\xFF" * 0x20000)
                else:
                    dump_path.write_bytes(b"\xFF" * 0x4000)
            return True

        self.tool.run_command = run_command

        self.assertTrue(self.tool.flash_bootloader_development())
        self.assertEqual(len(self.commands), 3)
        self.assertIn(
            "flash erase_sector 0 0 0",
            " ".join(self.commands[2]),
        )
        backups = list(
            (self.root / ".hbox" / "device-backups").glob(
                "internal-flash-*.bin"
            )
        )
        self.assertEqual(len(backups), 1)
        self.assertEqual(backups[0].stat().st_size, 0x20000)

    def test_nonblank_security_tail_refuses_before_erase(self) -> None:
        def run_command(command, _cwd):
            self.commands.append(command)
            dump_path = self._dump_path(command)
            self.assertIsNotNone(dump_path)
            data = bytearray(b"\xFF" * 0x4000)
            data[0x20] = 0x00
            dump_path.write_bytes(data)
            return True

        self.tool.run_command = run_command

        self.assertFalse(self.tool.flash_bootloader_development())
        self.assertEqual(len(self.commands), 1)
        self.assertNotIn("flash erase_sector", " ".join(self.commands[0]))
        self.assertEqual(
            list(
                (self.root / ".hbox" / "device-backups").glob(
                    "internal-flash-*.bin"
                )
            ),
            [],
        )


if __name__ == "__main__":
    unittest.main()
