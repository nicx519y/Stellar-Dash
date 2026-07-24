from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
DISPATCHER = (
    ROOT / "application" / "Cpp_Core" / "Src" / "webhid_rpc_dispatcher.cpp"
)
WEBSOCKET_MANAGER = (
    ROOT
    / "application"
    / "Cpp_Core"
    / "Src"
    / "configs"
    / "websocket_command_handler.cpp"
)
FRONTEND_POLICY = (
    ROOT
    / "application"
    / "www"
    / "lib"
    / "device-transport"
    / "scope-policy.ts"
)


def _quoted_values(body: str) -> set[str]:
    return set(
        value
        for _, value in re.findall(
            r"""(["'])([^"']+)\1""",
            body,
        )
    )


def _cpp_scope_arrays(source: str) -> dict[str, set[str]]:
    return {
        name: _quoted_values(body)
        for name, body in re.findall(
            r"static const char \*const (\w+)\[\]\s*=\s*\{(.*?)\};",
            source,
            flags=re.DOTALL,
        )
    }


def _typescript_set(source: str, name: str) -> set[str]:
    match = re.search(
        rf"const {re.escape(name)}\s*=\s*new Set\(\[(.*?)\]\);",
        source,
        flags=re.DOTALL,
    )
    if match is None:
        raise AssertionError(f"TypeScript scope set {name} is missing")
    return _quoted_values(match.group(1))


class WebHidScopeMatrixTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.dispatcher_source = DISPATCHER.read_text(encoding="utf-8")
        cls.websocket_source = WEBSOCKET_MANAGER.read_text(encoding="utf-8")
        cls.frontend_source = FRONTEND_POLICY.read_text(encoding="utf-8")
        cls.matrix = _cpp_scope_arrays(cls.dispatcher_source)

    def test_every_legacy_command_has_exactly_one_webhid_scope(self) -> None:
        expected_groups = {
            "configRead",
            "configWrite",
            "monitorRead",
            "deviceControl",
            "firmwareUpdate",
        }
        self.assertEqual(set(self.matrix), expected_groups)

        classified: set[str] = {"get_device_auth"}
        for group in expected_groups:
            overlap = classified & self.matrix[group]
            self.assertFalse(
                overlap,
                f"commands classified more than once: {sorted(overlap)}",
            )
            classified |= self.matrix[group]

        registered = set(
            re.findall(
                r'registerHandler\("([^"]+)"',
                self.websocket_source,
            )
        )
        webhid_local_commands = {
            "performance.get-checkpoint",
            "performance.clock-sync",
        }
        self.assertEqual(
            classified,
            registered | webhid_local_commands,
            "Every legacy command must receive one deliberate WebHID scope",
        )

        self.assertRegex(
            self.dispatcher_source,
            r'command == "get_device_auth"[\s\S]+?'
            r"return HBOX_SCOPE_CONFIG_READ;",
        )
        self.assertRegex(
            self.dispatcher_source,
            r'command == "ping" \|\| command == "session\.end"[\s\S]+?'
            r"return HBOX_SCOPE_CONFIG_READ;",
        )

    def test_browser_elevation_policy_matches_device_dangerous_groups(
        self,
    ) -> None:
        self.assertEqual(
            _typescript_set(
                self.frontend_source,
                "DEVICE_CONTROL_COMMANDS",
            ),
            self.matrix["deviceControl"],
        )
        self.assertEqual(
            _typescript_set(
                self.frontend_source,
                "FIRMWARE_UPDATE_COMMANDS",
            ),
            self.matrix["firmwareUpdate"],
        )

    def test_binary_and_stream_scope_matrix_is_fail_closed(self) -> None:
        service = (
            ROOT / "application" / "Cpp_Core" / "Src" / "webhid_service.cpp"
        ).read_text(encoding="utf-8")
        required_fragments = (
            'strcmp(name, "firmware") == 0',
            'strcmp(name, "image") == 0',
            'strcmp(name, "config-import") == 0',
            "case 1u:\n        return HBOX_SCOPE_FIRMWARE_UPDATE;",
            "case 2u:\n        return HBOX_SCOPE_ASSET_WRITE;",
            "case 3u:\n        return HBOX_SCOPE_CONFIG_WRITE;",
            "opcode == BINARY_CMD_UPLOAD_FIRMWARE_CHUNK",
            "return HBOX_SCOPE_FIRMWARE_UPDATE;",
            "opcode >= 0x30u && opcode <= 0x33u",
            "return HBOX_SCOPE_ASSET_WRITE;",
            "opcode == 0x34u || opcode == 0x35u",
            "return HBOX_SCOPE_CONFIG_READ;",
        )
        for fragment in required_fragments:
            with self.subTest(fragment=fragment):
                self.assertIn(fragment, service)
        self.assertRegex(
            service,
            r"uint32_t binaryOpcodeScope\(uint8_t opcode\)"
            r"[\s\S]+?return 0u;\s*\}",
        )
        self.assertRegex(
            service,
            r"uint32_t streamScope\(uint8_t type\)"
            r"[\s\S]+?default:\s*return 0u;\s*\}",
        )

    def test_firmware_session_control_is_physically_confirmed_and_bound(
        self,
    ) -> None:
        service = (
            ROOT / "application" / "Cpp_Core" / "Src" / "webhid_service.cpp"
        ).read_text(encoding="utf-8")
        firmware_handler = (
            ROOT
            / "application"
            / "Cpp_Core"
            / "Src"
            / "configs"
            / "firmware_command_handler.cpp"
        ).read_text(encoding="utf-8")

        self.assertIn(
            'command == "get_firmware_upgrade_status"',
            service,
        )
        self.assertRegex(
            service,
            r"completeFirmware \|\| uploadFirmware \|\|\s*"
            r"abortFirmware \|\| statusFirmware \|\|\s*"
            r"cleanupFirmware\)\s*&&\s*"
            r"\(firmwareSessionId == nullptr \|\|\s*"
            r"!firmwareAuthorizationValid\(\s*firmwareSessionId\)",
        )
        self.assertRegex(
            service,
            r"if \(createFirmware\) \{\s*"
            r"if \(!HBoxBoard_DangerousActionConfirmed\(\)",
        )

        cleanup = re.search(
            r"WebSocketDownstreamMessage "
            r"FirmwareCommandHandler::"
            r"handleCleanupFirmwareUpgradeSession"
            r"\(.*?\)\s*\{([\s\S]+?)\n\}",
            firmware_handler,
        )
        self.assertIsNotNone(cleanup)
        cleanup_body = cleanup.group(1)
        self.assertIn(
            'cJSON_GetObjectItem(params, "session_id")',
            cleanup_body,
        )
        self.assertIn(
            "manager->AbortUpgradeSession(sessionId)",
            cleanup_body,
        )
        self.assertNotIn(
            "manager->ForceCleanupSession",
            cleanup_body,
        )

    def test_attestation_signing_key_is_not_reused_for_ecdh_validation(
        self,
    ) -> None:
        service = (
            ROOT / "application" / "Cpp_Core" / "Src" / "webhid_service.cpp"
        ).read_text(encoding="utf-8")
        crypto_header = (
            ROOT / "common" / "device_security_crypto.h"
        ).read_text(encoding="utf-8")
        attestation = re.search(
            r"bool WebHidService::handleAttestationCreate"
            r"\([\s\S]+?(?=\nbool WebHidService::handleInstallPermit)",
            service,
        )
        self.assertIsNotNone(attestation)
        attestation_body = attestation.group(0)
        self.assertIn(
            "HBoxCrypto_P256ValidatePublicKey(browserKey.data())",
            attestation_body,
        )
        self.assertNotIn(
            "HBoxCrypto_P256Ecdh",
            attestation_body,
        )
        self.assertIn(
            "HBoxCrypto_P256ValidatePublicKey",
            crypto_header,
        )
        self.assertRegex(
            service,
            r"bool WebHidService::installSessionKeys"
            r"[\s\S]+?HBoxCrypto_P256Ecdh\(\s*"
            r"deviceEphemeralPrivate\.data\(\)",
        )


if __name__ == "__main__":
    unittest.main()
