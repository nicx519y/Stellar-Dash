"""Validate native font storage in the exact STM32 ELF; no hardware access."""
import os
from pathlib import Path
import struct
import unittest

from tools.generate_screen_fonts import SOURCE, STRIKES, read_strike
from tools.tests.application_paths import ROOT
from tools.tests.test_fault_log_layout import elf


class ScreenFontLayoutTests(unittest.TestCase):
    def test_font_load_image_dtcm_heap_and_stack(self):
        path = Path(os.environ.get("XORA_APPLICATION_ELF", ROOT / "application/build/application.elf"))
        sections, symbols = elf(path)
        data = path.read_bytes()
        section = sections[".screen_fonts"]
        start, size = section[3], section[5]
        self.assertEqual(section[1], 1, "font tables must have a load image")
        self.assertEqual(section[2], 2, "font tables are read-only allocated data")
        self.assertGreaterEqual(start, 0x20000000)
        self.assertEqual(symbols["_sscreen_fonts"][0], start)
        self.assertEqual(symbols["_escreen_fonts"][0], start + size)
        self.assertLessEqual(start + size, symbols["_end"][0])
        self.assertEqual(symbols["_estack"][0], 0x20020000)
        heap_budget = symbols["_estack"][0] - symbols["_Min_Stack_Size"][0] - symbols["_end"][0]
        self.assertGreaterEqual(heap_budget, symbols["_Min_Heap_Size"][0])
        self.assertGreaterEqual(symbols["_Min_Stack_Size"][0], 8192)
        # The source address used at reset must refer to these actual PROGBITS.
        header = struct.unpack_from("<16sHHIIIIIHHHHHH", data)
        loads = [struct.unpack_from("<IIIIIIII", data, header[5] + i * header[9])
                 for i in range(header[10])]
        segment = next(p for p in loads if p[0] == 1 and p[2] <= start and start + size <= p[2] + p[4])
        lma = segment[3] + start - segment[2]
        self.assertEqual(symbols["_siscreen_fonts"][0], lma)
        self.assertGreaterEqual(lma, 0x90000000)
        self.assertLess(lma + size, 0x90200000)
        # Verify all four compiled tables against the original source bitmaps.
        total = 0
        for name, filename, width, height in STRIKES:
            address, length, _, _ = symbols["screen_font_" + name]
            expected = b"".join(read_strike(SOURCE / filename, width, height).values())
            offset = section[4] + address - start
            self.assertEqual(data[offset:offset + length], expected)
            self.assertGreaterEqual(address, start)
            self.assertLessEqual(address + length, start + size)
            total += length
        self.assertLessEqual(size - total, 12, "only alignment padding is expected")
        print(f"ELF={path}; native font data={size} bytes in DTCM; heap capacity={heap_budget}; MSP reserve>=8192")


if __name__ == "__main__":
    unittest.main()
