import pathlib
import shutil
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
INC = ROOT / "application" / "Cpp_Core" / "Inc"


class WebHidConfigWritePolicyTest(unittest.TestCase):
    def test_native_policy(self) -> None:
        compiler = shutil.which("g++") or shutil.which("clang++")
        self.assertIsNotNone(compiler, "host C++ compiler is required")

        with tempfile.TemporaryDirectory() as temp:
            executable = pathlib.Path(temp) / "webhid_config_write_policy_test"
            compiled = subprocess.run(
                [
                    compiler,
                    "-std=c++17",
                    "-I",
                    str(INC),
                    str(ROOT / "tools" / "tests" /
                        "webhid_config_write_policy_test.cpp"),
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

    def test_dispatcher_applies_policy_before_handlers(self) -> None:
        source = (
            ROOT / "application" / "Cpp_Core" / "Src" /
            "webhid_rpc_dispatcher.cpp"
        ).read_text(encoding="utf-8")
        gate = source.index("webhidShouldBlockConfigWrite")
        dispatch = source.index("processCommand(request)")
        self.assertLess(gate, dispatch)
        self.assertIn("WEBCONFIG_BTNS_MANAGER.isActive()", source)
        self.assertIn("monitor-active", source)


if __name__ == "__main__":
    unittest.main()
