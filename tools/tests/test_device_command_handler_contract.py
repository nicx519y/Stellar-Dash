import json
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CASE_MANIFEST = ROOT / "tools" / "device_command_contract_cases.json"
COMMAND_MANIFEST = ROOT / "tools" / "webhid_command_manifest.json"


class DeviceCommandHandlerContractTest(unittest.TestCase):
    def test_case_manifest_matches_all_production_commands(self):
        migration = json.loads(COMMAND_MANIFEST.read_text(encoding="utf-8"))
        cases = json.loads(CASE_MANIFEST.read_text(encoding="utf-8"))
        migrated_names = {
            command["name"]
            for group in migration["groups"]
            for command in group["commands"]
            if command["name"] != "ping"
        }
        case_names = [case["name"] for case in cases["commands"]]

        self.assertEqual(68, len(case_names))
        self.assertEqual(68, len(set(case_names)))
        self.assertEqual(migrated_names, set(case_names))
        self.assertIn("ping", cases)
        for case in cases["commands"]:
            self.assertIn("validParams", case, case["name"])
            self.assertIn("validErrNo", case, case["name"])
            self.assertIn("requiredDataFields", case, case["name"])
            self.assertIn("invalid", case, case["name"])
            self.assertIn("errNo", case["invalid"], case["name"])

    def test_real_dispatcher_and_handlers_execute_every_case(self):
        gxx = shutil.which("g++")
        gcc = shutil.which("gcc")
        self.assertIsNotNone(gxx, "host g++ is required for real handler contracts")
        self.assertIsNotNone(gcc, "host gcc is required for the production cJSON source")

        production_sources = [
            "application/Cpp_Core/Src/configs/device_command_handler.cpp",
            "application/Cpp_Core/Src/configs/device_command_message.cpp",
            "application/Cpp_Core/Src/webhid_rpc_dispatcher.cpp",
            "application/Cpp_Core/Src/configs/global_config_command_handler.cpp",
            "application/Cpp_Core/Src/configs/profile_command_handler.cpp",
            "application/Cpp_Core/Src/configs/ms_mark_command_handler.cpp",
            "application/Cpp_Core/Src/configs/calibration_command_handler.cpp",
            "application/Cpp_Core/Src/configs/common_command_handler.cpp",
            "application/Cpp_Core/Src/configs/firmware_command_handler.cpp",
        ]
        for source in production_sources:
            self.assertNotIn(
                "HBOX_DEVICE_COMMAND_CONTRACT_TEST",
                (ROOT / source).read_text(encoding="utf-8"),
                f"handler-entry test short circuit is forbidden: {source}",
            )

        include_dirs = [
            "tools/tests/device_command_stubs",
            "application/Core/Inc",
            "application/Drivers/STM32H7xx_HAL_Driver/Inc",
            "application/Drivers/STM32H7xx_HAL_Driver/Inc/Legacy",
            "application/Drivers/CMSIS/Device/ST/STM32H7xx/Include",
            "application/Drivers/CMSIS/Include",
            "application/Cpp_Core/Inc",
            "application/Cpp_Core/Inc/configs",
            "application/Cpp_Core/Inc/firmware",
            "application/Drivers/QSPI-W25Q64",
            "application/Libs/cJSON",
            "common",
        ]
        common_flags = [
            "-w",
            "-std=c++17",
            "-fpermissive",
            "-DSTM32H750xx",
            "-DAPPLICATION_STARTUP_LOG=0",
            *[f"-I{ROOT / directory}" for directory in include_dirs],
        ]

        with tempfile.TemporaryDirectory(prefix="hbox-device-contract-") as tmp:
            build = Path(tmp)
            objects = []
            all_cpp_sources = production_sources + [
                "tools/tests/device_command_contract_stubs.cpp",
                "tools/tests/device_command_handler_contract_test.cpp",
            ]
            for index, source in enumerate(all_cpp_sources):
                output = build / f"source-{index}.o"
                completed = subprocess.run(
                    [gxx, *common_flags, "-c", str(ROOT / source), "-o", str(output)],
                    cwd=ROOT,
                    capture_output=True,
                    text=True,
                )
                self.assertEqual(
                    0,
                    completed.returncode,
                    f"host compile failed for {source}:\n{completed.stdout}\n{completed.stderr}",
                )
                objects.append(output)

            cjson_object = build / "cJSON.o"
            completed = subprocess.run(
                [
                    gcc,
                    "-w",
                    f"-I{ROOT / 'application/Libs/cJSON'}",
                    "-c",
                    str(ROOT / "application/Libs/cJSON/cJSON.c"),
                    "-o",
                    str(cjson_object),
                ],
                cwd=ROOT,
                capture_output=True,
                text=True,
            )
            self.assertEqual(0, completed.returncode, completed.stderr)
            objects.append(cjson_object)

            executable = build / "device-command-contract.exe"
            completed = subprocess.run(
                [gxx, *map(str, objects), "-o", str(executable)],
                cwd=ROOT,
                capture_output=True,
                text=True,
            )
            self.assertEqual(0, completed.returncode, completed.stderr)
            completed = subprocess.run(
                [str(executable), str(CASE_MANIFEST)],
                cwd=ROOT,
                capture_output=True,
                text=True,
            )
            self.assertEqual(
                0,
                completed.returncode,
                f"real handler contract executable failed:\n{completed.stdout}\n{completed.stderr}",
            )
            self.assertIn(
                "real handler contracts passed: 68/68; binary zero-copy, retired tombstone handler and ping passed separately",
                completed.stdout,
            )


if __name__ == "__main__":
    unittest.main()
