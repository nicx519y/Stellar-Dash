from __future__ import annotations

import os
import pathlib
import shutil
import subprocess
import sys
import tempfile


ROOT = pathlib.Path(__file__).resolve().parents[2]
TESTS = ROOT / "tools" / "tests"
USB = ROOT / "RF_PHY_Hop" / "TX" / "USB"
COMMON = ROOT / "common"
APP_INC = ROOT / "application" / "Cpp_Core" / "Inc"


def compiler(name: str) -> str:
    path = shutil.which(name)
    if path is None:
        raise RuntimeError(f"{name} is required for native USB tests")
    return path


def checked(command: list[str]) -> None:
    print("+", " ".join(command))
    environment = os.environ.copy()
    environment["PYTHONDONTWRITEBYTECODE"] = "1"
    subprocess.run(command, cwd=ROOT, check=True, env=environment)


def compile_and_run(
    compiler_path: str,
    standard: str,
    output: pathlib.Path,
    sources: list[pathlib.Path],
    includes: list[pathlib.Path],
) -> None:
    command = [
        compiler_path,
        f"-std={standard}",
        "-Wall",
        "-Wextra",
        "-Werror",
    ]
    command.extend(f"-I{include}" for include in includes)
    command.extend(str(source) for source in sources)
    command.extend(("-o", str(output)))
    checked(command)
    checked([str(output)])


def main() -> int:
    gcc = compiler("gcc")
    gxx = compiler("g++")

    with tempfile.TemporaryDirectory(prefix="hbox-usb-tests-") as temporary:
        output_dir = pathlib.Path(temporary)
        compile_and_run(
            gxx,
            "c++17",
            output_dir / "usb_profile_migration_test.exe",
            [
                TESTS / "usb_profile_migration_test.cpp",
                USB / "usb_legacy_descriptors.c",
                USB / "usb_ps4_features.c",
                USB / "usb_profiles.c",
            ],
            [COMMON, USB, APP_INC],
        )
        compile_and_run(
            gcc,
            "c11",
            output_dir / "usb_xbox_device_state_test.exe",
            [
                TESTS / "usb_xbox_device_state_test.c",
                USB / "usb_xbox_device.c",
                USB / "usb_gip_protocol.c",
            ],
            [USB],
        )
        compile_and_run(
            gcc,
            "c11",
            output_dir / "usb_board_link_codec_test.exe",
            [
                TESTS / "usb_board_link_codec_test.c",
                COMMON / "usb_board_link_codec.c",
            ],
            [COMMON],
        )
        compile_and_run(
            gcc,
            "c11",
            output_dir / "usb_endpoint_reset_control_test.exe",
            [TESTS / "usb_endpoint_reset_control_test.c"],
            [USB],
        )
        compile_and_run(
            gcc,
            "c11",
            output_dir / "usb_webhid_protocol_test.exe",
            [
                TESTS / "usb_webhid_protocol_test.c",
                USB / "usb_webhid.c",
            ],
            [COMMON, USB],
        )
        compile_and_run(
            gxx,
            "c++17",
            output_dir / "usb_board_link_tx_resume_test.exe",
            [
                TESTS / "usb_board_link_tx_resume_test.cpp",
                ROOT / "application" / "Cpp_Core" / "Src" / "usb_board_link.cpp",
                COMMON / "usb_board_link_codec.c",
            ],
            [TESTS / "stubs", COMMON, APP_INC],
        )
        compile_and_run(
            gcc,
            "c11",
            output_dir / "usb_webhid_board_link_test.exe",
            [
                TESTS / "usb_webhid_board_link_test.c",
                USB / "usb_net_bridge.c",
            ],
            [COMMON, USB],
        )
        compile_and_run(
            gcc,
            "c11",
            output_dir / "usb_webhid_flow_control_test.exe",
            [
                TESTS / "usb_webhid_flow_control_test.c",
                USB / "usb_device.c",
                USB / "usb_net_bridge.c",
            ],
            [COMMON, USB],
        )
        compile_and_run(
            gcc,
            "c11",
            output_dir / "usb_ncm_management_test.exe",
            [
                TESTS / "usb_ncm_management_test.c",
                USB / "usb_ncm.c",
                USB / "usb_management_control.c",
            ],
            [COMMON, USB],
        )
        compile_and_run(
            gcc,
            "c11",
            output_dir / "physical_confirmation_test.exe",
            [
                TESTS / "physical_confirmation_test.c",
                COMMON / "physical_confirmation.c",
            ],
            [COMMON],
        )

    checked(
        [
            sys.executable,
            "-m",
            "unittest",
            "discover",
            "-s",
            str(TESTS),
            "-p",
            "test_*.py",
            "-v",
        ]
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
