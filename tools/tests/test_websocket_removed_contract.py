from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]
WWW = ROOT / "application" / "www"
CPP = ROOT / "application" / "Cpp_Core"
APPLICATION = ROOT / "application"


def _source_files(root: Path, suffixes: set[str]):
    for path in root.rglob("*"):
        if path.is_file() and path.suffix.lower() in suffixes:
            yield path


class WebSocketRemovedContractTests(unittest.TestCase):
    def test_frontend_product_has_no_websocket_runtime_or_compatibility_surface(self) -> None:
        files = []
        for directory in ("app", "components", "contexts", "hooks", "lib"):
            files.extend(_source_files(WWW / directory, {".ts", ".tsx", ".js", ".mjs"}))
        files.extend(
            WWW / name
            for name in ("package.json", "README.md", ".env.example", "next.config.ts")
        )
        forbidden = re.compile(
            r"WebSocket|websocket|ws://|wss://|:8081|legacy-websocket|"
            r"legacy\.binary|wsConnected|sendWebSocket|connectWebSocket|"
            r"reconnectWebSocket|disconnectWebSocket"
        )
        violations = []
        for path in files:
            source = path.read_text(encoding="utf-8")
            match = forbidden.search(source)
            if match is not None:
                violations.append(f"{path.relative_to(ROOT)}:{match.group(0)}")
        self.assertEqual(violations, [])

        for removed in (
            WWW / "components" / "websocket-framework.tsx",
            WWW / "lib" / "websocket-types.ts",
            WWW / "lib" / "websocket-queue-manager.ts",
            WWW / "lib" / "device-transport" / "legacy-websocket-transport.ts",
            WWW / "websocket-server.js",
        ):
            self.assertFalse(removed.exists(), removed)

    def test_ui_uses_typed_firmware_and_image_operations(self) -> None:
        context = (WWW / "contexts" / "gamepad-config-context.tsx").read_text(
            encoding="utf-8"
        )
        screen = (
            WWW / "components" / "screen-control-setting-content.tsx"
        ).read_text(encoding="utf-8")
        client = (
            WWW / "lib" / "device-transport" / "device-command-client.ts"
        ).read_text(encoding="utf-8")

        for forbidden in (
            "exchangeDeviceBinary",
            "sendBinaryMessage",
            "onBinaryMessage",
            "sendBinaryFirmwareChunk",
        ):
            with self.subTest(forbidden=forbidden):
                self.assertNotIn(forbidden, context + screen)

        for method in (
            "uploadFirmwareChunk(",
            "getImageCatalog(",
            "readImage(",
            "uploadImage(",
            "deleteImage(",
        ):
            with self.subTest(method=method):
                self.assertIn(method, client)

        self.assertNotRegex(
            screen,
            r"\b0x(?:01|30|31|32|33|34|35|81|b0|b1|b2|b3|b4|b5)\b",
        )

    def test_screen_control_writes_require_device_confirmation(self) -> None:
        context = (WWW / "contexts" / "gamepad-config-context.tsx").read_text(
            encoding="utf-8"
        )
        screen = (
            WWW / "components" / "screen-control-setting-content.tsx"
        ).read_text(encoding="utf-8")

        # A click may render optimistically, but it must not remain displayed
        # as saved when the WebHID RPC is cancelled, times out, or is rejected.
        self.assertNotIn("void updateScreenControl", screen)
        self.assertGreaterEqual(screen.count("await commitUiChange("), 6)
        self.assertIn("const previous = screenControlRef.current;", context)
        self.assertIn("Device did not confirm the screen control update", context)
        self.assertIn("setScreenControl(previous);", context)

    def test_firmware_has_no_tcp_websocket_runtime(self) -> None:
        forbidden = re.compile(
            r"WebSocketServer|WebSocketConnection|WebSocketMessageQueue|"
            r"websocket_server|websocket_message_queue|tcp:?8081|:8081"
        )
        violations = []
        for path in _source_files(CPP, {".c", ".h", ".cpp", ".hpp"}):
            source = path.read_text(encoding="utf-8")
            match = forbidden.search(source)
            if match is not None:
                violations.append(f"{path.relative_to(ROOT)}:{match.group(0)}")
        self.assertEqual(violations, [])

        for removed in (
            CPP / "Inc" / "configs" / "websocket_server.hpp",
            CPP / "Src" / "configs" / "websocket_server.cpp",
            CPP / "Inc" / "configs" / "websocket_message_queue.hpp",
            CPP / "Src" / "configs" / "websocket_message_queue.cpp",
        ):
            self.assertFalse(removed.exists(), removed)

    def test_firmware_build_has_no_legacy_network_webconfig_runtime(self) -> None:
        makefile = (APPLICATION / "Makefile").read_text(encoding="utf-8")
        for forbidden in (
            "Libs/rndis",
            "Libs/stm32_mw_lwip",
            "Libs/tinyusb/lib/networking/dhserver.c",
            "Libs/tinyusb/lib/networking/dnserver.c",
            "Libs/httpd/fs.c",
            "Libs/httpd/fsdata.c",
        ):
            with self.subTest(forbidden=forbidden):
                self.assertNotIn(forbidden, makefile)

        tinyusb = (
            CPP / "Inc" / "tusb_config.h"
        ).read_text(encoding="utf-8")
        self.assertRegex(tinyusb, r"#define\s+CFG_TUD_ECM_RNDIS\s+0\b")
        self.assertRegex(tinyusb, r"#define\s+CFG_TUD_NCM\s+0\b")
        self.assertNotIn("lwipopts.h", tinyusb)

        driver_manager = (
            CPP / "Src" / "drivermanager.cpp"
        ).read_text(encoding="utf-8")
        self.assertNotIn("NetDriver", driver_manager)
        for removed in (
            CPP / "Inc" / "drivers" / "net" / "NetDriver.hpp",
            CPP / "Src" / "drivers" / "net" / "NetDriver.cpp",
            APPLICATION / "Libs" / "rndis" / "rndis.c",
            APPLICATION / "Libs" / "rndis" / "rndis.h",
            APPLICATION / "Libs" / "httpd" / "fs.c",
            APPLICATION / "Libs" / "httpd" / "fs.h",
            APPLICATION / "Libs" / "httpd" / "fscustom.h",
            APPLICATION / "Libs" / "httpd" / "fsdata.c",
            APPLICATION / "Libs" / "httpd" / "fsdata.h",
        ):
            self.assertFalse(removed.exists(), removed)

        # The immutable WebResources payload remains part of the established
        # A/B artifact layout even though no embedded HTTP server consumes it.
        self.assertTrue(
            (APPLICATION / "Libs" / "httpd" / "ex_fsdata.bin").is_file()
        )

        packer = (WWW / "makefsdata.js").read_text(encoding="utf-8")
        self.assertNotIn("fsdata.c", packer)
        self.assertNotIn("fsdataPath", packer)
        self.assertIn("ex_fsdata.bin", packer)
        self.assertIn("buildExternalWebResources", packer)

    def test_factory_is_fail_closed_to_webhid_or_explicit_mock(self) -> None:
        factory = (
            WWW / "lib" / "device-transport" / "factory.ts"
        ).read_text(encoding="utf-8")
        hosted = (
            WWW / "lib" / "device-transport" / "factory-runtime-hosted.ts"
        ).read_text(encoding="utf-8")
        mock = (
            WWW / "lib" / "device-transport" / "factory-runtime-mock.ts"
        ).read_text(encoding="utf-8")
        next_config = (WWW / "next.config.ts").read_text(encoding="utf-8")
        self.assertIn("export type DeviceTransportMode = 'webhid' | 'mock';", factory)
        self.assertIn("options.mode !== BUILD_DEVICE_TRANSPORT_MODE", factory)
        self.assertNotIn("MockDeviceTransport", factory)
        self.assertNotIn("MockDeviceTransport", hosted)
        self.assertNotIn("WebHidTransport", mock)
        self.assertIn("NEXT_PUBLIC_OFFLINE_PREVIEW", next_config)
        self.assertIn("@hbox/device-transport-runtime$", next_config)
        self.assertIn("hbox-build-variant:", next_config)
        package = (WWW / "package.json").read_text(encoding="utf-8")
        self.assertIn('"postbuild:hosted"', package)
        self.assertIn('"postbuild:mock"', package)


if __name__ == "__main__":
    unittest.main()
