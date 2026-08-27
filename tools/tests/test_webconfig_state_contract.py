from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]


class WebConfigStateContractTests(unittest.TestCase):
    def test_webhid_callback_is_ready_before_ch585_exposes_usb(self) -> None:
        state = (
            ROOT / "application" / "Cpp_Core" / "Src" / "states" /
            "webconfig_state.cpp"
        ).read_text(encoding="utf-8")
        driver = (
            ROOT / "application" / "Cpp_Core" / "Src" / "usbdriver.cpp"
        ).read_text(encoding="utf-8")
        service = (
            ROOT / "application" / "Cpp_Core" / "Src" /
            "webhid_service.cpp"
        ).read_text(encoding="utf-8")

        enter = state[
            state.index("bool WebConfigState::enter()"):
            state.index("void WebConfigState::tick()")
        ]
        prepare = enter.index("USB_DRIVER.prepare(InputMode::INPUT_MODE_CONFIG)")
        setup = enter.index("WEBHID_SERVICE.setup()")
        connect = enter.index("USB_DRIVER.connect()")
        self.assertLess(prepare, setup)
        self.assertLess(setup, connect)
        self.assertNotIn("USB_DRIVER.start(InputMode::INPUT_MODE_CONFIG)", enter)

        setup_body = service[
            service.index("bool WebHidService::setup()"):
            service.index("void WebHidService::shutdown()")
        ]
        self.assertIn("USB_DRIVER.isPrepared()", setup_body)
        self.assertIn("UsbBoardLink_SetWebConfigReceiveCallback", setup_body)

        start = driver[
            driver.index("bool USBDriver::start"):
            driver.index("bool USBDriver::prepare")
        ]
        self.assertIn("prepare(inputMode) && connect()", start)

    def test_webconfig_receive_credit_requires_a_live_consumer(self) -> None:
        source = (
            ROOT / "application" / "Cpp_Core" / "Src" /
            "usb_board_link.cpp"
        ).read_text(encoding="utf-8")

        initial = source[
            source.index("bool UsbBoardLink::grantInitialReceiveCredits"):
            source.index("void UsbBoardLink::returnReceiveCredit")
        ]
        self.assertIn("s_webConfigRxCallback == nullptr", initial)
        self.assertIn("? 0u", initial)

        setter = source[
            source.index("void UsbBoardLink::setWebConfigReceiverReady"):
            source.index("void UsbBoardLink::flushReceiveCredits")
        ]
        self.assertIn("ready ? bulkCreditLimit(channel) : 0u", setter)
        self.assertIn("flushReceiveCredits();", setter)

        callback = source[
            source.index("UsbBoardLink_SetWebConfigReceiveCallback"):
            source.index("extern \"C\" void UsbBoardLink_Process")
        ]
        self.assertIn("setWebConfigReceiverReady(callback != nullptr)", callback)

    def test_webhid_scope_upgrade_does_not_look_like_a_disconnect(self) -> None:
        source = (
            ROOT / "application" / "www" / "lib" /
            "device-transport" / "device-command-client.ts"
        ).read_text(encoding="utf-8")

        state_handler = source[
            source.index("private handleTransportState"):
            source.index("private invalidateExternalDisconnect")
        ]
        self.assertIn("if (this.scopeUpgrade)", state_handler)

        ensure_scopes = source[
            source.index("private async ensureScopes"):
            source.index("private assertLifecycleActive")
        ]
        publish = ensure_scopes.index("this.scopeUpgrade = upgrade;")
        reauthorize = ensure_scopes.index(".reauthorize(")
        self.assertLess(publish, reauthorize)

    def test_stale_webhid_bootstrap_gets_two_bounded_resync_retries(self) -> None:
        client_source = (
            ROOT / "application" / "www" / "lib" /
            "device-transport" / "device-command-client.ts"
        ).read_text(encoding="utf-8")
        transport_source = (
            ROOT / "application" / "www" / "lib" /
            "device-transport" / "webhid-transport.ts"
        ).read_text(encoding="utf-8")

        # One physical HID handle may perform at most three complete
        # authentication attempts: the original attempt plus two clean
        # logical-generation resynchronizations.  All attempts still share
        # DeviceCommandClient's original startup AbortSignal/deadline.
        self.assertIn("const MAX_BOOTSTRAP_RESYNCHRONIZATIONS = 2;", client_source)
        self.assertIn("for (;;)", client_source)
        self.assertIn("authenticationAttempt += 1;", client_source)
        self.assertIn(
            "authenticationAttempt > MAX_BOOTSTRAP_RESYNCHRONIZATIONS",
            client_source,
        )
        self.assertIn("authController.signal", client_source)
        self.assertIn("this.transport.resynchronizeBootstrap();", client_source)

        # Retrying is type-based, not message/command regex matching.  The
        # transport creates the marker only after every cleartext bootstrap
        # fragment was written and the response itself timed out.  Queued or
        # writing timeouts, aborts and authenticated/ordinary RPCs therefore
        # remain fail-closed.
        self.assertIn(
            "error instanceof RecoverableBootstrapResponseTimeoutError",
            client_source,
        )
        self.assertIn(
            "export class RecoverableBootstrapResponseTimeoutError",
            transport_source,
        )
        self.assertIn("'attestation.create'", transport_source)
        self.assertIn("'session.install-permit'", transport_source)
        recoverable = transport_source[
            transport_source.index("const mayResynchronizeBootstrap ="):
            transport_source.index("if (mayResynchronizeBootstrap)")
        ]
        self.assertIn("collection === this.pendingBootstrap", recoverable)
        self.assertIn("type === SecureHidFrameType.BOOTSTRAP_REQUEST", recoverable)
        self.assertIn("!secure", recoverable)
        self.assertIn("RECOVERABLE_BOOTSTRAP_COMMANDS.has(command)", recoverable)
        self.assertIn("pending.record.phase === 'awaiting-response'", recoverable)
        self.assertIn("normalized.code === 'timeout'", recoverable)

    def test_new_bootstrap_preempts_stale_session_without_clearing_usb_endpoint(self) -> None:
        source = (
            ROOT / "application" / "Cpp_Core" / "Src" /
            "webhid_service.cpp"
        ).read_text(encoding="utf-8")
        process = source[
            source.index("bool WebHidService::processReport"):
            source.index("bool WebHidService::acceptLogicalFragment")
        ]

        self.assertIn("report.sequence_le == 1u", process)
        self.assertIn("resetSession(true, false, false);", process)
        self.assertLess(
            process.index("resetSession(true, false, false);"),
            process.index("if (secure != sessionEstablished)"),
        )
        takeover = process[
            process.index("if (!secure &&"):
            process.index("resetSession(true, false, false);")
        ]
        self.assertNotIn("sessionEstablished", takeover)
        self.assertIn("report.type == WEBHID_REPORT_BOOTSTRAP_REQUEST", takeover)
        self.assertIn("allZero(report.tag", takeover)

    def test_webhid_unready_cleanup_never_requests_clear_fault(self) -> None:
        source = (
            ROOT / "application" / "Cpp_Core" / "Src" /
            "webhid_service.cpp"
        ).read_text(encoding="utf-8")
        process = source[
            source.index("void WebHidService::process()"):
            source.index("bool WebHidService::processReport")
        ]

        self.assertIn("!USB_DRIVER.isMounted()", process)
        unready = process[
            process.index("if (!USB_DRIVER.isReady()"):
            process.index("if (USB_DRIVER.isSuspended())")
        ]
        self.assertIn("resetSession(true, false);", unready)
        self.assertNotIn("resetSession(true);", unready)

    def test_webhid_suspend_pauses_without_resetting_the_session(self) -> None:
        source = (
            ROOT / "application" / "Cpp_Core" / "Src" /
            "webhid_service.cpp"
        ).read_text(encoding="utf-8")
        process = source[
            source.index("void WebHidService::process()"):
            source.index("bool WebHidService::processReport")
        ]

        suspended = process.index("if (USB_DRIVER.isSuspended())")
        resume = process.index("if (sessionEstablished", suspended)
        suspended_branch = process[suspended:resume]
        self.assertIn("return;", suspended_branch)
        self.assertNotIn("resetSession", suspended_branch)

    def test_stm32_board_link_preserves_partial_tx_on_suspend(self) -> None:
        source = (
            ROOT / "application" / "Cpp_Core" / "Src" /
            "usb_board_link.cpp"
        ).read_text(encoding="utf-8")
        handler = source[
            source.index("if ((command == USB_BOARD_EVT_USB_STATE)"):
            source.index("} else if ((command == USB_BOARD_EVT_BULK_CREDIT)")
        ]
        unmounted = handler[
            handler.index("if (updated.device_mounted == 0u)"):
            handler.index("} else if (updated.device_suspended != 0u)")
        ]
        suspended = handler[
            handler.index("} else if (updated.device_suspended != 0u)"):
            handler.index("usbState = updated;")
        ]
        self.assertIn("resetWebConfigTransmit();", unmounted)
        self.assertNotIn("resetWebConfigTransmit();", suspended)
        self.assertIn("credits[USB_BOARD_CHANNEL_WEBCONFIG] = 0u;", suspended)

    def test_ch585_suspend_withholds_credit_without_clearing_queues(self) -> None:
        device = (
            ROOT / "RF_PHY_Hop" / "TX" / "USB" / "usb_device.c"
        ).read_text(encoding="utf-8")
        board_link = (
            ROOT / "RF_PHY_Hop" / "TX" / "USB" / "usb_board_link.c"
        ).read_text(encoding="utf-8")
        sync = device[
            device.index("static void sync_webhid_link_capacity"):
            device.index("static usb_auth_scheme_t auth_scheme_for_profile")
        ]
        suspend_branch = sync[
            sync.index("(mounted != 0u) && (suspended != 0u)"):
            sync.index("else", sync.index("(mounted != 0u) && (suspended != 0u)"))
        ]
        pause = board_link[
            board_link.index("void usb_board_link_webconfig_pause"):
            board_link.index("void usb_board_link_webconfig_report_consumed")
        ]
        self.assertIn("usb_board_link_webconfig_pause();", suspend_branch)
        self.assertNotIn("clear_webhid_queue", suspend_branch)
        self.assertIn("s_webconfig_credit_paused = 1u;", pause)
        self.assertNotIn("usb_net_bridge_set_credit", pause)
        self.assertNotIn("usb_net_bridge_reset_channel", pause)
        capacity = device[
            device.index("static uint8_t webhid_available_reports"):
            device.index("void usb_device_transport_reset")
        ]
        self.assertIn(
            "usb_net_bridge_message_active(USB_BOARD_CHANNEL_WEBCONFIG)",
            capacity,
        )
        dirty = board_link[
            board_link.index("static void mark_credit_dirty"):
            board_link.index("static void queue_one_credit")
        ]
        self.assertIn("channel == USB_BOARD_CHANNEL_WEBCONFIG", dirty)
        self.assertIn("return;", dirty)

        publish = board_link[
            board_link.index("static void queue_one_credit"):
            board_link.index("static void handle_caps")
        ]
        webconfig_skip = publish[
            publish.index("if(channel == USB_BOARD_CHANNEL_WEBCONFIG)"):
            publish.index("if((s_credit_dirty_mask & mask) == 0u)")
        ]
        self.assertIn("s_credit_dirty_mask &=", webconfig_skip)
        self.assertIn("continue;", webconfig_skip)
        self.assertNotIn("queue_event", webconfig_skip)

        query = board_link[
            board_link.index(
                "bool usb_management_control_hw_get_webconfig_credit"
            ):
        ]
        self.assertIn("s_webconfig_credit_paused != 0u", query)
        self.assertIn("? 0u", query)

    def test_ch585_webhid_out_is_authoritative_resume_evidence(self) -> None:
        source = (
            ROOT / "RF_PHY_Hop" / "TX" / "USB" /
            "usb_device_port_ch585.c"
        ).read_text(encoding="utf-8")
        endpoint = source[
            source.index("static void complete_out_endpoint"):
            source.index("void USB2_DEVICE_IRQHandler")
        ]

        webconfig = endpoint.index(
            "s_profile == USB_BOARD_PROFILE_WEB_CONFIG"
        )
        clear_suspend = endpoint.index("s_suspended = 0u;", webconfig)
        enqueue = endpoint.index("webhid_out_enqueue", webconfig)
        self.assertLess(clear_suspend, enqueue)

    def test_reconnect_modal_opens_the_chooser_only_after_permission_is_required(self) -> None:
        source = (
            ROOT / "application" / "www" / "app" / "layout.tsx"
        ).read_text(encoding="utf-8")
        reconnect = source[
            source.index("onReconnect: async"):
            source.index("isLoading: isReconnecting")
        ]

        self.assertIn("reconnectRequiresPermission(deviceError)", reconnect)
        self.assertIn("await connectDevice();", reconnect)
        self.assertIn("await reconnectDevice();", reconnect)
        self.assertLess(
            reconnect.index("reconnectRequiresPermission(deviceError)"),
            reconnect.index("await connectDevice();"),
        )

    def test_hosted_webconfig_requests_its_startup_control_scope_up_front(self) -> None:
        source = (
            ROOT
            / "application"
            / "www"
            / "lib"
            / "device-transport"
            / "factory-runtime-hosted.ts"
        ).read_text(encoding="utf-8")
        self.assertIn("'device.control'", source)
        constructor = source[source.rindex("return new DeviceCommandClient("):]
        self.assertIn("transport,", constructor)
        self.assertIn("auth,", constructor)
        self.assertIn("initialScopes,", constructor)
        self.assertIn("config.startupTimeoutMs,", constructor)

    def test_unlocked_bootloader_still_prepares_volatile_webhid_identity(self) -> None:
        source = (
            ROOT / "bootloader" / "Core" / "Src" / "main.c"
        ).read_text(encoding="utf-8")
        lifecycle_end = source.index("#endif", source.index("B11"))
        prepare = source.index("BootAttestation_Prepare(&metadata)")
        jump = source.index("BOOT_DBG(\"Jumping to slot", prepare)
        self.assertLess(lifecycle_end, prepare)
        self.assertLess(prepare, jump)
        body = source[prepare:jump]
        self.assertIn("HBOX_SECURE_BOOT_REQUIRED", body)
        self.assertIn("Unlocked development handoff continues", body)
        self.assertIn("never changes Option Bytes", source)

    def test_webhid_lab_identity_fallback_is_read_only_and_test_gated(self) -> None:
        source = (
            ROOT / "application" / "Cpp_Core" / "Src" /
            "webhid_service.cpp"
        ).read_text(encoding="utf-8")
        start = source.index("#if WEBCONFIG_TEST_FORCE_BOOT")
        end = source.index("#endif", start)
        fallback = source[start:end]
        self.assertIn("HBOX_DEVICE_IDENTITY_REGION_ADDRESS", fallback)
        self.assertIn("read_flashword = developmentIdentityRead", fallback)
        self.assertNotIn("HAL_FLASH", fallback)
        self.assertNotIn("program_flashword", fallback)
        self.assertNotIn("OPTION", fallback.upper())

    def test_ch585_ready_pulse_is_a_hint_but_role_ack_is_authoritative(self) -> None:
        source = (
            ROOT / "application" / "Cpp_Core" / "Src" /
            "ch585_role_bootstrap.cpp"
        ).read_text(encoding="utf-8")
        wait = source.index("RFBootReady::waitForModuleReady")
        fallback = source.index("CH585 ready pulse not observed", wait)
        select = source.index("selectOnce(requestedRole)", fallback)
        self.assertLess(wait, fallback)
        self.assertLess(fallback, select)
        self.assertNotIn("continue;", source[wait:select])
        self.assertIn("ROLE_SELECTED remains the authoritative commit", source)

    def test_stlink_internal_flash_configs_use_openocd_khz_units(self) -> None:
        configs = (
            ROOT / "bootloader" / "Openocd_Script" / "ST-LINK-FLASH.cfg",
            ROOT / "application" / "Openocd_Script" / "ST-LINK-FLASH.cfg",
            ROOT / "tools" / "openocd_configs" / "ST-LINK-FLASH.cfg",
        )
        for config in configs:
            with self.subTest(config=config):
                source = config.read_text(encoding="utf-8")
                self.assertIn("adapter speed 10000", source)
                self.assertNotIn("adapter speed 10000000", source)

    def test_qspi_failure_is_fail_closed_before_runtime_becomes_ready(self) -> None:
        source = (
            ROOT
            / "application"
            / "Cpp_Core"
            / "Src"
            / "states"
            / "webconfig_state.cpp"
        ).read_text(encoding="utf-8")

        qspi_check = source.index("if (qspi_result != 0)")
        failure = source.index(
            "enterFailure(WebConfigRuntimeStatus::ErrorStorageInit);",
            qspi_check,
        )
        early_return = source.index("return false;", failure)
        ready = source.index(
            "runtimeStatus = WebConfigRuntimeStatus::Ready;",
            qspi_check,
        )

        self.assertLess(failure, early_return)
        self.assertLess(early_return, ready)
        self.assertIn(
            "runtimeStatus == WebConfigRuntimeStatus::ErrorStorageInit",
            source,
        )

    def test_webconfig_adc_preview_uses_report_rate_circular_dma(self) -> None:
        source = (
            ROOT
            / "application"
            / "Cpp_Core"
            / "Src"
            / "states"
            / "webconfig_state.cpp"
        ).read_text(encoding="utf-8")
        header = (
            ROOT
            / "application"
            / "Cpp_Core"
            / "Inc"
            / "states"
            / "webconfig_state.hpp"
        ).read_text(encoding="utf-8")

        arm = source.index("ADC_MANAGER.startADCSamping(false)")
        clock = source.index("REPORT_SCHEDULER.start(reportRateHz)")
        self.assertLess(arm, clock)
        self.assertIn("isDmaSamplingActive()", source)
        self.assertIn("REPORT_SCHEDULER.setRate(desiredRateHz)", source)
        self.assertIn("REPORT_SCHEDULER.consumeLatestTick()", source)
        self.assertNotIn("triggerSampling()", source)
        self.assertNotIn("kAdcPreviewIntervalMs", header)

    def test_webhid_output_uses_ep1_completion_backpressure_without_fixed_pacing(self) -> None:
        header = (
            ROOT
            / "application"
            / "Cpp_Core"
            / "Inc"
            / "webhid_service.hpp"
        ).read_text(encoding="utf-8")
        source = (
            ROOT
            / "application"
            / "Cpp_Core"
            / "Src"
            / "webhid_service.cpp"
        ).read_text(encoding="utf-8")
        board_link = (
            ROOT
            / "application"
            / "Cpp_Core"
            / "Src"
            / "usb_board_link.cpp"
        ).read_text(encoding="utf-8")
        ch585_link = (
            ROOT
            / "RF_PHY_Hop"
            / "TX"
            / "USB"
            / "usb_board_link.c"
        ).read_text(encoding="utf-8")

        self.assertNotIn("kFramePacingMs", header)
        self.assertNotIn("nextOutputAtMs", header)
        self.assertNotIn("outputPacingReady", source)
        self.assertNotIn("markOutputSent", source)
        self.assertIn("UsbBoardLink_WebConfigSendReport", source)
        send = source[
            source.index("bool WebHidService::sendFrame"):
            source.index("bool WebHidService::sendResponse")
        ]
        pump = source[
            source.index("void WebHidService::pumpOutput"):
        ]
        self.assertIn("if (!initialized)", pump)
        self.assertNotIn("HAL_Delay", send)
        self.assertNotIn("HAL_Delay", pump)
        self.assertLess(
            send.index("UsbBoardLink_WebConfigSendReport"),
            send.index("++nextTxSequence"),
        )
        self.assertIn("pullWebConfigCredit(", board_link)
        self.assertIn("webConfigTransmitMatches(", board_link)
        self.assertIn("usbState.device_mounted != 0u", board_link)
        self.assertIn("webConfigTxCreditConsumed = true", board_link)
        self.assertIn("usb_board_link_webconfig_report_consumed", ch585_link)
        self.assertIn("usb_net_bridge_return_credit", ch585_link)

    def test_webhid_drains_the_complete_ch585_usb_out_window(self) -> None:
        source = (
            ROOT
            / "application"
            / "Cpp_Core"
            / "Inc"
            / "webhid_service.hpp"
        ).read_text(encoding="utf-8")
        self.assertIn("kRxQueueDepth = 4u", source)
        self.assertIn("kRxProcessBudget = kRxQueueDepth", source)

    def test_webhid_logical_assembler_does_not_allocate_the_heap_limit(self) -> None:
        header = (
            ROOT
            / "application"
            / "Cpp_Core"
            / "Inc"
            / "webhid_service.hpp"
        ).read_text(encoding="utf-8")
        source = (
            ROOT
            / "application"
            / "Cpp_Core"
            / "Src"
            / "webhid_service.cpp"
        ).read_text(encoding="utf-8")

        assembler = header[
            header.index("struct LogicalAssembler"):
            header.index("struct StreamState")
        ]
        accept = source[
            source.index("bool WebHidService::acceptLogicalFragment"):
            source.index("bool WebHidService::processBootstrap")
        ]
        self.assertIn(
            "std::array<uint8_t, kMaximumLogicalBytes + 1u>",
            assembler,
        )
        self.assertNotIn("std::vector<uint8_t> bytes", assembler)
        self.assertNotIn(".reserve(kMaximumLogicalBytes)", accept)
        self.assertNotIn("std::vector<uint8_t> complete", accept)

    def test_webhid_stream_staging_is_fixed_and_bounded(self) -> None:
        header = (
            ROOT / "application" / "Cpp_Core" / "Inc" /
            "webhid_service.hpp"
        ).read_text(encoding="utf-8")
        source = (
            ROOT / "application" / "Cpp_Core" / "Src" /
            "webhid_service.cpp"
        ).read_text(encoding="utf-8")
        stream = header[
            header.index("struct StreamState"):
            header.index("struct OutboundLogical")
        ]
        self.assertIn(
            "std::array<uint8_t, kMaximumStreamBytes + 1u>",
            stream,
        )
        self.assertNotIn("std::vector<uint8_t> bytes", stream)
        self.assertNotIn("stream.bytes.reserve", source)
        self.assertNotIn("stream.bytes.insert", source)
        self.assertIn("Stream payload exceeds the 8 KiB limit", source)

    def test_webhid_binary_ack_is_real_typed_and_correlated(self) -> None:
        source = (
            ROOT / "application" / "Cpp_Core" / "Src" /
            "webhid_service.cpp"
        ).read_text(encoding="utf-8")
        validator = source[
            source.index("BinaryAckStatus describeBinaryAck"):
            source.index("cJSON *createBinaryAckData")
        ]
        stream = source[
            source.index("bool WebHidService::handleStreamRpc"):
            source.index("bool WebHidService::processStreamFragment")
        ]

        self.assertIn("response[0] != expectedOpcode", validator)
        self.assertIn("kFirmwareChunkAckOpcode = 0x81u", source)
        self.assertIn("loadLe32(&request[54])", validator)
        self.assertIn("loadLe32(&response[2])", validator)
        self.assertIn("acknowledgedChunkIndex != requestedChunkIndex", validator)
        self.assertIn("acknowledgedCid != requestedCid", validator)
        self.assertIn("received != offset + chunkLength", validator)
        self.assertIn("loadLe32(&response[16]) != loadLe32(&request[6])", validator)
        self.assertIn('"encoding", "base64"', source)
        self.assertIn('"data", encoded.c_str()', source)
        self.assertIn('"ack", ack', source)
        self.assertIn("createBinaryAckData(", stream)
        self.assertIn("Stream consumer returned an invalid or uncorrelated ACK", stream)
        self.assertNotIn("capturedBinary[1]", source)

    def test_webhid_image_reads_do_not_pause_the_qspi_mapping(self) -> None:
        image_source = (
            ROOT / "application" / "Cpp_Core" / "Src" / "configs" /
            "user_image_command_handler.cpp"
        ).read_text(encoding="utf-8")
        webhid_source = (
            ROOT / "application" / "Cpp_Core" / "Src" /
            "webhid_service.cpp"
        ).read_text(encoding="utf-8")

        read_helper = image_source[
            image_source.index("static bool qspi_read_bytes"):
            image_source.index("#pragma pack(push, 1)", image_source.index("static bool qspi_read_bytes"))
        ]
        read_only_opcodes = image_source[
            image_source.index("case BINARY_CMD_GET_BG_IMAGE_INFO:"):
            image_source.index("default:", image_source.index("case BINARY_CMD_GET_BG_IMAGE_INFO:"))
        ]

        # A read while WebConfig owns the mapped aperture must not abort/reset
        # QSPI: the LCD and other asset consumers may access it concurrently.
        self.assertIn("QSPI_W25Qxx_IsMemoryMappedMode()", read_helper)
        self.assertIn("SCB_InvalidateDCache_by_Addr", read_helper)
        self.assertIn("memcpy(destination", read_helper)
        self.assertIn("QSPI_W25Qxx_ReadBuffer", read_helper)
        self.assertNotIn("QSPI_W25Qxx_ExitMemoryMappedMode", read_helper)
        self.assertIn("qspi_read_bytes(", read_only_opcodes)
        self.assertNotIn("QSPIXipGuard", read_only_opcodes)
        self.assertNotIn("QSPI_W25Qxx_ExitMemoryMappedMode", read_only_opcodes)

        # Lock the typed 0x34/B4 response contract used by captureBinary.
        self.assertIn("kImageInfoResponseBytes = 64u", webhid_source)

    def test_webhid_stream_types_cannot_cross_binary_protocol_stacks(self) -> None:
        source = (
            ROOT / "application" / "Cpp_Core" / "Src" /
            "webhid_service.cpp"
        ).read_text(encoding="utf-8")
        stream = source[
            source.index("const bool firmwareStream ="):
            source.index("BinaryAckStatus ackStatus", source.index("const bool firmwareStream ="))
        ]
        self.assertIn("completedType == 1u", stream)
        self.assertIn("opcode == BINARY_CMD_UPLOAD_FIRMWARE_CHUNK", stream)
        self.assertIn("completedType == 2u && opcode == kImageChunkOpcode", stream)
        self.assertIn("(firmwareStream || imageStream)", stream)
        self.assertIn("binaryRequestShapeValid(completed, completedLength)", stream)

    def test_invalid_binary_exchange_never_leaks_capture_state(self) -> None:
        source = (
            ROOT / "application" / "Cpp_Core" / "Src" /
            "webhid_service.cpp"
        ).read_text(encoding="utf-8")
        exchange = source[
            source.index("bool WebHidService::handleBinaryExchange"):
            source.index("bool WebHidService::handleStreamRpc")
        ]
        auth = exchange.index("firmwareAuthorizationValid(")
        capture = exchange.index("captureBinary = true;")
        release = exchange.index("captureBinary = false;", capture)

        self.assertIn("clearBinaryCapture();", exchange[:capture])
        self.assertIn("binaryRequestShapeValid(binary.data(), binary.size())", exchange)
        self.assertLess(auth, capture)
        self.assertNotIn("return ", exchange[capture:release])
        self.assertIn("clearBinaryCapture();", exchange[release:])
        self.assertIn("captureBinaryInvalid", exchange)

    def test_session_end_and_queue_pressure_do_not_reset_the_usb_link(self) -> None:
        source = (
            ROOT / "application" / "Cpp_Core" / "Src" /
            "webhid_service.cpp"
        ).read_text(encoding="utf-8")
        pump = source[
            source.index("bool WebHidService::pumpLogicalOutput"):
            source.index("bool WebHidService::queueOneEvent")
        ]
        self.assertIn("if (endSession)", pump)
        self.assertIn("resetSession(true, false);", pump)

        enqueue = source[
            source.index("void WebHidService::enqueueJsonEvent"):
            source.index("void WebHidService::process()")
        ]
        self.assertNotIn("resetSession", enqueue)
        self.assertIn("droppedEventCount", enqueue)
        self.assertIn('"button.state"', enqueue)
        self.assertNotIn('"legacy.binary"', enqueue)

        telemetry = source[
            source.index("void WebHidService::updateTelemetry"):
            source.index("void WebHidService::pumpOutput")
        ]
        self.assertNotIn("resetSession", telemetry)
        self.assertIn("requestCheckpoint();", telemetry)

    def test_webhid_session_reset_stops_session_owned_adc_work(self) -> None:
        source = (
            ROOT
            / "application"
            / "Cpp_Core"
            / "Src"
            / "webhid_service.cpp"
        ).read_text(encoding="utf-8")
        reset = source[
            source.index("void WebHidService::resetSession"):
            source.index("bool WebHidService::firmwareAuthorizationValid")
        ]

        self.assertIn("ADC_CALIBRATION_MANAGER.stopCalibration()", reset)
        self.assertIn("buttons.enableTestMode(false)", reset)
        self.assertIn("buttons.stopButtonWorkers()", reset)
        self.assertIn("WEBCONFIG_LEDS_MANAGER.clearPreviewConfig()", reset)
        self.assertIn("UserImageCommandHandler::resetUploadSession()", reset)
        self.assertIn("WebHID session runtime stopped", reset)

    def test_webhid_control_plane_runs_before_adc_optional_work(self) -> None:
        source = (
            ROOT
            / "application"
            / "Cpp_Core"
            / "Src"
            / "states"
            / "webconfig_state.cpp"
        ).read_text(encoding="utf-8")
        tick = source[
            source.index("void WebConfigState::tick()"):
            source.index("void WebConfigState::exit()")
        ]

        webhid = tick.index("WEBHID_SERVICE.process();")
        calibration = tick.index("ADC_CALIBRATION_MANAGER.processCalibration()")
        adc_health = tick.index("if (!ADC_MANAGER.isDmaSamplingActive())")
        buttons = tick.index("WEBCONFIG_BTNS_MANAGER.update()")
        self.assertLess(webhid, calibration)
        self.assertLess(webhid, adc_health)
        self.assertLess(webhid, buttons)

    def test_calibration_view_never_sends_commands_from_mount_cleanup(self) -> None:
        source = (
            ROOT
            / "application"
            / "www"
            / "components"
            / "hitbox"
            / "hitbox-calibration.tsx"
        ).read_text(encoding="utf-8")

        self.assertNotIn("startManualCalibration", source)
        self.assertNotIn("stopManualCalibration", source)
        self.assertIn("eventBus.on(EVENTS.CALIBRATION_UPDATE", source)
        self.assertIn("unsubscribe();", source)

    def test_webconfig_disconnect_invalidates_calibration_session_locally(self) -> None:
        source = (
            ROOT
            / "application"
            / "www"
            / "components"
            / "global-setting-content.tsx"
        ).read_text(encoding="utf-8")

        self.assertIn("connectionEpochRef.current += 1", source)
        self.assertIn("calibrationCheckInFlightRef.current = false", source)
        self.assertIn("cancelConfirm();", source)
        self.assertIn("setCalibrationActiveForSession(false);", source)
        self.assertIn("setIsInit(false);", source)
        disconnect_effect = source[
            source.index("deviceConnectedRef.current = deviceConnected"):
            source.index("// \u521d\u59cb\u5316 currentHotkeys")
        ]
        self.assertNotIn("stopManualCalibration", disconnect_effect)

    def test_calibration_commands_require_current_connected_session(self) -> None:
        source = (
            ROOT
            / "application"
            / "www"
            / "components"
            / "global-setting-content.tsx"
        ).read_text(encoding="utf-8")

        self.assertIn("if (!deviceConnectedRef.current", source)
        self.assertIn("const epoch = connectionEpochRef.current", source)
        self.assertIn("connectionEpochRef.current === epoch", source)
        self.assertIn("await startManualCalibration();", source)
        self.assertIn("await stopManualCalibration();", source)

    def test_lcd_ui_is_independent_from_ch585_safe_state(self) -> None:
        power_source = (
            ROOT
            / "application"
            / "Cpp_Core"
            / "Src"
            / "board_power.cpp"
        ).read_text(encoding="utf-8")
        input_source = (
            ROOT
            / "application"
            / "Cpp_Core"
            / "Src"
            / "states"
            / "input_state.cpp"
        ).read_text(encoding="utf-8")

        setup = power_source[
            power_source.index("void BoardPower::setup()"):
            power_source.index("void BoardPower::assertMainPowerHold()")
        ]
        safe = power_source[
            power_source.index("void BoardPower::enterSafeState()"):
            power_source.index("void BoardPower::enterRecoveryUiState()")
        ]
        standby = power_source[
            power_source.index("void BoardPower::prepareForStandby()"):
            power_source.index("void BoardPower::releaseSafeState()")
        ]
        input_safe = input_source[
            input_source.index("static void enterBoardSafeState()"):
            input_source.index("static void teardownCh585Runtime()")
        ]

        self.assertIn("writePin(LCD_EN_PORT, LCD_EN_PIN, true);", setup)
        self.assertIn("lcdEnabled = true;", setup)
        self.assertIn("recoveryUiAllowed = true;", safe)
        self.assertIn("setLcdEnabled(true);", safe)
        self.assertNotIn("setLcdEnabled(false);", safe)
        self.assertIn("setLcdEnabled(false);", standby)
        self.assertNotIn("SPIScreenManager::getInstance().shutdown();", input_safe)
        self.assertIn("BOARD_POWER.enterSafeState();", input_safe)

    def test_webconfig_bringup_clears_all_retained_standby_selection(self) -> None:
        sleep_source = (
            ROOT
            / "application"
            / "Cpp_Core"
            / "Src"
            / "system_sleep_manager.cpp"
        ).read_text(encoding="utf-8")
        screen_source = (
            ROOT
            / "application"
            / "Cpp_Core"
            / "Src"
            / "screen_control"
            / "spi_screen_manager.cpp"
        ).read_text(encoding="utf-8")

        self.assertIn("HAL_PWREx_DisableWakeUpPin(PWR_WAKEUP_PIN1);", sleep_source)
        self.assertIn("PWR_CPUCR_PDDS_D1 | PWR_CPUCR_PDDS_D2 | PWR_CPUCR_PDDS_D3", sleep_source)
        self.assertIn("SET_BIT(PWR->CPUCR, PWR_CPUCR_RUN_D3);", sleep_source)
        self.assertIn("SCB_SCR_SLEEPDEEP_Msk | SCB_SCR_SLEEPONEXIT_Msk", sleep_source)
        self.assertIn("return (g_cfgBrightness < 20u) ? 20u : g_cfgBrightness;", screen_source)

    def test_screen_webconfig_save_quiesces_and_restores_input_pipeline(self) -> None:
        screen_source = (
            ROOT
            / "application"
            / "Cpp_Core"
            / "Src"
            / "screen_control"
            / "spi_screen_manager.cpp"
        ).read_text(encoding="utf-8")
        branch = screen_source[
            screen_source.index("if (id == 9u)"):
            screen_source.index("} else if (id == 10u)")
        ]

        suspend = branch.index("INPUT_STATE.suspendInputPipelineForStorage()")
        save = branch.index("STORAGE_MANAGER.saveConfig()")
        resume = branch.index("INPUT_STATE.resumeInputPipelineAfterStorage(")
        reset = branch.index("MainRuntime_RequestReset()")

        self.assertLess(suspend, save)
        self.assertLess(save, resume)
        self.assertLess(resume, reset)

    def test_qspi_ready_wait_uses_bounded_indirect_status_reads(self) -> None:
        source = (
            ROOT
            / "application"
            / "Drivers"
            / "QSPI-W25Q64"
            / "qspi-w25q64.c"
        ).read_text(encoding="utf-8")
        helper = source[
            source.index("static int8_t QSPI_W25Qxx_ReadStatusReg1"):
            source.index("void HAL_QSPI_MspInit")
        ]
        wait = source[
            source.index("int8_t QSPI_W25Qxx_AutoPollingMemReady"):
            source.index("int8_t QSPI_W25Qxx_Reset")
        ]

        self.assertIn("HAL_QSPI_Command", helper)
        self.assertIn("HAL_QSPI_Receive", helper)
        self.assertIn("W25Qxx_READY_POLL_MAX_ATTEMPTS", wait)
        self.assertIn("HAL_GetTick() - tick_start", wait)
        self.assertNotIn("HAL_QSPI_AutoPolling(", wait)


if __name__ == "__main__":
    unittest.main()
