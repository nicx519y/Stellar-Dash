"""Validate native font storage in the exact STM32 ELF; no hardware access."""
import os
from pathlib import Path
import struct
import unittest

from tools.generate_screen_fonts import SOURCE, STRIKES, read_strike
from tools import generate_screen_chinese as chinese
from tools.tests.application_paths import ROOT
from tools.tests.test_fault_log_layout import elf


class ScreenFontLayoutTests(unittest.TestCase):
    def test_ui_constants_copy_and_heap_budget(self):
        path = Path(os.environ.get("XORA_APPLICATION_ELF", ROOT / "application/build/application.elf"))
        sections, symbols = elf(path)
        data = path.read_bytes()
        ui, fonts = sections[".ui_rodata"], sections[".screen_fonts"]
        self.assertEqual(ui[1:3], (1, 2), "initialized read-only data, never executable or NOLOAD")
        self.assertEqual(symbols["_sui_rodata"][0], ui[3])
        self.assertEqual(symbols["_eui_rodata"][0], ui[3] + ui[5])
        self.assertGreaterEqual(ui[3], 0x20000000)
        self.assertLessEqual(ui[3] + ui[5], fonts[3])
        self.assertLessEqual(fonts[3] + fonts[5], symbols["_end"][0])
        self.assertGreaterEqual(symbols["_estack"][0] - symbols["_Min_Stack_Size"][0] - symbols["_end"][0], 32768)
        contents = data[ui[4]:ui[4]+ui[5]]
        for text in (b"Clear Records\0", b"Records cleared\0", b"History exists / clear first\0"):
            self.assertIn(text, contents)
        mappings = next(s for name, s in symbols.items() if "ScreenUI_ActionIcon" in name and name.endswith("mappings"))
        self.assertGreaterEqual(mappings[0], ui[3])
        self.assertLessEqual(mappings[0]+mappings[1], ui[3]+ui[5])
        header = struct.unpack_from("<16sHHIIIIIHHHHHH", data)
        loads = [struct.unpack_from("<IIIIIIII", data, header[5]+i*header[9]) for i in range(header[10])]
        segment = next(p for p in loads if p[0]==1 and p[2]<=ui[3] and ui[3]+ui[5]<=p[2]+p[4])
        lma = segment[3]+ui[3]-segment[2]
        self.assertEqual(symbols["_siui_rodata"][0], lma)
        self.assertGreaterEqual(lma, 0x90000000)
        self.assertLess(lma+ui[5], 0x90200000)
        # The vector load image must still start the firmware slot.
        vector = sections[".isr_vector"]
        vector_load = next(p for p in loads if p[0]==1 and p[2]<=vector[3] and vector[3]+vector[5]<=p[2]+p[4])
        self.assertEqual(vector_load[3]+vector[3]-vector_load[2], 0x90000000)
        startup = (ROOT / "application/startup_stm32h750xx.s").read_text(encoding="utf-8")
        self.assertRegex(startup, r"ldr r0, =_siui_rodata\s+ldr r1, =_sui_rodata\s+ldr r2, =_eui_rodata\s+bl\s+copy_section")
        boot = sections[".text.boot"]
        boot_bytes = data[boot[4]:boot[4]+boot[5]]
        for symbol in ("_siui_rodata", "_sui_rodata", "_eui_rodata"):
            self.assertIn(struct.pack("<I", symbols[symbol][0]), boot_bytes, "compiled startup copy literal")
        print(f"UI constants={ui[5]} bytes in DTCM; flash load, startup copy literals and >=32 KiB heap verified")

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
        chars = chinese.characters()
        tables = {"screen_cjk_codes": struct.pack("<" + "H" * len(chars), *chars)}
        for width, height, weight in chinese.STRIKES:
            font = chinese.load_font(height, weight)
            tables[f"screen_cjk_{height}"] = b"".join(chinese.raster(font, c, width, height) for c in chars)
        blob = bytearray(); offsets = bytearray()
        for en, zh in sorted(chinese.catalog().items()):
            first = len(blob); blob += en.encode() + b'\0'
            second = len(blob); blob += zh.encode() + b'\0'
            offsets += struct.pack('<HH', first, second)
        tables.update(screen_catalog_blob=blob, screen_catalog_offsets=offsets)
        for name, expected in tables.items():
            address, length, _, _ = symbols[name]
            self.assertEqual(length, len(expected))
            self.assertGreaterEqual(address, start)
            self.assertLessEqual(address + length, start + size)
            offset = section[4] + address - start
            self.assertEqual(data[offset:offset + length], expected)
            total += length
        self.assertLessEqual(size - total, 3 * (len(STRIKES) + len(tables)), "only alignment padding is expected")
        print(f"ELF={path}; native font data={size} bytes in DTCM; heap capacity={heap_budget}; MSP reserve>=8192")


if __name__ == "__main__":
    unittest.main()
