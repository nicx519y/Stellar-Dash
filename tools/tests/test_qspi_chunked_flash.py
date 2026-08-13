import io
import subprocess
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path
from unittest import mock

from tools.build import BuildTool


class QspiChunkedFlashTests(unittest.TestCase):
    def test_short_image_is_passed_unchanged_to_openocd(self) -> None:
        with tempfile.TemporaryDirectory(prefix="hbox-qspi-pad-") as root:
            root_path = Path(root)
            application_dir = root_path / "application"
            config_dir = application_dir / "Openocd_Script"
            build_dir = application_dir / "build"
            config_dir.mkdir(parents=True)
            build_dir.mkdir(parents=True)
            (config_dir / "ST-LINK-QSPIFLASH.cfg").write_text(
                "# fixture\n", encoding="utf-8"
            )
            source = build_dir / "payload.bin"
            source.write_bytes(b"payload")
            original = source.read_bytes()
            scripts = []

            def capture_command(command, _cwd, **_kwargs):
                scripts.append(Path(command[-1]).read_text(encoding="utf-8"))
                return True

            tool = BuildTool.__new__(BuildTool)
            tool.application_dir = application_dir
            tool.config = {"openocd_path": "openocd"}
            tool.run_command = mock.Mock(side_effect=capture_command)

            self.assertTrue(
                tool._flash_qspi_file_in_chunks(
                    source,
                    0x90000000,
                    "fixture",
                    reset_after=False,
                )
            )

        self.assertEqual(original, b"payload")
        self.assertEqual(len(scripts), 1)
        self.assertEqual(scripts[0].count("flash write_image erase"), 1)
        self.assertEqual(scripts[0].count("flash verify_bank 1"), 1)
        self.assertIn("0x00000000", scripts[0])
        self.assertNotIn("verify_image", scripts[0])

    def test_quiet_command_hides_success_output_and_replays_failure(self) -> None:
        tool = BuildTool.__new__(BuildTool)
        tool.config = {"gcc_path": ""}
        success = subprocess.CompletedProcess(
            ["openocd"],
            0,
            stdout="DEPRECATED! use 'read_memory' not 'mem2array'\n",
        )
        with mock.patch("tools.build.subprocess.run", return_value=success):
            output = io.StringIO()
            with redirect_stdout(output):
                self.assertTrue(
                    tool.run_command(
                        ["openocd"],
                        Path.cwd(),
                        quiet=True,
                    )
                )
        self.assertEqual(output.getvalue(), "")

        failure = subprocess.CompletedProcess(
            ["openocd"],
            1,
            stdout="target verification failed\n",
        )
        with mock.patch("tools.build.subprocess.run", return_value=failure):
            output = io.StringIO()
            with redirect_stdout(output):
                self.assertFalse(
                    tool.run_command(
                        ["openocd"],
                        Path.cwd(),
                        quiet=True,
                    )
                )
        self.assertIn("target verification failed", output.getvalue())
        self.assertIn("退出码: 1", output.getvalue())

    def test_large_image_uses_one_standard_openocd_session(self) -> None:
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

            def capture_command(command, _cwd, **_kwargs):
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

        self.assertEqual(len(scripts), 1)
        self.assertEqual(scripts[0].count("flash write_image erase"), 1)
        self.assertEqual(scripts[0].count("flash verify_bank 1"), 1)
        self.assertNotIn("verify_image", scripts[0])
        self.assertIn("reset run", scripts[0])

    def test_image_size_does_not_create_page_or_chunk_sessions(self) -> None:
        with tempfile.TemporaryDirectory(prefix="hbox-qspi-pages-") as root:
            root_path = Path(root)
            application_dir = root_path / "application"
            config_dir = application_dir / "Openocd_Script"
            build_dir = application_dir / "build"
            config_dir.mkdir(parents=True)
            build_dir.mkdir(parents=True)
            (config_dir / "ST-LINK-QSPIFLASH.cfg").write_text(
                "# fixture\n", encoding="utf-8"
            )
            source = build_dir / "payload.bin"
            source.write_bytes(b"P" * (33 * 1024))
            scripts = []

            def capture_command(command, _cwd, **_kwargs):
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

        program_scripts = [script for script in scripts if "flash write_image" in script]
        self.assertEqual(len(program_scripts), 1)
        self.assertEqual(program_scripts[0].count("flash write_image erase"), 1)

    def test_caller_can_hold_target_until_metadata_commit(self) -> None:
        with tempfile.TemporaryDirectory(prefix="hbox-qspi-hold-") as root:
            root_path = Path(root)
            application_dir = root_path / "application"
            config_dir = application_dir / "Openocd_Script"
            build_dir = application_dir / "build"
            config_dir.mkdir(parents=True)
            build_dir.mkdir(parents=True)
            (config_dir / "ST-LINK-QSPIFLASH.cfg").write_text(
                "# fixture\n", encoding="utf-8"
            )
            source = build_dir / "payload.bin"
            source.write_bytes(b"payload")
            scripts = []

            def capture_command(command, _cwd, **_kwargs):
                scripts.append(Path(command[-1]).read_text(encoding="utf-8"))
                return True

            tool = BuildTool.__new__(BuildTool)
            tool.application_dir = application_dir
            tool.config = {"openocd_path": "openocd"}
            tool.run_command = mock.Mock(side_effect=capture_command)

            self.assertTrue(
                tool._flash_qspi_file_in_chunks(
                    source,
                    0x90000000,
                    "fixture",
                    reset_after=False,
                    runtime_attach=True,
                    leave_halted=True,
                )
            )

        self.assertNotIn("resume", scripts[0])
        self.assertNotIn("reset run", scripts[0])

    def test_runtime_software_reset_does_not_wait_for_dap_to_return(self) -> None:
        with tempfile.TemporaryDirectory(prefix="hbox-qspi-reset-") as root:
            root_path = Path(root)
            application_dir = root_path / "application"
            config_dir = application_dir / "Openocd_Script"
            build_dir = application_dir / "build"
            config_dir.mkdir(parents=True)
            build_dir.mkdir(parents=True)
            (config_dir / "ST-LINK-QSPIFLASH.cfg").write_text(
                "# fixture\n", encoding="utf-8"
            )
            source = build_dir / "ready.bin"
            source.write_bytes(b"ready")
            scripts = []

            def capture_command(command, _cwd, **_kwargs):
                scripts.append(Path(command[-1]).read_text(encoding="utf-8"))
                return True

            tool = BuildTool.__new__(BuildTool)
            tool.application_dir = application_dir
            tool.config = {"openocd_path": "openocd"}
            tool.run_command = mock.Mock(side_effect=capture_command)
            self.assertTrue(
                tool._flash_qspi_file_in_chunks(
                    source,
                    0x90000000,
                    "fixture",
                    reset_after=True,
                    runtime_attach=True,
                )
            )

        self.assertIn("mww 0xE000ED0C 0x05FA0004", scripts[0])
        self.assertNotIn("sleep 100", scripts[0])

    def test_every_destructive_session_is_serial_and_uid_bound(self) -> None:
        serial = "00112233445566778899AABB"
        uid = "A1B2C3D4E5F60718293A4B5C"
        with tempfile.TemporaryDirectory(prefix="hbox-qspi-bound-") as root:
            root_path = Path(root)
            application_dir = root_path / "application"
            config_dir = application_dir / "Openocd_Script"
            build_dir = application_dir / "build"
            config_dir.mkdir(parents=True)
            build_dir.mkdir(parents=True)
            config = config_dir / "ST-LINK-QSPIFLASH.cfg"
            config.write_text("# fixture\n", encoding="utf-8")
            source = build_dir / "payload.bin"
            source.write_bytes(b"P" * 4097)
            invocations = []

            def capture_command(command, _cwd, **_kwargs):
                invocations.append(
                    (list(command), Path(command[-1]).read_text(encoding="utf-8"))
                )
                return True

            tool = BuildTool.__new__(BuildTool)
            tool.application_dir = application_dir
            tool.config = {"openocd_path": "openocd"}
            tool.run_command = mock.Mock(side_effect=capture_command)

            self.assertTrue(
                tool._flash_qspi_file_in_chunks(
                    source,
                    0x90000000,
                    "fixture",
                    reset_after=False,
                    stlink_serial=serial,
                    expected_target_uid=uid,
                )
            )

        self.assertEqual(len(invocations), 1)
        uid_words = ("A1B2C3D4", "E5F60718", "293A4B5C")
        for command, script in invocations:
            serial_command = f"adapter serial {serial}"
            self.assertIn(serial_command, command)
            config_index = command.index(str(config))
            serial_index = command.index(serial_command)
            script_index = command.index(str(Path(command[-1])))
            self.assertLess(config_index, serial_index)
            self.assertLess(serial_index, script_index)
            destructive_positions = [
                position
                for operation in ("flash erase_sector", "flash write_image")
                if (position := script.find(operation)) >= 0
            ]
            self.assertTrue(destructive_positions)
            first_destructive = min(destructive_positions)
            dev_read = "set hbox_dbgmcu_idcode [mrw 0x5C001000]"
            dev_compare = "($hbox_dbgmcu_idcode & 0xFFF) != 0x450"
            self.assertIn(dev_read, script)
            self.assertIn(dev_compare, script)
            self.assertLess(script.index(dev_read), first_destructive)
            for index, word in enumerate(uid_words):
                read = f"set hbox_uid_{index} [mrw 0x{0x1FF1E800 + index * 4:08X}]"
                compare = f"$hbox_uid_{index} != 0x{word}"
                self.assertIn(read, script)
                self.assertIn(compare, script)
                self.assertLess(script.index(read), first_destructive)

    def test_automatic_single_stlink_still_asserts_uid_before_writes(self) -> None:
        uid = "A1B2C3D4E5F60718293A4B5C"
        with tempfile.TemporaryDirectory(prefix="hbox-qspi-auto-") as root:
            root_path = Path(root)
            application_dir = root_path / "application"
            config_dir = application_dir / "Openocd_Script"
            build_dir = application_dir / "build"
            config_dir.mkdir(parents=True)
            build_dir.mkdir(parents=True)
            (config_dir / "ST-LINK-QSPIFLASH.cfg").write_text(
                "# fixture\n", encoding="utf-8"
            )
            source = build_dir / "payload.bin"
            source.write_bytes(b"payload")
            invocations = []

            def capture_command(command, _cwd, **_kwargs):
                invocations.append(
                    (list(command), Path(command[-1]).read_text(encoding="utf-8"))
                )
                return True

            tool = BuildTool.__new__(BuildTool)
            tool.application_dir = application_dir
            tool.config = {"openocd_path": "openocd"}
            tool.run_command = mock.Mock(side_effect=capture_command)

            self.assertTrue(
                tool._flash_qspi_file_in_chunks(
                    source,
                    0x90000000,
                    "fixture",
                    reset_after=False,
                    stlink_serial=None,
                    expected_target_uid=uid,
                )
            )

        self.assertTrue(invocations)
        for command, script in invocations:
            self.assertFalse(
                any(
                    isinstance(argument, str)
                    and argument.startswith("adapter serial ")
                    for argument in command
                )
            )
            self.assertIn("STM32 DEV_ID changed", script)
            self.assertIn("STM32 UID changed", script)
            destructive = min(
                position
                for operation in ("flash erase_sector", "flash write_image")
                if (position := script.find(operation)) >= 0
            )
            self.assertLess(script.index("set hbox_dbgmcu_idcode"), destructive)
            self.assertLess(script.index("set hbox_uid_0"), destructive)

    def test_tcl_braced_path_rejects_unsafe_characters(self) -> None:
        safe = BuildTool._openocd_tcl_braced_path(
            Path("safe directory") / "payload.bin",
            must_exist=False,
        )
        self.assertTrue(safe.startswith("{"))
        self.assertTrue(safe.endswith("}"))
        for unsafe in (
            Path("unsafe{directory") / "payload.bin",
            Path("unsafe}directory") / "payload.bin",
            Path("unsafe\ndirectory") / "payload.bin",
            Path("unsafe\rdirectory") / "payload.bin",
        ):
            with self.subTest(path=unsafe), self.assertRaisesRegex(
                ValueError,
                "OpenOCD Tcl",
            ):
                BuildTool._openocd_tcl_braced_path(
                    unsafe,
                    must_exist=False,
                )


if __name__ == "__main__":
    unittest.main()
