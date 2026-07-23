from __future__ import annotations

import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
AUTH_HOST = ROOT / "RF_PHY_Hop" / "TX" / "USB" / "usb_auth_host.c"
STM_XINPUT_AUTH = (
    ROOT
    / "application"
    / "Cpp_Core"
    / "Src"
    / "drivers"
    / "xinput"
    / "XInputAuthUSBListener.cpp"
)
STM_XINPUT_ABI = (
    ROOT
    / "application"
    / "Cpp_Core"
    / "Inc"
    / "drivers"
    / "xinput"
    / "XInputAuth.hpp"
)


def macro_value(source: str, name: str) -> int:
    match = re.search(
        rf"^\s*#define\s+{re.escape(name)}\s+(0x[0-9A-Fa-f]+|\d+)u?\s*$",
        source,
        re.MULTILINE,
    )
    if match is None:
        raise AssertionError(f"missing numeric macro {name}")
    return int(match.group(1), 0)


class XInputAuthenticationContractTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.auth_host = AUTH_HOST.read_text(encoding="utf-8")
        cls.stm_auth = STM_XINPUT_AUTH.read_text(encoding="utf-8")
        cls.stm_abi = STM_XINPUT_ABI.read_text(encoding="utf-8")

    def test_request_ids_and_lengths_match_stm32_path(self) -> None:
        expected = {
            "X360_GET_SERIAL": 0x81,
            "X360_INIT_AUTH": 0x82,
            "X360_RESPOND_CHALLENGE": 0x83,
            "X360_AUTH_KEEPALIVE": 0x84,
            "X360_REQUEST_STATE": 0x86,
            "X360_VERIFY_AUTH": 0x87,
            "X360_CONSOLE_INIT_BYTES": 34,
            "X360_SERIAL_BYTES": 29,
            "X360_INIT_REPLY_BYTES": 46,
            "X360_VERIFY_BYTES": 22,
        }
        for name, value in expected.items():
            self.assertEqual(macro_value(self.auth_host, name), value, name)

        stm_lengths = {
            "X360_AUTHLEN_CONSOLE_INIT": 34,
            "X360_AUTHLEN_DONGLE_SERIAL": 29,
            "X360_AUTHLEN_DONGLE_INIT": 46,
            "X360_AUTHLEN_CHALLENGE": 22,
        }
        for name, value in stm_lengths.items():
            self.assertEqual(macro_value(self.stm_abi, name), value, name)

    def test_wvalue_and_interface_golden(self) -> None:
        expected = {
            "X360_WVALUE_CONSOLE_DATA": 0x0003,
            "X360_WVALUE_CONTROLLER_ID": 0x5B17,
            "X360_WVALUE_INIT_REPLY": 0x5C28,
            "X360_WVALUE_VERIFY_REPLY": 0x5C10,
            "X360_WINDEX_SECURITY": 0x0103,
        }
        for name, value in expected.items():
            self.assertEqual(macro_value(self.auth_host, name), value, name)

        self.assertRegex(
            self.stm_auth,
            r"\.wIndex\s*=\s*TU_U16\(\s*0x01\s*,\s*0x03\s*\)",
        )

    def test_each_host_transfer_uses_the_security_interface(self) -> None:
        self.assertRegex(
            self.auth_host,
            r"auth_setup_packet\(\s*setup\s*,\s*"
            r"input\s*\?\s*0xC1u\s*:\s*0x41u\s*,\s*"
            r"request\s*,\s*value\s*,\s*X360_WINDEX_SECURITY\s*,\s*length\s*\)",
        )

    def test_auth_sequence_golden(self) -> None:
        sequence_fragments = (
            r"auth_x360_vendor_transfer\(\s*true\s*,\s*X360_GET_SERIAL\s*,"
            r"\s*X360_WVALUE_CONTROLLER_ID\s*,\s*s_control_buffer\s*,"
            r"\s*X360_SERIAL_BYTES\s*\)",
            r"auth_x360_vendor_transfer\(\s*false\s*,"
            r"\s*s_x360_request_id\s*,\s*X360_WVALUE_CONSOLE_DATA\s*,"
            r"\s*s_x360_request\s*,\s*s_x360_request_length\s*\)",
            r"auth_x360_vendor_transfer\(\s*true\s*,"
            r"\s*X360_REQUEST_STATE\s*,\s*0u\s*,\s*s_control_buffer\s*,"
            r"\s*2u\s*\)",
            r"auth_x360_vendor_transfer\(\s*true\s*,"
            r"\s*X360_RESPOND_CHALLENGE\s*,\s*value\s*,"
            r"\s*s_control_buffer\s*,\s*reply_length\s*\)",
        )
        for fragment in sequence_fragments:
            self.assertRegex(self.auth_host, fragment)


if __name__ == "__main__":
    unittest.main()
