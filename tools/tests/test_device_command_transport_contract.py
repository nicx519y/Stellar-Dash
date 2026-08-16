from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CPP = ROOT / "application" / "Cpp_Core"
CORE = ROOT / "application" / "Core"


class DeviceCommandTransportContractTests(unittest.TestCase):
    def test_legacy_websocket_runtime_is_deleted(self) -> None:
        removed = (
            CPP / "Inc" / "configs" / "websocket_server.hpp",
            CPP / "Src" / "configs" / "websocket_server.cpp",
            CPP / "Inc" / "configs" / "websocket_message_queue.hpp",
            CPP / "Src" / "configs" / "websocket_message_queue.cpp",
            CPP / "Inc" / "configs" / "webconfig.hpp",
            CPP / "Src" / "configs" / "webconfig.cpp",
            CPP / "Inc" / "configmanager.hpp",
            CPP / "Src" / "configmanager.cpp",
        )
        self.assertTrue(all(not path.exists() for path in removed))

        product_source = "\n".join(
            path.read_text(encoding="utf-8", errors="ignore")
            for base in (CPP, CORE)
            for path in base.rglob("*")
            if path.suffix in {".c", ".cpp", ".h", ".hpp"}
        )
        for forbidden in (
            "WebSocketServer",
            "websocket_server",
            "WebSocketMessageQueue",
            "websocket_message_queue",
            "tcp:8081",
            ":8081",
        ):
            with self.subTest(forbidden=forbidden):
                self.assertNotIn(forbidden, product_source)

    def test_command_dispatch_is_transport_neutral(self) -> None:
        header = (
            CPP / "Inc" / "configs" / "device_command_handler.hpp"
        ).read_text(encoding="utf-8")
        registry = (
            CPP / "Src" / "configs" / "device_command_handler.cpp"
        ).read_text(encoding="utf-8")
        message = (
            CPP / "Inc" / "configs" / "device_command_message.hpp"
        ).read_text(encoding="utf-8")
        self.assertIn("class DeviceCommandHandler", header)
        self.assertIn("class DeviceCommandDispatcher", header)
        self.assertIn("class DeviceCommandRequest", message)
        self.assertIn("class DeviceCommandResponse", message)
        self.assertEqual(registry.count("registerHandler(\""), 59)
        self.assertNotIn("Connection", message)

    def test_config_transport_has_only_registered_hid_sinks(self) -> None:
        source = (CPP / "Src" / "config_transport_sink.cpp").read_text(
            encoding="utf-8"
        )
        header = (CPP / "Inc" / "config_transport_sink.hpp").read_text(
            encoding="utf-8"
        )
        self.assertIn("jsonSink(json, length)", source)
        self.assertIn("binarySink(data, length)", source)
        self.assertNotIn("broadcast", source)
        self.assertNotIn("Connection", header)

    def test_weak_device_auth_is_only_a_410_tombstone(self) -> None:
        handler = (
            CPP / "Src" / "configs" / "firmware_command_handler.cpp"
        ).read_text(encoding="utf-8")
        utility_source = (CORE / "Src" / "utils.c").read_text(
            encoding="utf-8"
        )
        utility_header = (CORE / "Inc" / "utils.h").read_text(
            encoding="utf-8"
        )
        self.assertIn('"Legacy weak device authentication is disabled"', handler)
        self.assertNotIn("createDeviceAuthJSON", handler)
        self.assertNotIn("str_stm32_unique_id", utility_source + utility_header)
        self.assertNotIn("get_device_id_hash", utility_source + utility_header)

    def test_binary_handlers_reply_without_connection_objects(self) -> None:
        firmware_header = (
            CPP / "Inc" / "configs" / "firmware_command_handler.hpp"
        ).read_text(encoding="utf-8")
        image_header = (
            CPP / "Inc" / "configs" / "user_image_command_handler.hpp"
        ).read_text(encoding="utf-8")
        self.assertNotIn("Connection", firmware_header)
        self.assertNotIn("Connection", image_header)

    def test_binary_firmware_handler_borrows_read_only_payload(self) -> None:
        handler = (
            CPP / "Src" / "configs" / "firmware_command_handler.cpp"
        ).read_text(encoding="utf-8")
        manager_header = (
            CPP / "Inc" / "firmware" / "firmware_manager.hpp"
        ).read_text(encoding="utf-8")
        binary_function = handler.split(
            "bool FirmwareCommandHandler::handleBinaryFirmwareChunk", 1
        )[1].split(
            "void FirmwareCommandHandler::sendBinaryChunkResponse", 1
        )[0]
        self.assertIn("chunk.data = data + payload_offset;", binary_function)
        self.assertNotIn("malloc(", binary_function)
        self.assertNotIn("std::string sessionId", binary_function)
        self.assertIn("char sessionId[sizeof(header->session_id) + 1u]", binary_function)
        self.assertIn("const uint8_t* data;", manager_header)

    def test_legacy_performance_snapshot_is_not_published(self) -> None:
        source = (
            CPP / "Src" / "configs" / "common_command_handler.cpp"
        ).read_text(encoding="utf-8")
        self.assertNotIn("sendButtonPerformanceMonitoringNotification", source)
        self.assertNotIn("buildButtonPerformanceMonitoringBinaryData", source)

    def test_large_rpc_response_uses_preallocated_single_ownership_serialization(self) -> None:
        dispatcher = (
            CPP / "Src" / "webhid_rpc_dispatcher.cpp"
        ).read_text(encoding="utf-8")
        response_header = (
            CPP / "Inc" / "configs" / "device_command_message.hpp"
        ).read_text(encoding="utf-8")
        self.assertIn("cJSON_PrintPreallocated", dispatcher)
        self.assertIn("serializedResponse", dispatcher)
        self.assertIn("response.releaseData()", dispatcher)
        self.assertIn("releaseData()", response_header)
        self.assertNotIn("cJSON_Duplicate(response.getData()", dispatcher)
        self.assertNotIn("cJSON_PrintUnformatted", dispatcher)

        service_header = (
            CPP / "Inc" / "webhid_service.hpp"
        ).read_text(encoding="utf-8")
        service = (
            CPP / "Src" / "webhid_service.cpp"
        ).read_text(encoding="utf-8")
        self.assertIn("outboundStorage", service_header)
        self.assertIn("responseScratch", service_header)
        self.assertIn("cJSON_PrintPreallocated", service)
        self.assertIn("cJSON_AddItemReferenceToObject", service)
        self.assertNotIn("std::vector<uint8_t> bytes;", service_header)
        self.assertNotIn("rpcExplicitSuccess", service)


if __name__ == "__main__":
    unittest.main()
