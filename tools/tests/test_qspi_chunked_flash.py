import io
import subprocess
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path
from unittest import mock

from tools.build import BuildTool


class QspiWholeImageFlashTests(unittest.TestCase):
    @staticmethod
    def _fixture(root: str, payload: bytes = b"payload"):
        application_dir = Path(root) / "application"
        config_dir = application_dir / "Openocd_Script"
        build_dir = application_dir / "build"
        config_dir.mkdir(parents=True)
        build_dir.mkdir(parents=True)
        config = config_dir / "ST-LINK-QSPIFLASH.cfg"
        config.write_text("# fixture\n", encoding="utf-8")
        source = build_dir / "payload.bin"
        source.write_bytes(payload)
        tool = BuildTool.__new__(BuildTool)
        tool.application_dir = application_dir
        tool.config = {"openocd_path": "openocd"}
        return tool, source, config

    def test_short_image_uses_one_whole_image_session(self) -> None:
        with tempfile.TemporaryDirectory(prefix="hbox-qspi-whole-") as root:
            tool, source, _config = self._fixture(root)
            original = source.read_bytes()
            invocations = []

            def capture_command(command, _cwd, **_kwargs):
                invocations.append(
                    (list(command), Path(command[-1]).read_text(encoding="utf-8"))
                )
                return True

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
        self.assertEqual(len(invocations), 1)
        _command, script = invocations[0]
        self.assertEqual(script.count("flash write_image erase"), 1)
        self.assertEqual(script.count("flash verify_bank 1"), 1)
        self.assertIn("0x90000000 bin", script)
        self.assertIn("0x00000000", script)
        self.assertNotIn("flash erase_sector", script)
        self.assertNotIn("verify_image", script)
        self.assertNotIn("reset run", script)

    def test_large_image_still_uses_one_whole_image_session(self) -> None:
        payload = bytes(range(256)) * 144  # 9 x 4096 bytes
        with tempfile.TemporaryDirectory(prefix="hbox-qspi-large-") as root:
            tool, source, _config = self._fixture(root, payload)
            scripts = []

            def capture_command(command, _cwd, **_kwargs):
                scripts.append(Path(command[-1]).read_text(encoding="utf-8"))
                return True

            tool.run_command = mock.Mock(side_effect=capture_command)
            self.assertTrue(
                tool._flash_qspi_file_in_chunks(
                    source,
                    0x90000000,
                    "fixture",
                )
            )

        self.assertEqual(len(scripts), 1)
        self.assertEqual(scripts[0].count("flash write_image erase"), 1)
        self.assertEqual(scripts[0].count("flash verify_bank 1"), 1)
        self.assertNotIn("flash erase_sector", scripts[0])
        self.assertNotIn("chunk-", scripts[0])
        self.assertIn("reset run", scripts[0])

    def test_failed_session_has_only_optional_connect_under_reset_retry(self) -> None:
        with tempfile.TemporaryDirectory(prefix="hbox-qspi-retry-") as root:
            tool, source, _config = self._fixture(root, b"retry")
            invocations = []
            results = iter((False, True))

            def capture_command(command, _cwd, **_kwargs):
                invocations.append(
                    (list(command), Path(command[-1]).read_text(encoding="utf-8"))
                )
                return next(results)

            tool.run_command = mock.Mock(side_effect=capture_command)
            self.assertTrue(
                tool._flash_qspi_file_in_chunks(
                    source,
                    0x90000000,
                    "fixture",
                    reset_after=False,
                    connect_under_reset_fallback=True,
                )
            )

        self.assertEqual(len(invocations), 2)
        self.assertEqual(invocations[0][1], invocations[1][1])
        self.assertNotIn("reset_config connect_assert_srst", invocations[0][0])
        self.assertIn("reset_config connect_assert_srst", invocations[1][0])
        for _command, script in invocations:
            self.assertEqual(script.count("flash write_image erase"), 1)
            self.assertEqual(script.count("flash verify_bank 1"), 1)

    def test_requested_swd_speed_is_set_before_the_script(self) -> None:
        with tempfile.TemporaryDirectory(prefix="hbox-qspi-speed-") as root:
            tool, source, _config = self._fixture(root, b"speed")
            invocations = []

            def capture_command(command, _cwd, **_kwargs):
                invocations.append(list(command))
                return True

            tool.run_command = mock.Mock(side_effect=capture_command)
            self.assertTrue(
                tool._flash_qspi_file_in_chunks(
                    source,
                    0x90000000,
                    "fixture",
                    adapter_speed_khz=400,
                )
            )

        self.assertEqual(len(invocations), 1)
        command = invocations[0]
        self.assertLess(command.index("adapter speed 400"), len(command) - 1)

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
                self.assertTrue(tool.run_command(["openocd"], Path.cwd(), quiet=True))
        self.assertEqual(output.getvalue(), "")

        failure = subprocess.CompletedProcess(
            ["openocd"],
            1,
            stdout=(
                "DEPRECATED! use 'read_memory' not 'mem2array'\n"
                "target verification failed\n"
            ),
        )
        with mock.patch("tools.build.subprocess.run", return_value=failure):
            output = io.StringIO()
            with redirect_stdout(output):
                self.assertFalse(tool.run_command(["openocd"], Path.cwd(), quiet=True))
        self.assertIn("target verification failed", output.getvalue())
        self.assertNotIn("DEPRECATED", output.getvalue())
        self.assertIn("退出码: 1", output.getvalue())

    def test_runtime_caller_can_hold_target_until_metadata_commit(self) -> None:
        with tempfile.TemporaryDirectory(prefix="hbox-qspi-hold-") as root:
            tool, source, _config = self._fixture(root)
            scripts = []

            def capture_command(command, _cwd, **_kwargs):
                scripts.append(Path(command[-1]).read_text(encoding="utf-8"))
                return True

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

        self.assertEqual(len(scripts), 1)
        self.assertIn("flash write_image erase", scripts[0])
        self.assertIn("flash verify_bank 1", scripts[0])
        self.assertNotIn("resume", scripts[0])
        self.assertNotIn("reset run", scripts[0])

    def test_runtime_software_reset_does_not_wait_for_dap(self) -> None:
        with tempfile.TemporaryDirectory(prefix="hbox-qspi-reset-") as root:
            tool, source, _config = self._fixture(root, b"ready")
            scripts = []

            def capture_command(command, _cwd, **_kwargs):
                scripts.append(Path(command[-1]).read_text(encoding="utf-8"))
                return True

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

        self.assertEqual(len(scripts), 1)
        self.assertIn("mww 0xE000ED0C 0x05FA0004", scripts[0])
        self.assertNotIn("sleep 100", scripts[0])

    def test_destructive_session_is_serial_and_uid_bound(self) -> None:
        serial = "00112233445566778899AABB"
        uid = "A1B2C3D4E5F60718293A4B5C"
        with tempfile.TemporaryDirectory(prefix="hbox-qspi-bound-") as root:
            tool, source, config = self._fixture(root, b"P" * 4097)
            invocations = []

            def capture_command(command, _cwd, **_kwargs):
                invocations.append(
                    (list(command), Path(command[-1]).read_text(encoding="utf-8"))
                )
                return True

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
        command, script = invocations[0]
        serial_command = f"adapter serial {serial}"
        self.assertIn(serial_command, command)
        self.assertLess(command.index(str(config)), command.index(serial_command))
        self.assertLess(command.index(serial_command), len(command) - 1)
        first_destructive = script.index("flash write_image erase")
        dev_read = "set hbox_dbgmcu_idcode [mrw 0x5C001000]"
        self.assertIn(dev_read, script)
        self.assertIn("($hbox_dbgmcu_idcode & 0xFFF) != 0x450", script)
        self.assertLess(script.index(dev_read), first_destructive)
        for index, word in enumerate(("A1B2C3D4", "E5F60718", "293A4B5C")):
            read = f"set hbox_uid_{index} [mrw 0x{0x1FF1E800 + index * 4:08X}]"
            compare = f"$hbox_uid_{index} != 0x{word}"
            self.assertIn(read, script)
            self.assertIn(compare, script)
            self.assertLess(script.index(read), first_destructive)

    def test_automatic_single_stlink_still_asserts_uid_before_write(self) -> None:
        uid = "A1B2C3D4E5F60718293A4B5C"
        with tempfile.TemporaryDirectory(prefix="hbox-qspi-auto-") as root:
            tool, source, _config = self._fixture(root)
            invocations = []

            def capture_command(command, _cwd, **_kwargs):
                invocations.append(
                    (list(command), Path(command[-1]).read_text(encoding="utf-8"))
                )
                return True

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

        self.assertEqual(len(invocations), 1)
        command, script = invocations[0]
        self.assertFalse(
            any(
                isinstance(argument, str) and argument.startswith("adapter serial ")
                for argument in command
            )
        )
        self.assertIn("STM32 DEV_ID changed", script)
        self.assertIn("STM32 UID changed", script)
        destructive = script.index("flash write_image erase")
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
                BuildTool._openocd_tcl_braced_path(unsafe, must_exist=False)


if __name__ == "__main__":
    unittest.main()
