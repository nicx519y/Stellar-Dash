import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class LedConfigSafetyTests(unittest.TestCase):
    def test_all_persistable_u8_values_are_runtime_safe(self) -> None:
        compiler = shutil.which("g++")
        self.assertIsNotNone(compiler, "host g++ is required for LED safety tests")

        with tempfile.TemporaryDirectory(prefix="hbox-led-safety-") as temp:
            executable = Path(temp) / "led_config_safety_test.exe"
            compile_result = subprocess.run(
                [
                    compiler,
                    "-std=c++17",
                    "-Wall",
                    "-Wextra",
                    "-I",
                    str(ROOT / "application" / "Cpp_Core" / "Inc"),
                    str(ROOT / "tools" / "tests" / "led_config_safety_test.cpp"),
                    "-o",
                    str(executable),
                ],
                cwd=ROOT,
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(
                compile_result.returncode,
                0,
                compile_result.stdout + compile_result.stderr,
            )
            run_result = subprocess.run(
                [str(executable)],
                cwd=ROOT,
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(
                run_result.returncode,
                0,
                run_result.stdout + run_result.stderr,
            )
            self.assertIn("LED config safety tests passed", run_result.stdout)

    def test_runtime_and_storage_paths_use_the_shared_guard(self) -> None:
        manager = (
            ROOT / "application" / "Cpp_Core" / "Src" / "leds" / "leds_manager.cpp"
        ).read_text(encoding="utf-8")
        config = (
            ROOT / "application" / "Cpp_Core" / "Src" / "config.cpp"
        ).read_text(encoding="utf-8")
        webconfig_state = (
            ROOT
            / "application"
            / "Cpp_Core"
            / "Src"
            / "states"
            / "webconfig_state.cpp"
        ).read_text(encoding="utf-8")
        board_config = (
            ROOT / "application" / "Core" / "Inc" / "board_cfg.h"
        ).read_text(encoding="utf-8")

        self.assertNotIn("3000 / opts->ledAnimationSpeed", manager)
        self.assertIn("LedConfigSafety::rippleDurationMs", manager)
        self.assertEqual(
            manager.count("LedConfigSafety::scaleGammaPercentToCap"), 1
        )
        self.assertGreaterEqual(manager.count("brightnessPercentToDrive8("), 4)
        self.assertIn("startupRampDriveBrightness", manager)
        self.assertIn("LedConfigSafety::interpolateDrive8", manager)
        self.assertNotIn("uint8_t LEDsManager::startupRampBrightness", manager)
        self.assertIn("sanitize_led_profiles(config)", config)
        self.assertIn("repaired invalid LED configuration", config)
        self.assertIn("LEDS_MANAGER.deinit();", webconfig_state)
        self.assertNotIn("LEDS_MANAGER.setup();", webconfig_state)
        self.assertIn("#define WEBCONFIG_TEST_FORCE_BOOT 0", board_config)
        self.assertIn("#define INPUT_LED_RECOVERY_HOLD_OFF                0", board_config)
        self.assertIn("frontColor = hexToRGB(opts->ledColor1);", manager)

    def test_ws2812_single_frame_dma_ownership_and_timing(self) -> None:
        driver = (ROOT / "application/Drivers/PWM-WS2812B/pwm-ws2812b.c").read_text(encoding="utf-8")
        timer = (ROOT / "application/Core/Src/tim.c").read_text(encoding="utf-8")
        self.assertNotIn("double_t brightness", driver)
        self.assertNotIn("SCB_CleanInvalidateDCache_by_Addr", driver)
        self.assertIn("SCB_CleanDCache_by_Addr", driver)
        self.assertIn("WS2812B_UPDATE_ENCODING", driver)
        self.assertIn("WS2812B_UPDATE_TRANSMITTING", driver)
        self.assertNotIn("WS2812B_UPDATE_WAIT_HT", driver)
        self.assertNotIn("copy_staging_to_dma_frame", driver)
        self.assertNotIn("g_keys_staged_dma", driver)
        self.assertIn("__HAL_DMA_DISABLE_IT(hdma, DMA_IT_HT)", driver)
        self.assertIn("hdma_tim4_ch1.Init.Mode = DMA_NORMAL", timer)
        self.assertIn("hdma_tim4_ch2.Init.Mode = DMA_NORMAL", timer)
        # CC requests move to UPDATE but retain independent CC1/CC2 DMA streams.
        init = timer[timer.index("void MX_TIM4_Init(void)"):
                     timer.index("void HAL_TIM_Base_MspInit")]
        self.assertIn("SET_BIT(htim4.Instance->CR2, TIM_CR2_CCDS)", init)
        self.assertNotIn("DMA_REQUEST_TIM4_UP", timer)
        for token in ("KEYS_HIGH_CCR_CODE       150u", "KEYS_LOW_CCR_CODE         72u",
                      "AMBIENT_HIGH_CCR_CODE    150u", "AMBIENT_LOW_CCR_CODE      72u",
                      "WS2812B_KEYS_RESET_SLOT_COUNT       10u",
                      "WS2812B_AMBIENT_RESET_SLOT_COUNT    10u"):
            self.assertIn(token, driver)
        self.assertNotIn("2u * WS2812B_FRAME_BUFFER_LEN_FOR", driver)
        callbacks = driver[driver.index("static void complete_strip"):
                           driver.index("void WS2812B_InitStrip")]
        for forbidden in ("memcpy(", "led_data_to_buffer(", "encode_submitted_frame(",
                          "HAL_TIM_PWM_Start_DMA(", "HAL_DMA_Start_IT(", "APP_ERR(", "APP_DBG("):
            self.assertNotIn(forbidden, callbacks)
        self.assertIn("__HAL_DMA_GET_COUNTER(hdma) != 0u", callbacks)
        self.assertIn("hdma->ErrorCode != HAL_DMA_ERROR_NONE", callbacks)
        self.assertIn("*strip_applied_generation(strip) = *strip_in_flight_generation(strip)", callbacks)
        self.assertNotIn("HAL_DMA_PollForTransfer(", driver)
        self.assertNotIn("__HAL_TIM_GENERATE_EVENT", driver)
        self.assertNotIn("__HAL_TIM_SET_COUNTER", driver)
        self.assertIn("__HAL_TIM_DISABLE_OCxPRELOAD", driver)
        self.assertIn("__HAL_TIM_ENABLE_OCxPRELOAD", driver)
        self.assertNotIn("HAL_TIM_PWM_Start_DMA(", driver)
        self.assertIn("hdma->XferHalfCpltCallback = NULL", driver)
        submit = driver[driver.index("static bool start_encoded_frame"):
                        driver.index("void WS2812B_ServiceStrip")]
        self.assertLess(submit.index("HAL_DMA_Start_IT("), submit.index("htim4.Instance->CCER |="))
        self.assertLess(submit.index("htim4.Instance->CCER |="), submit.index("__HAL_TIM_ENABLE_DMA("))
        self.assertLess(submit.index("__HAL_TIM_ENABLE(&htim4)"), submit.index("__HAL_TIM_ENABLE_DMA("))
        self.assertIn("hdma->State == HAL_DMA_STATE_BUSY && HAL_DMA_Abort(hdma)", driver)
        legacy = driver[driver.index("void LEDDataToDMABuffer"):
                        driver.index("static volatile WS2812B_StateTypeDef* strip_state")]
        self.assertIn("WS2812B_RefreshStrip", legacy)
        self.assertNotIn("led_data_to_buffer", legacy)

    def test_led_switches_control_only_the_selected_rail(self) -> None:
        manager = (
            ROOT / "application" / "Cpp_Core" / "Src" / "leds" / "leds_manager.cpp"
        ).read_text(encoding="utf-8")
        driver = (
            ROOT / "application" / "Drivers" / "PWM-WS2812B" / "pwm-ws2812b.c"
        ).read_text(encoding="utf-8")

        key_start = manager.index("void LEDsManager::enableSwitch()")
        key_end = manager.index("void LEDsManager::setLedsBrightness")
        key_switch = manager[key_start:key_end]
        ambient_start = manager.index("void LEDsManager::ambientLightEnableSwitch()")
        ambient_switch = manager[ambient_start:]

        self.assertIn("keyStrip.setPowerEnabled", key_switch)
        self.assertNotIn("deinit();", key_switch)
        self.assertNotIn("setup();", key_switch)
        self.assertIn("ambientStrip.setPowerEnabled", ambient_switch)
        self.assertNotIn("deinit();", ambient_switch)
        self.assertNotIn("setup();", ambient_switch)
        self.assertIn("PI6=%u PI7=%u", key_switch)
        self.assertIn("PI6=%u PI7=%u", ambient_switch)

        stop_start = driver.index("WS2812B_StateTypeDef WS2812B_StopStrip")
        stop_end = driver.index("WS2812B_StateTypeDef WS2812B_GetStateStrip")
        stop_strip = driver[stop_start:stop_end]
        self.assertNotIn("HAL_TIM_PWM_Stop_DMA", stop_strip)
        self.assertIn("__HAL_TIM_DISABLE_DMA(&htim4, strip_dma_request(strip))", stop_strip)
        request_selector = driver[driver.index("static uint32_t strip_dma_request"):
                                  driver.index("static void disable_strip_output")]
        self.assertIn("TIM_DMA_CC1", request_selector)
        self.assertIn("TIM_DMA_CC2", request_selector)
        self.assertIn("otherState == WS2812B_RUNNING", stop_strip)

    def test_webconfig_preview_updates_led_strips_in_place(self) -> None:
        manager = (
            ROOT / "application" / "Cpp_Core" / "Src" / "leds" / "leds_manager.cpp"
        ).read_text(encoding="utf-8")

        preview_start = manager.index("void LEDsManager::setTemporaryConfig")
        preview_end = manager.index("void LEDsManager::restoreDefaultConfig")
        preview_update = manager[preview_start:preview_end]

        self.assertNotIn("deinit();", preview_update)
        self.assertIn("if (!runtimeWasEnabled)", preview_update)
        self.assertEqual(preview_update.count("setup();"), 1)
        self.assertIn("keyStrip.setPowerEnabled(false)", preview_update)
        self.assertIn("ambientStrip.setPowerEnabled(false)", preview_update)
        self.assertIn("keyStrip.start()", preview_update)
        self.assertIn("ambientStrip.start()", preview_update)

    def test_key_led_count_and_tail_mapping_are_exact(self) -> None:
        board_config = (
            ROOT / "application" / "Core" / "Inc" / "board_cfg.h"
        ).read_text(encoding="utf-8")
        driver = (
            ROOT
            / "application"
            / "Drivers"
            / "PWM-WS2812B"
            / "pwm-ws2812b.c"
        ).read_text(encoding="utf-8")

        self.assertEqual(board_config.count("ADC_REGULAR_RANK_"), 18)
        self.assertIn("#define NUM_GPIO_BUTTONS            4", board_config)
        for index in range(18, 22):
            self.assertIn(f"/* {index} */", board_config)
            self.assertIn(f"GPIO_BTN{index - 17}_VIRTUAL_PIN       {index}", board_config)
        self.assertIn(
            "WS2812B_KEYS_LED_COUNT = (NUM_ADC_BUTTONS + NUM_GPIO_BUTTONS)",
            driver,
        )
        self.assertNotIn("WS2812B_KEYS_TX_LED_COUNT", driver)

    def test_product_led_runtime_has_expected_caps_and_no_isolation_mode(self) -> None:
        board_config = (
            ROOT / "application" / "Core" / "Inc" / "board_cfg.h"
        ).read_text(encoding="utf-8")
        controller = (
            ROOT
            / "application"
            / "Cpp_Core"
            / "Src"
            / "leds"
            / "led_strip_controller.cpp"
        ).read_text(encoding="utf-8")
        main_source = (
            ROOT / "application" / "Core" / "Src" / "main.c"
        ).read_text(encoding="utf-8")
        screen = (
            ROOT
            / "application"
            / "Cpp_Core"
            / "Src"
            / "screen_control"
            / "spi_screen_manager.cpp"
        ).read_text(encoding="utf-8")

        self.assertIn("#define FPS_OF_LED_ANIMATION        30", board_config)
        self.assertIn("WS2812B_KEYS_TIM_DMA_IRQn_PRIO          6u", board_config)
        self.assertIn("WS2812B_AMBIENT_TIM_DMA_IRQn_PRIO       6u", board_config)
        self.assertNotIn("INPUT_LED_ONLY_DIAGNOSTIC", board_config)
        self.assertNotIn("INPUT_FORCE_AMBIENT_LED_OFF", board_config)
        self.assertNotIn("runCombinedLedDiagnostic", main_source)
        self.assertIn("NUM_ADC_BUTTONS + NUM_GPIO_BUTTONS", controller)
        self.assertIn("NUM_LED_AROUND", controller)
        self.assertIn("WS2812B_KEYS_TIM_CHANNEL", controller)
        self.assertIn("WS2812B_AMBIENT_TIM_CHANNEL", controller)
        self.assertIn("WS2812B_KEYS_TIM_DMA_INSTANCE", controller)
        self.assertIn("WS2812B_AMBIENT_TIM_DMA_INSTANCE", controller)
        self.assertIn("LED_EN_PORT", controller)
        self.assertIn("AMBIENT_EN_PORT", controller)
        self.assertIn("kKeyMaxHardwareDrivePercent", controller)
        self.assertIn("kAmbientMaxHardwareDrivePercent", controller)
        self.assertIn("kLcdMaxHardwareDrivePercent", screen)
        self.assertIn("map_backlight_percent", screen)

    def test_all_configured_effects_generate_and_submit_frames(self) -> None:
        animation = (
            ROOT / "application" / "Cpp_Core" / "Src" / "leds" / "led_animation.cpp"
        ).read_text(encoding="utf-8")
        manager = (
            ROOT / "application" / "Cpp_Core" / "Src" / "leds" / "leds_manager.cpp"
        ).read_text(encoding="utf-8")

        for effect in (
            "LEDEffect::STATIC",
            "LEDEffect::BREATHING",
            "LEDEffect::STAR",
            "LEDEffect::FLOWING",
            "LEDEffect::RIPPLE",
            "LEDEffect::TRANSFORM",
        ):
            self.assertIn(effect, animation)
        for effect in (
            "AroundLEDEffect::AROUND_STATIC",
            "AroundLEDEffect::AROUND_BREATHING",
            "AroundLEDEffect::AROUND_QUAKE",
            "AroundLEDEffect::AROUND_METEOR",
        ):
            self.assertIn(effect, manager)
        self.assertIn("getLedAnimation(opts->ledEffect)", manager)
        loop_start = manager.index("void LEDsManager::loop(uint32_t virtualPinMask)")
        loop_end = manager.index(
            "uint8_t LEDsManager::startupRampDriveBrightness"
        )
        self.assertEqual(manager[loop_start:loop_end].count("submitFrame()"), 2)

    def test_calibration_colors_are_submitted_to_the_key_strip(self) -> None:
        calibration = (
            ROOT
            / "application"
            / "Cpp_Core"
            / "Src"
            / "adc_btns"
            / "adc_calibration.cpp"
        ).read_text(encoding="utf-8")

        process = calibration[
            calibration.index("void ADCCalibrationManager::processCalibration()"):
            calibration.index("bool ADCCalibrationManager::shouldSampleButton")
        ]
        update_all = calibration[
            calibration.index("void ADCCalibrationManager::updateAllLEDs()"):
            calibration.index("void ADCCalibrationManager::printButtonCalibrationCompleted")
        ]

        # SetLEDColor/SetLEDBrightness only dirty the edit frame in the
        # circular-DMA driver.  Initial colours need one coherent publish,
        # and the calibration loop must retry while a prior HT/TC update is
        # still in flight.
        self.assertIn("WS2812B_ServiceStrip(WS2812B_STRIP_KEYS)", update_all)
        self.assertIn("WS2812B_GetStateStrip(WS2812B_STRIP_KEYS)", process)
        self.assertIn("WS2812B_ServiceStrip(WS2812B_STRIP_KEYS)", process)

    def test_calibration_uses_public_adc_scale_and_turns_key_strip_off(self) -> None:
        calibration = (
            ROOT
            / "application"
            / "Cpp_Core"
            / "Src"
            / "adc_btns"
            / "adc_calibration.cpp"
        ).read_text(encoding="utf-8")

        scale_helper = calibration[
            calibration.index("static uint16_t mappingEndpointInPublicAdcScale"):
            calibration.index("} // namespace")
        ]
        initialize = calibration[
            calibration.index("void ADCCalibrationManager::initializeButtonStates()"):
            calibration.index("bool ADCCalibrationManager::loadExistingCalibration()")
        ]
        stop = calibration[
            calibration.index("ADCBtnsError ADCCalibrationManager::stopCalibration()"):
            calibration.index("ADCBtnsError ADCCalibrationManager::resetAllCalibration()")
        ]

        self.assertIn("ADC_VALUE_PUBLIC_RIGHT_SHIFT", scale_helper)
        self.assertIn("fullResolutionMapping", scale_helper)
        self.assertEqual(
            initialize.count("mappingEndpointInPublicAdcScale("), 2
        )
        self.assertIn("state.ledColor = CalibrationLEDColor::OFF", stop)
        self.assertIn(
            "WS2812B_SetAllLEDBrightnessStrip(WS2812B_STRIP_KEYS, 0u)",
            stop,
        )
        self.assertIn("WS2812B_StopStrip(WS2812B_STRIP_KEYS)", stop)

    def test_power_and_led_runtime_initialization_order(self) -> None:
        state_machine = (
            ROOT / "application" / "Cpp_Core" / "Src" / "main_state_machine.cpp"
        ).read_text(encoding="utf-8")
        input_state = (
            ROOT / "application" / "Cpp_Core" / "Src" / "states" / "input_state.cpp"
        ).read_text(encoding="utf-8")

        interactive_start = state_machine.index(
            "void MainStateMachine::initializeInteractiveRuntime()"
        )
        interactive_end = state_machine.index(
            "MainRuntimeState MainStateMachine::resolveNormalStartupState()"
        )
        interactive = state_machine[interactive_start:interactive_end]
        self.assertLess(
            interactive.index("SPIScreenManager::getInstance().setup()"),
            interactive.index("POWER_MANAGER.setup()"),
        )

        pipeline_start = input_state.index("void InputState::startInputPipeline()")
        pipeline_end = input_state.index("void InputState::stopInputPipeline()")
        pipeline = input_state[pipeline_start:pipeline_end]
        self.assertLess(
            pipeline.index("LEDS_MANAGER.setup()"),
            pipeline.index("REPORT_SCHEDULER.start"),
        )
        self.assertNotIn("LEDS_MANAGER.loop", input_state[
            input_state.index("void InputState::tick()"):
            input_state.index("void InputState::serviceLeds()")
        ])
        self.assertLess(
            state_machine.index("SPIScreenManager::getInstance().loop()"),
            state_machine.index("INPUT_STATE.serviceLeds()"),
        )


if __name__ == "__main__":
    unittest.main()
