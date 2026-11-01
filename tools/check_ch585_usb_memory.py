"""Offline CH585 ELF RAM/DMA gate, including orphan/noinit sections. No device IO."""
import argparse
import json
from pathlib import Path
import struct

RAM_START = 0x20000000
RAM_END = 0x20020000
DMA_SYMBOLS = ("rx_dma", "tx_dma", "control_data", "interrupt_data", "g_usb_mode_memory",
               "s_ep1_tx", "s_ep7_tx", "s_rx_dma", "s_spi_rx_dma_buf")


def inspect(path: Path):
    data = path.read_bytes()
    if data[:7] != b"\x7fELF\x01\x01\x01":
        raise ValueError("expected ELF32 little endian")
    offset = struct.unpack_from("<I", data, 32)[0]
    entry, count, strings = struct.unpack_from("<HHH", data, 46)
    sections = [struct.unpack_from("<10I", data, offset + i * entry) for i in range(count)]
    table = sections[strings]
    names = data[table[4]:table[4] + table[5]]
    allocated = []
    symbols = {}
    for section in sections:
        name = names[section[0]:].split(b"\0", 1)[0].decode()
        if section[2] & 2 and section[5] and RAM_START <= section[3] < RAM_END:
            allocated.append({"name": name, "start": section[3],
                              "end": section[3] + section[5], "size": section[5]})
        if section[1] == 2:  # SHT_SYMTAB
            strings_section = sections[section[6]]
            symbol_names = data[strings_section[4]:strings_section[4] + strings_section[5]]
            for pos in range(section[4], section[4] + section[5], section[9]):
                name_offset, address, size, _, _, index = struct.unpack_from("<IIIBBH", data, pos)
                symbol = symbol_names[name_offset:].split(b"\0", 1)[0].decode()
                if index and symbol in DMA_SYMBOLS:
                    symbols[symbol] = {"address": address, "size": size}
    if not allocated:
        raise ValueError("no allocated RAM sections")
    end = max(section["end"] for section in allocated)
    errors = []
    for name in DMA_SYMBOLS:
        symbol = symbols.get(name)
        if not symbol or not symbol["size"]:
            errors.append(f"missing DMA symbol: {name}")
        elif symbol["address"] % 4 or not RAM_START <= symbol["address"] < symbol["address"] + symbol["size"] <= RAM_END:
            errors.append(f"unaligned or out-of-RAM DMA symbol: {name}")
    return {"elf": str(path), "ram_end": hex(end), "stack_headroom": RAM_END - end,
            "required_stack": 4096, "sections": allocated, "dma_symbols": symbols, "errors": errors}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", type=Path)
    args = parser.parse_args()
    report = inspect(args.elf)
    print(json.dumps(report, indent=2))
    if report["stack_headroom"] < 4096 or report["errors"]:
        raise SystemExit("CH585 RAM/DMA budget FAILED")


if __name__ == "__main__":
    main()
