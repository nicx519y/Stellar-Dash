from pathlib import Path
import shutil
from tools.tests.fault_log_test_support import run_checked
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]

class FaultLogRpcTest(unittest.TestCase):
    def test_production_rpc(self):
        cpp, cc = shutil.which("g++"), shutil.which("gcc")
        self.assertTrue(cpp and cc)
        includes = ["tools/tests/fault_log_stubs", "application/Inc/webconfig", "application/Inc/webconfig/configs",
                    "application/Libs/cJSON", "common"]
        flags = ["-std=c++17", "-Wall", "-Wextra", "-Werror", *[f"-I{ROOT / p}" for p in includes]]
        with tempfile.TemporaryDirectory(prefix="xora-log-rpc-") as folder:
            obj = Path(folder) / "json.o"
            exe = Path(folder) / "test.exe"
            print("fault RPC: compiling production handler and in-memory transport", flush=True)
            run_checked([cc, "-c", str(ROOT / "application/Libs/cJSON/cJSON.c"), "-o", str(obj)],
                           timeout=120)
            run_checked([cpp, *flags,
                str(ROOT / "application/Src/webconfig/configs/fault_log_command.cpp"),
                str(ROOT / "application/Src/webconfig/configs/device_command_message.cpp"),
                str(ROOT / "tools/tests/fault_log_rpc_test.cpp"), str(obj), "-o", str(exe)],
                timeout=120)
            run_checked([str(exe)], timeout=120)

if __name__ == "__main__":
    unittest.main()
