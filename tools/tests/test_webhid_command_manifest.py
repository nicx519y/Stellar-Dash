import json
import re
import unittest
from collections import Counter
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MANIFEST = ROOT / "tools" / "webhid_command_manifest.json"
COMMAND_REGISTRY = (
    ROOT
    / "application"
    / "Cpp_Core"
    / "Src"
    / "configs"
    / "device_command_handler.cpp"
)
HID_DISPATCHER = (
    ROOT
    / "application"
    / "Cpp_Core"
    / "Src"
    / "webhid_rpc_dispatcher.cpp"
)
FRONTEND_SCOPE_POLICY = (
    ROOT
    / "application"
    / "www"
    / "lib"
    / "device-transport"
    / "scope-policy.ts"
)


def _manifest_commands(manifest: dict) -> list[dict]:
    result = []
    for group in manifest["groups"]:
        for command in group["commands"]:
            result.append({**command, "scope": group["scope"]})
    return result


def _cpp_string_array(source: str, name: str) -> set[str]:
    match = re.search(
        rf"static const char \*const {re.escape(name)}\[\] = \{{(.*?)\}};",
        source,
        re.DOTALL,
    )
    if match is None:
        raise AssertionError(f"missing C++ command array: {name}")
    return set(re.findall(r'"([^"]+)"', match.group(1)))


def _typescript_set(source: str, name: str) -> set[str]:
    match = re.search(
        rf"const {re.escape(name)} = new Set\(\[(.*?)\]\);",
        source,
        re.DOTALL,
    )
    if match is None:
        raise AssertionError(f"missing TypeScript command set: {name}")
    return set(re.findall(r"'([^']+)'", match.group(1)))


class WebHidCommandManifestTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
        cls.commands = _manifest_commands(cls.manifest)
        cls.by_name = {item["name"]: item for item in cls.commands}

    def test_manifest_has_all_sixty_unique_legacy_commands(self) -> None:
        names = [item["name"] for item in self.commands]
        self.assertEqual(self.manifest["version"], 1)
        self.assertEqual(self.manifest["legacyCommandCount"], 60)
        self.assertEqual(len(names), 60)
        self.assertEqual(len(set(names)), 60)
        self.assertEqual(
            Counter(item["migration"] for item in self.commands),
            Counter(self.manifest["migrationStatusCounts"]),
        )
        self.assertEqual(self.by_name["export_all_config"]["migration"], "equivalent")
        self.assertEqual(self.by_name["get_device_auth"]["migration"], "retired")

    def test_manifest_matches_firmware_command_registry(self) -> None:
        source = COMMAND_REGISTRY.read_text(encoding="utf-8")
        registered = set(re.findall(r'registerHandler\("([^"]+)"', source))
        self.assertEqual(len(registered), 59)
        self.assertEqual(set(self.by_name) - {"ping"}, registered)

    def test_manifest_scopes_match_webhid_dispatcher(self) -> None:
        source = HID_DISPATCHER.read_text(encoding="utf-8")
        firmware_scopes = {
            "config.read": _cpp_string_array(source, "configRead")
            | {"ping", "get_device_auth"},
            "config.write": _cpp_string_array(source, "configWrite"),
            "monitor.read": _cpp_string_array(source, "monitorRead")
            - {"performance.get-checkpoint", "performance.clock-sync"},
            "device.control": _cpp_string_array(source, "deviceControl"),
            "firmware.update": _cpp_string_array(source, "firmwareUpdate"),
        }
        manifest_scopes = {
            scope: {
                item["name"]
                for item in self.commands
                if item["scope"] == scope
            }
            for scope in firmware_scopes
        }
        self.assertEqual(manifest_scopes, firmware_scopes)

    def test_frontend_elevated_scope_policy_matches_manifest(self) -> None:
        source = FRONTEND_SCOPE_POLICY.read_text(encoding="utf-8")
        self.assertEqual(
            _typescript_set(source, "DEVICE_CONTROL_COMMANDS"),
            {
                item["name"]
                for item in self.commands
                if item["scope"] == "device.control"
            },
        )
        self.assertEqual(
            _typescript_set(source, "FIRMWARE_UPDATE_COMMANDS"),
            {
                item["name"]
                for item in self.commands
                if item["scope"] == "firmware.update"
            },
        )

    def test_all_binary_requests_have_unique_hid_paths_and_scopes(self) -> None:
        requests = self.manifest["binaryRequests"]
        self.assertEqual(len(requests), 7)
        self.assertEqual(len({item["opcode"] for item in requests}), 7)
        self.assertEqual(
            {item["opcode"] for item in requests},
            {"0x01", "0x30", "0x31", "0x32", "0x33", "0x34", "0x35"},
        )
        self.assertTrue(all(item["webhidPath"] for item in requests))
        self.assertTrue(all(item["scope"] for item in requests))


if __name__ == "__main__":
    unittest.main()
