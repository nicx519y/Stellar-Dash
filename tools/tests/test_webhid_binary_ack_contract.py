import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class WebHidBinaryAckContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.service = (
            ROOT / "application" / "Cpp_Core" / "Src" / "webhid_service.cpp"
        ).read_text(encoding="utf-8")
        cls.image_handler = (
            ROOT
            / "application"
            / "Cpp_Core"
            / "Src"
            / "configs"
            / "user_image_command_handler.cpp"
        ).read_text(encoding="utf-8")

    def test_correlated_stream_rejection_is_a_successful_transport_envelope(self) -> None:
        stream = self.service[
            self.service.index("bool WebHidService::handleStreamRpc") :
            self.service.index("bool WebHidService::processStreamFragment")
        ]
        ack_result = stream[
            stream.index("BinaryAckStatus ackStatus") :
            stream.index("bool succeeded = true;")
        ]

        self.assertIn(
            "ackStatus == BinaryAckStatus::ProtocolError",
            ack_result,
        )
        self.assertIn(
            '"Stream consumer returned an invalid or uncorrelated ACK"',
            ack_result,
        )
        self.assertIn(
            "A correlated negative ACK is an application result",
            ack_result,
        )
        self.assertIn(
            "sendRpcResult(\n            transactionId,\n            0,",
            ack_result,
        )
        self.assertNotIn(
            "ackStatus == BinaryAckStatus::Accepted ? 0 : 422",
            ack_result,
        )

    def test_correlated_direct_rejection_is_a_successful_transport_envelope(self) -> None:
        direct = self.service[
            self.service.index("bool WebHidService::handleBinaryExchange") :
            self.service.index("bool WebHidService::handleStreamRpc")
        ]
        protocol_error = direct[
            direct.index("if (response == nullptr") :
            direct.index("const bool result = sendRpcResult")
        ]
        result_start = direct.index("const bool result = sendRpcResult")
        result = direct[
            result_start :
            direct.index("cJSON_Delete(response)", result_start)
        ]

        self.assertIn("transactionId,\n            502,", protocol_error)
        self.assertIn("A correlated negative ACK is a valid application result", direct)
        self.assertIn("transactionId,\n        0,", result)
        self.assertNotIn("BinaryAckStatus::Accepted ? 0 : 422", direct)

    def test_uncorrelated_stream_ack_remains_a_protocol_failure(self) -> None:
        stream = self.service[
            self.service.index("BinaryAckStatus ackStatus", self.service.index("bool WebHidService::handleStreamRpc")) :
            self.service.index("bool succeeded = true;")
        ]
        protocol_error = stream[
            stream.index("if (response == nullptr") :
            stream.index("const bool result = sendRpcResult")
        ]
        self.assertIn("transactionId,\n                502,", protocol_error)
        self.assertIn("clearBinaryCapture();", stream)
        self.assertIn("clearStream();", stream)

    def test_image_chunk_ack_carries_request_correlation_on_rejection(self) -> None:
        validator = self.service[
            self.service.index("BinaryAckStatus describeBinaryAck") :
            self.service.index("cJSON *createBinaryAckData")
        ]
        image_chunk = validator[
            validator.index("if (requestOpcode == kImageChunkOpcode)") :
            validator.index("cJSON_AddStringToObject(\n            ack,\n            \"kind\"")
        ]
        accepted_check = image_chunk.index("(accepted && received != offset + chunkLength)")
        offset_field = image_chunk.index('ack, "offset", offset')
        size_field = image_chunk.index('ack, "chunkSize", chunkLength')

        self.assertLess(accepted_check, offset_field)
        self.assertLess(accepted_check, size_field)
        self.assertNotIn("if (accepted)", image_chunk[offset_field:])
        self.assertIn("acknowledgedCid != requestedCid", validator)
        self.assertIn('ack, "received", received', validator)
        self.assertIn('ack, "total", total', validator)

    def test_catalog_and_read_ack_shapes_are_strict(self) -> None:
        validator = self.service[
            self.service.index("BinaryAckStatus describeBinaryAck") :
            self.service.index("cJSON *createBinaryAckData")
        ]
        catalog = validator[
            validator.index("case kImageInfoOpcode") :
            validator.index("case kImageReadOpcode")
        ]
        read = validator[
            validator.index("case kImageReadOpcode") :
            validator.index("default:", validator.index("case kImageReadOpcode"))
        ]

        self.assertIn("responseLength != expectedResponseLength", catalog)
        self.assertIn("kExtendedImageInfoResponseBytes", catalog)
        self.assertIn("kFastImageInfoResponseBytes", catalog)
        self.assertIn("requestedVersion > 2u", catalog)
        self.assertIn("response[64] != 2u", catalog)
        self.assertIn("response[64] != 3u", catalog)
        self.assertIn("response[76] != 2u", catalog)
        self.assertIn("response[77] != 44u", catalog)
        self.assertIn("loadLe16(&response[78]) != 0x0003u", catalog)
        self.assertIn("response[6] > 1u || response[7] > 1u", catalog)
        self.assertIn("response[66] > 10u", catalog)
        self.assertIn("response[7] == 1u && response[66] == 0u", catalog)
        self.assertNotIn("response[66] == 0u || response[67]", catalog)
        self.assertIn("response[22] > 32u", read)
        self.assertIn("(accepted && response[22] != 0u)", read)
        self.assertIn("!accepted && returnedChunkLength != 0u", read)

    def test_legacy_binary_stream_is_not_public(self) -> None:
        self.assertNotIn('strcmp(name, "legacy-binary")', self.service)
        self.assertNotIn("completedType == 0x7Fu", self.service)

    def test_image_handler_negative_ack_preserves_valid_request_identity(self) -> None:
        response = self.image_handler[
            self.image_handler.index("static void send_user_image_binary_response") :
            self.image_handler.index("static int8_t qspi_write_bytes")
        ]
        self.assertIn("response.cid = cid;", response)
        self.assertIn("response.received = received;", response)
        self.assertIn("response.total = total;", response)

        chunk = self.image_handler[
            self.image_handler.index("case BINARY_CMD_UPLOAD_USER_IMAGE_CHUNK") :
            self.image_handler.index("case BINARY_CMD_UPLOAD_USER_IMAGE_COMMIT")
        ]
        self.assertIn("BINARY_CMD_UPLOAD_USER_IMAGE_CHUNK_RESP", chunk)
        self.assertIn("false,\n                h->cid", chunk)
        self.assertIn('"Use IMAGE_DATA reports"', chunk)
        self.assertIn("g_user_image_upload_session.received", chunk)
        self.assertIn("g_user_image_upload_session.total", chunk)

    def test_image_reads_do_not_interrupt_a_live_qspi_mapping(self) -> None:
        reader = self.image_handler[
            self.image_handler.index("static bool qspi_read_bytes") :
            self.image_handler.index("using HBoxUserImage::HeaderV3")
        ]
        index_reader = self.image_handler[
            self.image_handler.index("static bool read_index_header_at") :
            self.image_handler.index("static bool read_index_header(")
        ]
        chunk_reader = self.image_handler[
            self.image_handler.index("case BINARY_CMD_READ_BG_IMAGE_CHUNK") :
            self.image_handler.index("default:", self.image_handler.index("case BINARY_CMD_READ_BG_IMAGE_CHUNK"))
        ]

        self.assertIn("QSPI_W25Qxx_IsMemoryMappedMode()", reader)
        self.assertIn("SCB_InvalidateDCache_by_Addr", reader)
        self.assertIn("memcpy(destination", reader)
        self.assertIn("QSPI_W25Qxx_ReadBuffer", reader)
        self.assertIn("length > W25Qxx_FlashSize", reader)
        self.assertIn("flashOffset > W25Qxx_FlashSize - length", reader)
        self.assertIn("qspi_read_bytes(address, &out", index_reader)
        self.assertIn("HBoxUserImage::validateStructure", index_reader)
        self.assertIn("payloadCrc == out.payload_crc32", index_reader)
        self.assertNotIn("QSPIXipGuard", index_reader)
        self.assertIn("qspi_read_bytes(", chunk_reader)
        self.assertNotIn("QSPIXipGuard", chunk_reader)


if __name__ == "__main__":
    unittest.main()
