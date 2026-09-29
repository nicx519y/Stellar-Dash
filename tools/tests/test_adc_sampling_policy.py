from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

try:
    from .application_paths import run_native
except ImportError:
    from application_paths import run_native


ROOT = Path(__file__).resolve().parents[2]


class AdcSamplingPolicyTests(unittest.TestCase):
    def test_scan_deadlines_and_calibrated_value_scale(self):
        compiler = shutil.which("g++") or shutil.which("clang++")
        self.assertIsNotNone(compiler, "a host C++ compiler is required")
        with tempfile.TemporaryDirectory() as temporary:
            executable = Path(temporary) / "adc_sampling_policy_test.exe"
            result = run_native(
                [compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                 f"-I{ROOT / 'application/Inc/input/drivers/adc'}",
                 str(ROOT / "tools/tests/adc_sampling_policy_test.cpp"),
                 "-o", str(executable)],
                capture_output=True, text=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = run_native([str(executable)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_reconfiguration_is_between_stopping_and_rearming_all_adcs(self):
        manager = (ROOT / 'application/Src/input/adc_btns/adc_manager.cpp').read_text(
            encoding="utf-8"
        )
        rearm = manager[manager.index("bool ADCManager::rearmInputSampling("):]
        configure = rearm.index("ADC_ConfigureInputOversampling(reportRateHz)")
        for adc in ("hadc1", "hadc2", "hadc3"):
            self.assertLess(rearm.index(f"HAL_ADC_Stop_DMA(&{adc})"), configure)
            self.assertGreater(rearm.index(f"HAL_ADC_Start_DMA(&{adc},"), configure)
        failure = rearm[configure:rearm.index("memset(ADC1_Values", configure)]
        self.assertIn("forceStopAllSampling();", failure)
        self.assertIn("return false;", failure)
        scheduler = (ROOT / 'application/Src/input/report_scheduler.cpp').read_text(
            encoding="utf-8"
        )
        rearm_at = scheduler.index("ADC_MANAGER.rearmInputSampling(runningRateHz)")
        self.assertLess(scheduler.index("HAL_TIM_Base_Stop_IT(&htim2)"), rearm_at)
        self.assertGreater(scheduler.index("HAL_TIM_Base_Start_IT(&htim2)"), rearm_at)


if __name__ == "__main__":
    unittest.main()
