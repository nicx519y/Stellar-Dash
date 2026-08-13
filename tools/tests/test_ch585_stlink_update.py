import hashlib
import sys
import unittest
import zlib
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import ch585_stlink_update as update


class Ch585StlinkUpdateTests(unittest.TestCase):
    def test_waiting_probe_has_a_hard_timeout_per_attempt(self) -> None:
        source = (ROOT / "tools" / "ch585_stlink_update.py").read_text(
            encoding="utf-8"
        )
        self.assertIn("timeout=3.0 if wait_seconds > 0 else None", source)
        self.assertIn("except subprocess.TimeoutExpired", source)

    def test_stm32_staging_accepts_sha256_success_return_value(self) -> None:
        source = (
            ROOT / "application" / "Cpp_Core" / "Src" / "ch585_firmware_update.cpp"
        ).read_text(encoding="utf-8")
        self.assertGreaterEqual(source.count("sha256_calculate_raw("), 3)
        self.assertEqual(
            source.count("sha256_calculate_raw("), source.count("actual) == 0")
        )
        self.assertNotIn("actual) != 0", source)

    def test_header_sector_and_payload_are_64k_separated(self) -> None:
        self.assertEqual(update.STAGING_HEADER_BYTES, 0x10000)
        self.assertEqual(update.STAGING_DATA_ADDRESS, 0x90790000)
        self.assertEqual(update.STAGING_DATA_BYTES, 0x70000)
        self.assertEqual(update.STAGING_DATA_ADDRESS % 0x10000, 0)

    def test_ready_record_round_trips_and_commit_is_valid(self) -> None:
        firmware = bytes((index & 0xFF) for index in range(0x1004))
        record = update.build_ready_record(firmware, 123)
        sector = record + bytes([0xFF]) * (
            update.STAGING_HEADER_BYTES - len(record)
        )
        parsed = update.parse_records(sector)
        self.assertEqual(len(parsed), 1)
        self.assertEqual(parsed[0].state, update.STATE_READY)
        self.assertEqual(parsed[0].generation, 123)
        self.assertEqual(parsed[0].image_size, len(firmware))
        self.assertEqual(parsed[0].sha256, hashlib.sha256(firmware).digest())
        self.assertEqual(
            parsed[0].image_crc32,
            zlib.crc32(firmware[update.CH585_IAP_BYTES :]) & 0xFFFFFFFF,
        )

    def test_partial_record_is_not_actionable(self) -> None:
        firmware = bytes(0x1004)
        record = bytearray(update.build_ready_record(firmware, 1))
        record[-4:] = b"\xFF" * 4
        sector = bytes(record) + bytes([0xFF]) * (
            update.STAGING_HEADER_BYTES - len(record)
        )
        self.assertEqual(update.parse_records(sector), [])

    def test_rejects_unaligned_or_iap_only_images(self) -> None:
        with self.assertRaises(update.Ch585StlinkUpdateError):
            update.validate_firmware(bytes(update.CH585_IAP_BYTES))
        with self.assertRaises(update.Ch585StlinkUpdateError):
            update.validate_firmware(bytes(update.CH585_IAP_BYTES + 1))

    def test_daily_command_has_no_bridge_or_lock_operations(self) -> None:
        source = (ROOT / "tools" / "ch585_stlink_update.py").read_text(
            encoding="utf-8"
        )
        self.assertNotIn("install-stm32-bridge", source)
        self.assertNotIn("SLOT_A_", source)
        self.assertNotIn("SLOT_B_", source)
        self.assertNotIn("METADATA_ADDRESS", source)
        forbidden = ("option_write", "readout_protect", "pcrop", "scar", "wrp")
        lowered = source.lower()
        for token in forbidden:
            self.assertNotIn(token, lowered)

    def test_daily_command_keeps_default_speed_and_allows_explicit_recovery_speed(self) -> None:
        source = (ROOT / "tools" / "ch585_stlink_update.py").read_text(
            encoding="utf-8"
        )
        self.assertIn('"--swd-khz"', source)
        self.assertIn("default=DEFAULT_SWD_KHZ", source)
        self.assertIn("adapter_speed_khz=args.swd_khz", source)
        self.assertIn("read_latest_status(openocd, uid, args.swd_khz)", source)
        self.assertIn('"--probe-wait-seconds"', source)
        self.assertIn("resume_after=False", source)

    def test_real_qspi_openocd_chain_has_no_protection_or_internal_flash_write(self) -> None:
        qspi_cfg = (
            ROOT / "application" / "Openocd_Script" /
            "ST-LINK-QSPIFLASH.cfg"
        ).read_text(encoding="utf-8").lower()
        for command in (
            "option_write",
            "readout_protect",
            "stm32h7x mass_erase",
            "flash protect",
            "flash erase_sector 0",
            "flash write_bank 0",
            "program ",
        ):
            self.assertNotIn(command, qspi_cfg)

        build = (ROOT / "tools" / "build.py").read_text(encoding="utf-8")
        start = build.index("    def _flash_qspi_file_in_chunks(")
        end = build.index("    @staticmethod\n    def _openocd_tcl_braced_path", start)
        qspi_writer = build[start:end].lower()
        self.assertIn("flash write_image erase", qspi_writer)
        self.assertIn("flash verify_bank 1", qspi_writer)
        self.assertIn("qspi_base = 0x90000000", qspi_writer)
        for command in (
            "option_write",
            "readout_protect",
            "mass_erase",
            "flash protect",
            "flash erase_sector 0",
            "flash write_bank 0",
        ):
            self.assertNotIn(command, qspi_writer)

    def test_payload_is_written_before_ready_commit(self) -> None:
        source = (ROOT / "tools" / "ch585_stlink_update.py").read_text(
            encoding="utf-8"
        )
        payload_call = source.index("payload_path,\n            STAGING_DATA_ADDRESS")
        ready_call = source.index("ready_path,\n            STAGING_ADDRESS")
        self.assertLess(payload_call, ready_call)
        self.assertIn("leave_halted=True", source[payload_call:ready_call])

    def test_status_reads_physical_qspi_bank_not_cached_memory_map(self) -> None:
        source = (ROOT / "tools" / "ch585_stlink_update.py").read_text(
            encoding="utf-8"
        )
        start = source.index("def _openocd_status_dump(")
        end = source.index("\ndef _probe_uid(", start)
        body = source[start:end]
        self.assertIn("flash read_bank 1", body)
        self.assertNotIn("dump_image", body)

    def test_iap_retry_contract_is_explicit(self) -> None:
        source = (
            ROOT / "application" / "Cpp_Core" / "Src" / "ch585_iap_client.cpp"
        ).read_text(encoding="utf-8")
        self.assertIn("previousTimedOut", source)
        self.assertIn("CH585_IAP_STATUS_BAD_ADDRESS", source)
        self.assertIn("RawDiscardPendingResponse", source)
        self.assertIn("deferring result to app CAPS", source)
        self.assertIn("USB_BOARD_CAP_ROLE_MAINTENANCE", source)

    def test_qspi_mapping_state_is_set_only_after_hal_success(self) -> None:
        source = (
            ROOT
            / "application"
            / "Drivers"
            / "QSPI-W25Q64"
            / "qspi-w25q64.c"
        ).read_text(encoding="utf-8")
        call = source.index("HAL_QSPI_MemoryMapped(&hqspi")
        assignment = source.index("xip_enabled = (status == HAL_OK)", call)
        self.assertGreater(assignment, call)

    def test_updater_runs_before_logger_storage_and_screen(self) -> None:
        state_machine = (
            ROOT / "application" / "Cpp_Core" / "Src" / "main_state_machine.cpp"
        ).read_text(encoding="utf-8")
        ready = state_machine.index("CH585_FIRMWARE_UPDATE.hasReadyStagedImage()")
        bridge = state_machine.index("enterState(MainRuntimeState::Ch585BridgeUpdate)", ready)
        interactive = state_machine.index("initializeInteractiveRuntime();", bridge)
        self.assertLess(ready, bridge)
        self.assertLess(bridge, interactive)
        bridge_state = (
            ROOT / "application" / "Cpp_Core" / "Src" / "states" /
            "ch585_bridge_update_state.cpp"
        ).read_text(encoding="utf-8")
        self.assertIn("performPendingUpdate", bridge_state)
        main = (ROOT / "application" / "Core" / "Src" / "main.c").read_text(
            encoding="utf-8"
        )
        self.assertNotIn("Ch585FirmwareUpdate_EarlyBoot", main)

    def test_firmware_qspi_writes_use_24_bit_flash_offsets(self) -> None:
        source = (
            ROOT / "application" / "Cpp_Core" / "Src" / "ch585_firmware_update.cpp"
        ).read_text(encoding="utf-8")
        self.assertIn("return mappedAddress & 0x00FFFFFFu", source)
        self.assertIn("const uint32_t target = flashOffset(mappedTarget)", source)
        self.assertIn("address = flashOffset(address)", source)
        self.assertIn("QSPI_W25Qxx_SectorErase(flashOffset", source)

    def test_unclaimed_ready_does_not_reset_loop(self) -> None:
        source = (
            ROOT / "application" / "Cpp_Core" / "Src" / "states" /
            "ch585_bridge_update_state.cpp"
        ).read_text(encoding="utf-8")
        guard = source.index("if (!CH585_FIRMWARE_UPDATE.wasClaimed())")
        request = source.index("MAIN_STATE_MACHINE.requestReset()", guard)
        self.assertIn("return false;", source[guard:request])

    def test_iap_has_pre_destructive_serial_milestones(self) -> None:
        source = (
            ROOT / "application" / "Cpp_Core" / "Src" / "ch585_iap_client.cpp"
        ).read_text(encoding="utf-8")
        for stage in ('"M00"', '"M00C"', '"M00P"', '"M00L"'):
            self.assertIn(stage, source)

    def test_role_bootstrap_waits_for_clean_iap_handoff_before_select_role(self) -> None:
        source = (
            ROOT / "application" / "Cpp_Core" / "Src" /
            "ch585_role_bootstrap.cpp"
        ).read_text(encoding="utf-8")
        wait_ready = source.index("RFBootReady::waitForModuleReady")
        select = source.index("selectOnce(requestedRole)", wait_ready)
        self.assertLess(wait_ready, select)
        self.assertNotIn("USBBoardLinkPort_RequestApplicationBoot()", source)
        self.assertNotIn("probeRunningLoader", source)

        port = (
            ROOT / "application" / "Cpp_Core" / "Src" /
            "usb_board_link_port.cpp"
        ).read_text(encoding="utf-8")
        self.assertNotIn("USBBoardLinkPort_RequestApplicationBoot", port)
        self.assertNotIn("probeRunningLoader", (
            ROOT / "application" / "Cpp_Core" / "Src" /
            "ch585_iap_client.cpp"
        ).read_text(encoding="utf-8"))

        board_cfg = (
            ROOT / "application" / "Core" / "Inc" / "board_cfg.h"
        ).read_text(encoding="utf-8")
        self.assertIn("CH585_ROLE_SELECT_TIMEOUT_MS          1200u", board_cfg)

    def test_role_ack_keeps_proven_spi_rate_through_caps(self) -> None:
        port = (
            ROOT / "application" / "Cpp_Core" / "Src" /
            "usb_board_link_port.cpp"
        ).read_text(encoding="utf-8")
        self.assertIn("bool USBBoardLinkPort_InitApplication()", port)
        app_start = port.index("bool USBBoardLinkPort_InitApplication()")
        self.assertIn(
            "SPI_BAUDRATEPRESCALER_256",
            port[app_start:app_start + 1800],
        )
        self.assertNotIn(
            "SPI_BAUDRATEPRESCALER_16",
            port[app_start:app_start + 1800],
        )
        link = (
            ROOT / "application" / "Cpp_Core" / "Src" /
            "usb_board_link.cpp"
        ).read_text(encoding="utf-8")
        role_ack = link.index("selectedRole = role;")
        steady = link.index("USBBoardLinkPort_InitApplication()", role_ack)
        self.assertLess(role_ack, steady)

    def test_caps_liveness_is_not_invalidated_by_transient_credit_send(self) -> None:
        source = (
            ROOT / "application" / "Cpp_Core" / "Src" /
            "usb_board_link.cpp"
        ).read_text(encoding="utf-8")
        caps_start = source.index("bool UsbBoardLink::getCapabilities()")
        credits_start = source.index(
            "bool UsbBoardLink::grantInitialReceiveCredits()", caps_start
        )
        body = source[caps_start:credits_start]
        self.assertIn("(void)grantInitialReceiveCredits();", body)
        self.assertNotIn(
            "if (capsValid && !grantInitialReceiveCredits())", body
        )

    def test_four_byte_get_caps_advances_dma_without_filling_it(self) -> None:
        source = (
            ROOT / "application" / "Cpp_Core" / "Src" /
            "usb_board_link_port.cpp"
        ).read_text(encoding="utf-8")
        send_start = source.index("bool USBBoardLinkPort_Send(")
        transact_start = source.index(
            "bool USBBoardLinkPort_Transact(", send_start
        )
        send = source[send_start:transact_start]
        self.assertIn("USB_BOARD_CMD_GET_CAPS", send)
        self.assertIn("paddedCapsFrame[USB_BOARD_LINK_MAX_FRAME_BYTES]", send)
        self.assertIn("wireLength = sizeof(paddedCapsFrame)", send)
        ch585 = (
            ROOT / "RF_PHY_Hop" / "TX" / "USB" /
            "usb_board_link_port_ch585.h"
        ).read_text(encoding="utf-8")
        self.assertIn("SPI0_IT_FIFO_HF", (
            ROOT / "RF_PHY_Hop" / "TX" / "USB" /
            "usb_board_link_port_ch585.c"
        ).read_text(encoding="utf-8"))

    def test_select_role_stays_exactly_five_bytes_on_wire(self) -> None:
        source = (
            ROOT / "application" / "Cpp_Core" / "Src" /
            "usb_board_link_port.cpp"
        ).read_text(encoding="utf-8")
        send_start = source.index("bool USBBoardLinkPort_Send(")
        transact_start = source.index(
            "bool USBBoardLinkPort_Transact(", send_start
        )
        send = source[send_start:transact_start]
        padding = send[
            send.index("uint8_t paddedCapsFrame"):
            send.index("const HAL_StatusTypeDef result")
        ]
        self.assertIn("USB_BOARD_CMD_GET_CAPS", padding)
        self.assertNotIn("USB_BOARD_CMD_SELECT_ROLE", padding)

    def test_ch585_replies_idempotently_to_same_role_retry(self) -> None:
        source = (
            ROOT / "RF_PHY_Hop" / "TX" / "USB" /
            "usb_board_link.c"
        ).read_text(encoding="utf-8")
        select = source.index("case USB_BOARD_CMD_SELECT_ROLE:")
        default = source.index("default:", select)
        body = source[select:default]
        self.assertIn("frame->payload[0] == (uint8_t)s_role", body)
        self.assertIn("USB_BOARD_EVT_ROLE_SELECTED", body)
        self.assertIn("USB_BOARD_STATUS_ROLE_LOCKED", body)

    def test_ch585_host_failure_cannot_kill_board_link_caps(self) -> None:
        source = (
            ROOT / "RF_PHY_Hop" / "TX" / "USB" /
            "usb_subsystem.c"
        ).read_text(encoding="utf-8")
        link_init = source.index("usb_board_link_init(role);")
        loop = source.index("for(;;)", link_init)
        startup = source[link_init:loop]
        runtime = source[loop:]
        self.assertNotIn("(void)usb_host_init();", startup)
        self.assertIn("usb_board_link_process();", runtime)
        self.assertIn("usb_board_link_caps_requested()", runtime)
        self.assertIn("(void)usb_host_init();", runtime)
        self.assertLess(
            runtime.index("usb_board_link_process();"),
            runtime.index("(void)usb_host_init();"),
        )

    def test_ch585_does_not_publish_async_events_before_get_caps(self) -> None:
        source = (
            ROOT / "RF_PHY_Hop" / "TX" / "USB" /
            "usb_board_link.c"
        ).read_text(encoding="utf-8")
        self.assertIn("static uint8_t s_caps_requested;", source)
        get_caps = source.index("case USB_BOARD_CMD_GET_CAPS:")
        set_gate = source.index("s_caps_requested = 1u;", get_caps)
        process = source.index("void usb_board_link_process(void)", set_gate)
        gate = source.index("if(s_caps_requested != 0u)", process)
        for event_source in (
            "poll_state_change();",
            "queue_one_credit();",
            "pump_outbound();",
        ):
            self.assertGreater(source.index(event_source, gate), gate)

    def test_role_ack_allows_application_dma_to_settle_before_caps(self) -> None:
        ch585_port = (
            ROOT / "RF_PHY_Hop" / "TX" / "USB" /
            "usb_board_link_port_ch585.c"
        ).read_text(encoding="utf-8")
        init = ch585_port.index("bool usb_board_link_port_init(void)")
        rx = ch585_port.index("rx_fifo_start();", init)
        unlock = ch585_port.index("port_unlock();", rx)
        self.assertLess(rx, unlock)

        subsystem = (
            ROOT / "RF_PHY_Hop" / "TX" / "USB" /
            "usb_subsystem.c"
        ).read_text(encoding="utf-8")
        pulse = subsystem.index("rfm_board_latest_ch585_pulse_boot_ready();")
        loop = subsystem.index("for(;;)", pulse)
        self.assertLess(pulse, loop)

        stm32_link = (
            ROOT / "application" / "Cpp_Core" / "Src" /
            "usb_board_link.cpp"
        ).read_text(encoding="utf-8")
        role_valid = stm32_link.index(
            "if (!validExplicitSelection && !validUsbSubsystemSelection)"
        )
        settle = stm32_link.index("HAL_Delay(150u);", role_valid)
        selected = stm32_link.index("selectedRole = role;", settle)
        self.assertLess(settle, selected)
        self.assertNotIn("RFBootReady::waitForModuleReady", stm32_link[role_valid:selected])

    def test_approved_recovery_measurement_matches_default_makefile_bin(self) -> None:
        firmware = update.DEFAULT_FIRMWARE.read_bytes()
        self.assertEqual(len(firmware), update.EXPECTED_RECOVERY_SIZE)
        self.assertEqual(
            hashlib.sha256(firmware).hexdigest(),
            update.EXPECTED_RECOVERY_SHA256,
        )

    def test_manual_isp_fails_safe_and_suppresses_all_takeover_and_standby(self) -> None:
        mode = (
            ROOT / "application" / "Cpp_Core" / "Src" /
            "ch585_update_mode.cpp"
        ).read_text(encoding="utf-8")
        active_start = mode.index("bool Ch585UpdateMode::isManualIspActive() const")
        active_end = mode.index("bool Ch585UpdateMode::isManualIspPowered() const")
        active = mode[active_start:active_end]
        self.assertIn("!isIapConfirmed()", active)
        self.assertIn("CH585_FIRMWARE_UPDATE.hasFailed()", active)

        setup_start = mode.index("void Ch585UpdateMode::setupManualIspRuntime()")
        setup_end = mode.index("bool Ch585UpdateMode::powerOnManualIsp()", setup_start)
        setup = mode[setup_start:setup_end]
        for shutdown in (
            "USB_DRIVER.shutdown()",
            "USB_BOARD_LINK.shutdown()",
            "CH585_ROLE_BOOTSTRAP.shutdown()",
            "RFBridgePort_Shutdown()",
        ):
            self.assertIn(shutdown, setup)
        self.assertIn("BOARD_POWER.setCh585Enabled(true)", setup)
        self.assertNotIn("setUsbHostEnabled(true)", setup)
        self.assertNotIn("selectRole", setup)

        main = (
            ROOT / "application" / "Cpp_Core" / "Src" /
            "main_state_machine.cpp"
        ).read_text(encoding="utf-8")
        manual = main.index("if (CH585_UPDATE_MODE.isManualIspActive())")
        self.assertIn("MainRuntimeState::Ch585UsbIsp", main[manual:manual + 160])
        usb_state = (
            ROOT / "application" / "Cpp_Core" / "Src" / "states" /
            "ch585_usb_isp_state.cpp"
        ).read_text(encoding="utf-8")
        self.assertIn("setupManualIspRuntime", usb_state)

        sleep = (
            ROOT / "application" / "Cpp_Core" / "Src" /
            "system_sleep_manager.cpp"
        ).read_text(encoding="utf-8")
        self.assertNotIn("HAL_PWR_EnterSTANDBYMode", sleep)
        self.assertIn("deep Standby request ignored", sleep)

    def test_manual_recovery_requires_iap_and_application_caps_before_clearing_failure(self) -> None:
        mode = (
            ROOT / "application" / "Cpp_Core" / "Src" /
            "ch585_update_mode.cpp"
        ).read_text(encoding="utf-8")
        start = mode.index("bool Ch585UpdateMode::requestExitManualIsp()")
        end = mode.index("bool Ch585UpdateMode::setIapConfirmed", start)
        verify = mode[start:end]
        probe = verify.index("CH585_IAP_CLIENT.probe()")
        app = verify.index("CH585_IAP_CLIENT.validateApplication()")
        journal = verify.index("acknowledgeManualRecovery()")
        persist = verify.index("STORAGE_MANAGER.saveConfig()")
        self.assertLess(probe, app)
        self.assertLess(app, journal)
        self.assertLess(journal, persist)

        updater = (
            ROOT / "application" / "Cpp_Core" / "Src" /
            "ch585_firmware_update.cpp"
        ).read_text(encoding="utf-8")
        start = updater.index("bool Ch585FirmwareUpdate::acknowledgeManualRecovery()")
        body = updater[start:]
        self.assertIn("eraseHeaderJournal()", body)
        self.assertNotIn("eraseStaging()", body)

    def test_six_peer_states_and_ready_priority_are_explicit(self) -> None:
        header = (
            ROOT / "application" / "Cpp_Core" / "Inc" /
            "main_state_machine.hpp"
        ).read_text(encoding="utf-8")
        for state in (
            "Input", "WebConfig", "Calibration", "Ch585UsbIsp",
            "Ch585BridgeUpdate", "SafeRecovery",
        ):
            self.assertIn(state, header)
        source = (
            ROOT / "application" / "Cpp_Core" / "Src" /
            "main_state_machine.cpp"
        ).read_text(encoding="utf-8")
        resolver = source.index("MainRuntimeState MainStateMachine::resolveNormalStartupState")
        ready = source.index("hasReadyStagedImage()", resolver)
        manual = source.index("isManualIspActive()", ready)
        self.assertLess(ready, manual)
        self.assertIn("exactly six states", source)

        mode = (
            ROOT / "application" / "Cpp_Core" / "Src" /
            "ch585_update_mode.cpp"
        ).read_text(encoding="utf-8")
        confirmed = mode.index("bool Ch585UpdateMode::isIapConfirmed() const")
        visible = mode.index("bool Ch585UpdateMode::isManualEntryVisible() const")
        self.assertIn("hasAppliedImage()", mode[confirmed:visible])

    def test_daily_staging_uses_runtime_attach_without_reset_or_nrst(self) -> None:
        source = (ROOT / "tools" / "ch585_stlink_update.py").read_text(
            encoding="utf-8"
        )
        self.assertIn("runtime_attach=True", source)
        self.assertNotIn("connect_assert_srst", source)
        self.assertNotIn('"reset init"', source)
        self.assertNotIn('"reset run"', source)

    def test_runtime_resets_are_coordinated_by_main_state_machine(self) -> None:
        roots = (
            ROOT / "application" / "Cpp_Core" / "Src",
        )
        direct = []
        for root in roots:
            for path in root.rglob("*.cpp"):
                if path.name == "main_state_machine.cpp":
                    continue
                text = path.read_text(encoding="utf-8")
                if "NVIC_SystemReset();" in text and "// NVIC_SystemReset();" not in text:
                    direct.append(path.relative_to(ROOT).as_posix())
        self.assertEqual(direct, [])

    def test_deep_standby_is_globally_unreachable(self) -> None:
        source = (
            ROOT / "application" / "Cpp_Core" / "Src" /
            "system_sleep_manager.cpp"
        ).read_text(encoding="utf-8")
        self.assertNotIn("HAL_PWR_EnterSTANDBYMode", source)
        self.assertNotIn("prepareForStandby", source)
        self.assertIn("forceRunPowerPolicy", source)

    def test_application_caps_verification_retries_across_role_handoff(self) -> None:
        client = (
            ROOT / "application" / "Cpp_Core" / "Src" /
            "ch585_iap_client.cpp"
        ).read_text(encoding="utf-8")
        self.assertIn("kApplicationCapsWindowMs = 1000u", client)
        self.assertIn("kApplicationCapsRetryMs = 10u", client)
        verify = client.index("bool Ch585IapClient::validateApplication()")
        caps = client.index("USB_BOARD_LINK.getCapabilities()", verify)
        retry_delay = client.index("HAL_Delay(kApplicationCapsRetryMs)", caps)
        deadline = client.index("kApplicationCapsWindowMs", retry_delay)
        self.assertLess(caps, retry_delay)
        self.assertLess(retry_delay, deadline)

    def test_tx_uses_official_iap_application_startup(self) -> None:
        makefile = (ROOT / "RF_PHY_Hop" / "TX" / "Makefile").read_text(
            encoding="utf-8"
        )
        self.assertIn("IAP/APP/Startup", makefile)
        self.assertIn("IAP/USB_IAP/Startup", makefile)
        self.assertNotIn("STARTUP_DIR  := $(SRC_ROOT_DIR)/Startup", makefile)

    def test_ch585_rx_uses_fifo_interrupt_without_nss_dependency(self) -> None:
        port = (
            ROOT / "RF_PHY_Hop" / "TX" / "USB" /
            "usb_board_link_port_ch585.c"
        ).read_text(encoding="utf-8")
        self.assertIn("static void rx_fifo_start(void)", port)
        self.assertIn("SPI0_IT_FIFO_HF | SPI0_IT_FIFO_OV", port)
        self.assertIn("rx_drain_fifo_locked();", port)
        self.assertNotIn("rx_dma_position", port)

    def test_role_selector_has_explicit_application_ready_handshake(self) -> None:
        ch585 = (
            ROOT / "RF_PHY_Hop" / "TX" / "BOARD" /
            "board_role_selector.c"
        ).read_text(encoding="utf-8")
        self.assertIn("rfm_board_latest_ch585_pulse_boot_ready();", ch585)
        stm32 = (
            ROOT / "application" / "Cpp_Core" / "Src" /
            "ch585_role_bootstrap.cpp"
        ).read_text(encoding="utf-8")
        self.assertIn("RFBootReady::waitForModuleReady", stm32)

    def test_ch585_app_cannot_self_commit_iap_metadata(self) -> None:
        entry = (
            ROOT / "RF_PHY_Hop" / "TX" / "BOARD" / "board_entry.c"
        ).read_text(encoding="utf-8")
        self.assertNotIn("ch585_iap_mark_running_app", entry)
        self.assertNotIn("SetSysClock(SYSCLK_FREQ)", entry)
        self.assertNotIn("HSECFG_Capacitance", entry)

    def test_iap_handoff_matches_official_direct_jump(self) -> None:
        iap = (
            ROOT / "RF_PHY_Hop" / "TX" / "IAP" / "iap_main.c"
        ).read_text(encoding="utf-8")
        start = iap.index("static void jump_to_app(void)")
        end = iap.index("int main(void)", start)
        handoff = iap[start:end]
        self.assertIn("CH585_IAP_APP_START", handoff)
        self.assertNotIn("SYS_DisableAllIrq", handoff)

    def test_qspi_journal_write_invalidates_mapped_dcache_before_verify(self) -> None:
        source = (
            ROOT / "application" / "Cpp_Core" / "Src" / "ch585_firmware_update.cpp"
        ).read_text(encoding="utf-8")
        remap = source.index(
            "QSPI_W25Qxx_EnterMemoryMappedMode() != QSPI_W25Qxx_OK",
            source.index("static bool appendRecord"),
        )
        invalidate = source.index("invalidateMappedRange(mappedTarget", remap)
        verify = source.index("recordValid(*written)", invalidate)
        self.assertLess(remap, invalidate)
        self.assertLess(invalidate, verify)


if __name__ == "__main__":
    unittest.main()
