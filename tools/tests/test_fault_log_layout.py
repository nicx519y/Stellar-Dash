"""Read local ELF files only. Never opens a debug probe or a device."""
from pathlib import Path
import struct
import unittest

ROOT = Path(__file__).resolve().parents[2]

def elf(path):
    data = path.read_bytes()
    header = struct.unpack_from("<16sHHIIIIIHHHHHH", data)
    assert header[0][:6] == b"\x7fELF\x01\x01", "expected little-endian ELF32"
    sections = [struct.unpack_from("<IIIIIIIIII", data, header[6] + i * header[11])
                for i in range(header[12])]
    names_section = sections[header[13]]
    names = data[names_section[4]:names_section[4] + names_section[5]]
    def string(table, offset):
        return table[offset:table.index(b"\0", offset)].decode()
    by_name = {string(names, s[0]): s for s in sections}
    symbols = {}
    for section in sections:
        if section[1] != 2:
            continue
        strings = sections[section[6]]
        table = data[strings[4]:strings[4] + strings[5]]
        for offset in range(section[4], section[4] + section[5], section[9]):
            name, value, size, info, other, index = struct.unpack_from("<IIIBBH", data, offset)
            if name:
                symbols[string(table, name)] = (value, size, index, info)
    return by_name, symbols

class FaultLogLayoutTests(unittest.TestCase):
    def test_tx_retained_ram_and_budget(self):
        sections, symbols = elf(ROOT / "RF_PHY_Hop/TX/build_tx/RF_PHY_Hop_TX.elf")
        retained = sections[".noinit.xora_fault"]
        self.assertEqual(retained[1], 8, "retained section must be NOBITS")
        self.assertGreaterEqual(retained[3], symbols["_ebss"][0])
        self.assertGreaterEqual(symbols["_eusrstack"][0] - retained[3] - retained[5], 2048)
        allocated = sum(s[1] for name, s in symbols.items() if name.startswith("log_") and (s[3] & 15) == 1)
        self.assertLessEqual(allocated, 512)
        self.assertNotIn("_sbrk", symbols, "heap must not allocate over orphan NOLOAD storage")
        iap_sections, iap_symbols = elf(ROOT / "RF_PHY_Hop/TX/build_tx/RF_PHY_Hop_TX_iap.elf")
        for section in iap_sections.values():
            if section[2] & 2 and section[5] and 0x20000000 <= section[3] < 0x20020000:
                self.assertTrue(section[3] + section[5] <= retained[3] or
                                section[3] >= retained[3] + retained[5],
                                "IAP load/clear section overlaps crash mailbox")
        self.assertGreaterEqual(iap_symbols["_eusrstack"][0] - retained[3] - retained[5], 2048)
        print(f"TX logging RAM={allocated} bytes; retained=0x{retained[3]:08x}; stack reserve="
              f"{symbols['_eusrstack'][0]-retained[3]-retained[5]} bytes", flush=True)

    def test_stm32_retained_and_ram_execution(self):
        sections, symbols = elf(ROOT / "application/build/application.elf")
        retained = sections[".xora_fault"]
        self.assertEqual(retained[1], 8)
        self.assertGreaterEqual(retained[3], symbols["_ebss"][0])
        self.assertLessEqual(retained[3]+retained[5], 0x24080000)
        boot_sections, _ = elf(ROOT / "bootloader/build/bootloader.elf")
        for section in boot_sections.values():
            if section[2] & 2 and section[5]:
                self.assertTrue(section[3] + section[5] <= retained[3] or
                                section[3] >= retained[3] + retained[5],
                                "bootloader load/clear section overlaps crash mailbox")
        for name in ("XoraLog_CaptureFault", "XoraLog_Service", "XoraLog_Read"):
            address = symbols[name][0] & ~1
            self.assertLess(address, 0x10000, f"{name} must execute from ITCM")
        print(f"STM32 retained=0x{retained[3]:08x}; AXI free="
              f"{0x24080000-retained[3]-retained[5]} bytes; logging executes in ITCM", flush=True)

if __name__ == "__main__":
    unittest.main()
