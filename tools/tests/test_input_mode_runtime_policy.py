import pathlib
import shutil
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
INC = ROOT / "application" / "Cpp_Core" / "Inc"


class InputModeRuntimePolicyTest(unittest.TestCase):
    def test_native_policies(self) -> None:
        compiler = shutil.which("g++") or shutil.which("clang++")
        self.assertIsNotNone(compiler, "host C++ compiler is required")

        with tempfile.TemporaryDirectory() as temp:
            executable = pathlib.Path(temp) / "input_mode_runtime_policy_test"
            compiled = subprocess.run(
                [
                    compiler,
                    "-std=c++17",
                    "-I",
                    str(INC),
                    str(ROOT / "tools" / "tests" /
                        "input_mode_runtime_policy_test.cpp"),
                    "-o",
                    str(executable),
                ],
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            ran = subprocess.run(
                [str(executable)], capture_output=True, text=True, check=False
            )
            self.assertEqual(ran.returncode, 0, ran.stderr)

    def test_connection_state_uses_module_status_only(self) -> None:
        source = (INC.parent / "Src" / "connection_manager.cpp").read_text(
            encoding="utf-8"
        )
        report_start = source.index("bool ConnectionManager::onReportReady")
        report_body = source[report_start:]
        self.assertNotIn("ConnectionLinkState::Connected", report_body)
        self.assertIn("return ok;", report_body)
        self.assertIn("kRfStatusPollMs = 500u", source)
        self.assertIn("rfTransport.pollStatus()", source)
        self.assertIn("updateRfLinkStateFromStatus();", source)

    def test_rf_xinput_and_fn_paths_are_wired(self) -> None:
        source = (
            INC.parent / "Src" / "states" / "input_state.cpp"
        ).read_text(encoding="utf-8")
        self.assertIn("requiresRfXInputPersistence", source)
        self.assertIn("STORAGE_MANAGER.setInputMode(INPUT_MODE_XINPUT)", source)
        self.assertIn("effectiveInputModeForConnection", source)
        self.assertIn("fnLayerPolicy.update", source)
        self.assertIn("fnLayerPolicy.onNeutralSubmitted", source)
        self.assertIn("HOTKEYS_MANAGER.updateHotkeyState", source)

    def test_ps5_is_the_primary_ui_entry(self) -> None:
        screen = (
            INC.parent / "Src" / "screen_control" /
            "spi_screen_detail_input_mode.cpp"
        ).read_text(encoding="utf-8")
        web = (
            ROOT / "application" / "www" / "components" /
            "input-mode-content.tsx"
        ).read_text(encoding="utf-8")
        self.assertIn("INPUT_MODE_PS5", screen)
        self.assertIn("mode == INPUT_MODE_PS4", screen)
        self.assertIn("platformForDisplay", web)


if __name__ == "__main__":
    unittest.main()
