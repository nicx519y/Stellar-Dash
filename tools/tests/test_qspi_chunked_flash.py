import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tools.build import BuildTool


class QspiChunkedFlashTests(unittest.TestCase):
    def test_large_image_uses_erase_then_short_program_sessions(self) -> None:
        with tempfile.TemporaryDirectory(prefix="hbox-qspi-flash-") as root:
            root_path = Path(root)
            application_dir = root_path / "application"
            config_dir = application_dir / "Openocd_Script"
            build_dir = application_dir / "build"
            config_dir.mkdir(parents=True)
            build_dir.mkdir(parents=True)
            (config_dir / "ST-LINK-QSPIFLASH.cfg").write_text(
                "# fixture\n", encoding="utf-8"
            )
            source = build_dir / "application.bin"
            source.write_bytes(bytes(range(256)) * 144)  # 9 x 4096 bytes

            scripts = []

            def capture_command(command, _cwd):
                scripts.append(Path(command[-1]).read_text(encoding="utf-8"))
                return True

            tool = BuildTool.__new__(BuildTool)
            tool.application_dir = application_dir
            tool.config = {"openocd_path": "openocd"}
            tool.run_command = mock.Mock(side_effect=capture_command)

            self.assertTrue(
                tool._flash_qspi_file_in_chunks(
                    source, 0x90000000, "fixture"
                )
            )

        self.assertEqual(len(scripts), 3)
        self.assertIn("flash erase_sector 1 0 0", scripts[0])
        self.assertNotIn("flash write_image", scripts[0])
        self.assertEqual(scripts[1].count("flash write_image"), 8)
        self.assertEqual(scripts[1].count("verify_image"), 8)
        self.assertNotIn("reset run", scripts[1])
        self.assertEqual(scripts[2].count("flash write_image"), 1)
        self.assertEqual(scripts[2].count("verify_image"), 1)
        self.assertIn("reset run", scripts[2])


if __name__ == "__main__":
    unittest.main()
