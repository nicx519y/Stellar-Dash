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

    def test_ws2812_circular_dma_path_avoids_floating_point_and_cache_invalidation(self) -> None:
        driver = (
            ROOT
            / "application"
            / "Drivers"
            / "PWM-WS2812B"
            / "pwm-ws2812b.c"
        ).read_text(encoding="utf-8")

        self.assertNotIn("double_t brightness", driver)
        self.assertNotIn("SCB_CleanInvalidateDCache_by_Addr", driver)
        self.assertIn("SCB_CleanDCache_by_Addr", driver)
        self.assertIn("HAL_TIM_PWM_Start_DMA", driver)
        self.assertIn("WS2812B_SubmitStrip", driver)
        self.assertIn("WS2812B_UPDATE_WAIT_HT", driver)
        self.assertIn("WS2812B_UPDATE_WAIT_TC", driver)
        self.assertIn(
            "__HAL_DMA_ENABLE_IT(hdma, DMA_IT_HT | DMA_IT_TC)", driver
        )
        self.assertIn("HAL_TIM_PWM_PulseFinishedHalfCpltCallback", driver)
        self.assertIn("HAL_TIM_PWM_PulseFinishedCallback", driver)
        self.assertIn("g_keys_submitted_colors", driver)
        self.assertIn("g_ambient_submitted_colors", driver)
        self.assertIn("g_keys_staged_dma", driver)
        self.assertIn("g_ambient_staged_dma", driver)
        self.assertIn("WS2812B_UPDATE_ENCODING", driver)
        self.assertIn("encode_submitted_to_staging(strip)", driver)
        self.assertIn("copy_staging_to_dma_frame(strip, 0u)", driver)
        self.assertIn("copy_staging_to_dma_frame(strip, 1u)", driver)
        self.assertIn("g_keys_published_generation", driver)
        self.assertIn("g_ambient_published_generation", driver)
        self.assertIn("strip_in_flight_generation", driver)
        self.assertIn("strip_applied_generation", driver)
        self.assertIn("KEYS_HIGH_CCR_CODE       150u", driver)
        self.assertIn("KEYS_LOW_CCR_CODE         72u", driver)
        self.assertIn("AMBIENT_HIGH_CCR_CODE    150u", driver)
        self.assertIn("AMBIENT_LOW_CCR_CODE      72u", driver)
        self.assertIn("strip_high_ccr_code", driver)
        self.assertIn("strip_low_ccr_code", driver)
        self.assertIn("WS2812B_FRAME_BUFFER_LEN_FOR", driver)
        self.assertIn("WS2812B_KEYS_RESET_SLOT_COUNT       10u", driver)
        self.assertIn("WS2812B_AMBIENT_RESET_SLOT_COUNT    10u", driver)
        self.assertIn(
            "2u * WS2812B_FRAME_BUFFER_LEN_FOR((count), (resetSlots))",
            driver,
        )
        self.assertIn(
            "*strip_applied_generation(strip) = *strip_in_flight_generation(strip)",
            driver,
        )
        self.assertIn("__HAL_DMA_CLEAR_FLAG", driver)
        self.assertIn(
            "WS2812B_GetStateStrip(strip) == WS2812B_RUNNING", driver
        )
        half_callback = driver[
            driver.index("void HAL_TIM_PWM_PulseFinishedHalfCpltCallback"):
            driver.index("void HAL_TIM_ErrorCallback")
        ]
        complete_callback = driver[
            driver.index("void HAL_TIM_PWM_PulseFinishedCallback"):
            driver.index("void HAL_TIM_PWM_PulseFinishedHalfCpltCallback")
        ]
        self.assertNotIn("__HAL_DMA_DISABLE_IT", half_callback)
        self.assertNotIn("__HAL_DMA_DISABLE_IT", complete_callback)
        self.assertNotIn("HAL_DMAEx_MultiBufferStart", driver)
        self.assertNotIn("HAL_DMA_PollForTransfer(", driver)
        self.assertNotIn("ledCount - start", complete_callback)
        self.assertNotIn("dmaLen / 2u / 24u", half_callback)
        self.assertNotIn("led_data_to_buffer", half_callback)
        self.assertNotIn("led_data_to_buffer", complete_callback)
        self.assertNotIn("led_data_to_dma_frame", half_callback)
        self.assertNotIn("led_data_to_dma_frame", complete_callback)
        self.assertIn("DMA_CIRCULAR", (
            ROOT / "application" / "Core" / "Src" / "tim.c"
        ).read_text(encoding="utf-8"))

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
        self.assertIn("TIM_DMA_CC1", stop_strip)
        self.assertIn("TIM_DMA_CC2", stop_strip)
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
