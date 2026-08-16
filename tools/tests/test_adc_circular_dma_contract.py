from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]


class AdcCircularDmaContractTests(unittest.TestCase):
    def test_adc_driver_has_one_timer_triggered_circular_configuration(self) -> None:
        source = (
            ROOT / "application" / "Drivers" / "ADC" / "adc.c"
        ).read_text(encoding="utf-8")
        header = (
            ROOT / "application" / "Drivers" / "ADC" / "adc.h"
        ).read_text(encoding="utf-8")

        self.assertIn("ADC_CONVERSIONDATA_DMA_CIRCULAR", source)
        self.assertIn("ADC_EXTERNALTRIG_T2_TRGO", source)
        self.assertIn("ADC_EXTERNALTRIGCONVEDGE_RISING", source)
        self.assertIn("hdma->Init.Mode = DMA_CIRCULAR", source)
        self.assertIn("ContinuousConvMode = DISABLE", source)
        self.assertNotIn("ADC_CONVERSIONDATA_DMA_ONESHOT", source)
        self.assertNotIn("ADC_SOFTWARE_START", source)
        self.assertNotIn("DMA_NORMAL", source)
        self.assertNotIn("ADC_SamplingMode", header + source)
        self.assertNotIn("ADC_SetMode", header + source)

        board = (
            ROOT / "application" / "Core" / "Inc" / "board_cfg.h"
        ).read_text(encoding="utf-8")
        self.assertIn("BOARD_ADC_OVERSAMPLE_RATIO", board)
        self.assertNotIn("BOARD_ADC_LOWLAT_", board)
        self.assertNotIn("BOARD_ADC_CONTINUOUS_", board)

    def test_tim2_update_is_the_sampling_source_and_is_not_auto_started(self) -> None:
        timer = (
            ROOT / "application" / "Core" / "Src" / "tim.c"
        ).read_text(encoding="utf-8")
        scheduler = (
            ROOT / "application" / "Cpp_Core" / "Src" /
            "report_scheduler.cpp"
        ).read_text(encoding="utf-8")

        tim2 = timer[timer.index("void MX_TIM2_Init(void)"):
                     timer.index("/* TIM4 init function */")]
        self.assertIn("MasterOutputTrigger = TIM_TRGO_UPDATE", tim2)
        self.assertNotIn("HAL_TIM_Base_Start", tim2)
        for rate in ("1000", "2000", "4000", "8000"):
            self.assertIn(f"case {rate}:", scheduler)
        self.assertIn("HAL_TIM_Base_Start_IT(&htim2)", scheduler)

    def test_adc_manager_contains_no_one_shot_state_machine(self) -> None:
        source = (
            ROOT / "application" / "Cpp_Core" / "Src" / "adc_btns" /
            "adc_manager.cpp"
        ).read_text(encoding="utf-8")
        header = (
            ROOT / "application" / "Cpp_Core" / "Inc" / "adc_btns" /
            "adc_manager.hpp"
        ).read_text(encoding="utf-8")
        combined = source + header

        for obsolete in (
            "triggerSampling", "startSamplingNow", "startContinuousSampling",
            "completionMask", "samplingDelayUs", "setADCMode", "getADCMode",
        ):
            self.assertNotIn(obsolete, combined)
        self.assertGreaterEqual(source.count("HAL_ADC_Start_DMA"), 3)
        self.assertGreaterEqual(source.count("* 2u)"), 3)
        self.assertIn("HAL_ADC_ConvHalfCpltCallback", source)
        self.assertIn("completedDmaSequence", source)
        self.assertIn("DMA1_START_FAILED", source)
        self.assertIn("DMA2_START_FAILED", source)
        self.assertIn("DMA3_START_FAILED", source)

    def test_all_runtime_states_arm_dma_before_starting_the_clock(self) -> None:
        paths = {
            "input": ROOT / "application" / "Cpp_Core" / "Src" /
                     "states" / "input_state.cpp",
            "webconfig": ROOT / "application" / "Cpp_Core" / "Src" /
                         "states" / "webconfig_state.cpp",
            "calibration": ROOT / "application" / "Cpp_Core" / "Src" /
                           "states" / "calibration_state.cpp",
        }
        sources = {name: path.read_text(encoding="utf-8")
                   for name, path in paths.items()}

        self.assertLess(sources["input"].index("ADC_BTNS_WORKER.setup()"),
                        sources["input"].index("REPORT_SCHEDULER.start(reportRateHz)"))
        for name in ("webconfig", "calibration"):
            self.assertLess(sources[name].index("ADC_MANAGER.startADCSamping(false)"),
                            sources[name].index(
                                "REPORT_SCHEDULER.start(reportRateHz)"))

    def test_worker_setup_clears_retained_button_masks(self) -> None:
        adc_worker = (
            ROOT / "application" / "Cpp_Core" / "Src" / "adc_btns" /
            "adc_btns_worker.cpp"
        ).read_text(encoding="utf-8")
        gpio_worker = (
            ROOT / "application" / "Cpp_Core" / "Src" / "gpio_btns" /
            "gpio_btns_worker.cpp"
        ).read_text(encoding="utf-8")

        adc_setup = adc_worker[adc_worker.index("ADCBtnsError ADCBtnsWorker::setup()"):
                               adc_worker.index("ADCBtnsError ADCBtnsWorker::deinit()")]
        gpio_setup = gpio_worker[gpio_worker.index("void GPIOBtnsWorker::setup()"):
                                 gpio_worker.index("uint32_t GPIOBtnsWorker::read()")]
        self.assertIn("virtualPinMask = 0u;", adc_setup)
        self.assertIn("virtualPinMask = 0u;", gpio_setup)


if __name__ == "__main__":
    unittest.main()
