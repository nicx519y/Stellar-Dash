"""Inspect the exact battery-test development ELF; never accesses hardware."""
from pathlib import Path
import os
import unittest
from tools.tests.test_fault_log_layout import elf

ROOT = Path(__file__).resolve().parents[2]


class BatteryTestLayoutTests(unittest.TestCase):
    def test_development_elf(self):
        image = Path(os.environ.get("XORA_APPLICATION_ELF", ROOT / "application/build_battery_test/application.elf"))
        sections, symbols = elf(image)
        for name in ("BatteryTest_Service", "BatteryTest_Tick", "BatteryTest_GetInfo",
                     "XoraLog_Service", "XoraLog_CaptureFault"):
            self.assertLess(symbols[name][0] & ~1, 0x10000, name + " must execute in ITCM")
        self.assertLessEqual(symbols["_evector"][0], 0x10000)
        # Check the actual image (including the formal trust-injected variant),
        # not only whether a linker wildcard appears in a source file.
        helpers = [symbol for name, symbol in symbols.items()
                   if symbol[3] == 0x12  # GLOBAL FUNC: exclude local lambdas and branch veneers.
                   and ("ScreenUI_DrawStringCenteredInBox" in name or "ScreenUI_DrawActionIcon" in name)]
        self.assertEqual(len(helpers), 2)
        vector = sections[".isr_vector"]
        for address, size, _, _ in helpers:
            self.assertGreaterEqual(address & ~1, vector[3])
            self.assertLessEqual((address & ~1) + size, symbols["_evector"][0])
        self.assertEqual(vector[3] + vector[5], symbols["_evector"][0])
        self.assertGreaterEqual(0x10000 - symbols["_evector"][0], 1024, "ITCM headroom")
        recorder = next(s for name, s in symbols.items() if name.endswith("recorderE"))
        self.assertGreaterEqual(recorder[0], 0x38000000)
        self.assertLessEqual(recorder[0] + recorder[1], 0x3800F800)
        self.assertEqual(sections["._RAM_D3_Area"][1], 8, "D3 state is explicitly initialized NOLOAD")
        retained = sections[".xora_fault"]
        sleep = sections[".sleep_fault"]
        self.assertEqual(retained[1], 8)
        self.assertGreaterEqual(retained[3], symbols["_ebss"][0])
        self.assertLessEqual(retained[3] + retained[5], sleep[3])
        self.assertLessEqual(sleep[3] + sleep[5], 0x24080000)
        self.assertGreaterEqual(0x24080000-sleep[3]-sleep[5], 1024, "AXI headroom including formal trust")
        stack = symbols["_estack"][0]
        self.assertEqual(stack, 0x20020000)
        self.assertGreaterEqual(symbols["_Min_Stack_Size"][0], 8192)
        self.assertLessEqual(sections["._user_heap_stack"][3] + sections["._user_heap_stack"][5], stack)
        boot_sections, _ = elf(ROOT / "bootloader/build/bootloader.elf")
        for section in boot_sections.values():
            if section[2] & 2 and section[5]:
                self.assertTrue(section[3] + section[5] <= retained[3] or
                                section[3] >= sleep[3] + sleep[5], "bootloader overlaps retained fault RAM")
        print(f"ELF={image}; Battery queue/state={recorder[1]} bytes in D3; "
              f"ITCM free={0x10000-symbols['_evector'][0]} bytes; "
              f"AXI tail free={0x24080000-sleep[3]-sleep[5]} bytes; MSP reserve>=8192", flush=True)


if __name__ == "__main__":
    unittest.main()
