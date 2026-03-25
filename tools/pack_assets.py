#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import argparse
import binascii
import os
import re
import struct
from pathlib import Path

sys_path_added = False
try:
    import sys
    sys.path.append(os.path.join(os.path.dirname(__file__), '..', 'common'))
    sys_path_added = True
except Exception:
    sys_path_added = False

try:
    from firmware_metadata import SYS_IMAGE_RESOURCES_SIZE
except Exception:
    SYS_IMAGE_RESOURCES_SIZE = 0x40000


MAGIC = b'HIMG'
VERSION = 1

TYPE_GIF = 1
TYPE_RGB565LE = 2

ENTRY_NAME_LEN = 32
ENTRY_SIZE = 64


def _align_up(v: int, a: int) -> int:
    return (v + (a - 1)) & ~(a - 1)


def _gif_get_canvas_size(data: bytes):
    if len(data) < 10:
        return None
    if data[0:3] != b'GIF':
        return None
    if data[3:6] not in (b'87a', b'89a'):
        return None
    w = data[6] | (data[7] << 8)
    h = data[8] | (data[9] << 8)
    return w, h


_re_dim = re.compile(r'^(?P<name>.+?)[_\\-](?P<w>\\d{1,4})x(?P<h>\\d{1,4})$', re.IGNORECASE)


def _parse_rgb565_name(stem: str):
    m = _re_dim.match(stem)
    if not m:
        return None
    name = m.group('name')
    w = int(m.group('w'))
    h = int(m.group('h'))
    return name, w, h


def _iter_files(root: Path):
    for p in sorted(root.rglob('*')):
        if p.is_file():
            yield p


def build_pack(input_dir: Path, output_file: Path, max_size: int):
    assets = []

    for p in _iter_files(input_dir):
        ext = p.suffix.lower()
        if ext in ('.gif',):
            data = p.read_bytes()
            sz = _gif_get_canvas_size(data)
            if not sz:
                raise ValueError(f"Invalid GIF: {p}")
            w, h = sz
            name = p.stem
            assets.append({
                'name': name,
                'type': TYPE_GIF,
                'width': w,
                'height': h,
                'data': data,
            })
        elif ext in ('.rgb565', '.rgb565le', '.raw'):
            parsed = _parse_rgb565_name(p.stem)
            if not parsed:
                raise ValueError(f"RGB565 file name must contain _WxH or -WxH, e.g. logo_320x172.rgb565: {p}")
            name, w, h = parsed
            data = p.read_bytes()
            if len(data) != w * h * 2:
                raise ValueError(f"RGB565 size mismatch for {p}: expect {w*h*2} bytes, got {len(data)}")
            assets.append({
                'name': name,
                'type': TYPE_RGB565LE,
                'width': w,
                'height': h,
                'data': data,
            })

    if not assets:
        output_file.parent.mkdir(parents=True, exist_ok=True)
        header = struct.pack('<4sHHIIII', MAGIC, VERSION, 0, 0, 0, 0, 0)
        header = header.ljust(64, b'\x00')
        output_file.write_bytes(header)
        return {'count': 0, 'total': len(header)}

    names = set()
    for a in assets:
        n = a['name']
        if len(n.encode('utf-8')) >= ENTRY_NAME_LEN:
            raise ValueError(f"Asset name too long (max {ENTRY_NAME_LEN-1} bytes): {n}")
        if n in names:
            raise ValueError(f"Duplicate asset name: {n}")
        names.add(n)

    header_size = 64
    index_size = _align_up(len(assets) * ENTRY_SIZE, 16)
    data_offset = header_size + index_size
    cur = data_offset

    entries = []
    blobs = []

    for a in assets:
        cur = _align_up(cur, 16)
        offset = cur
        data = a['data']
        size = len(data)
        crc = binascii.crc32(data) & 0xFFFFFFFF
        entries.append({
            'name': a['name'],
            'type': a['type'],
            'offset': offset,
            'size': size,
            'width': a['width'],
            'height': a['height'],
            'crc32': crc,
        })
        blobs.append((offset, data))
        cur += size

    total_size = _align_up(cur, 16)
    if total_size > max_size:
        raise ValueError(f"Packed assets too large: {total_size} > max {max_size}")

    output_file.parent.mkdir(parents=True, exist_ok=True)

    header = struct.pack(
        '<4sHHIIII',
        MAGIC,
        VERSION,
        0,
        total_size,
        len(entries),
        index_size,
        0
    )
    header = header.ljust(header_size, b'\x00')

    index = bytearray()
    for e in entries:
        name_bytes = e['name'].encode('utf-8')
        name_bytes = name_bytes + b'\x00' * (ENTRY_NAME_LEN - len(name_bytes))
        packed = struct.pack(
            '<32sBBHIIHHI',
            name_bytes,
            e['type'],
            0,
            0,
            e['offset'],
            e['size'],
            e['width'],
            e['height'],
            e['crc32']
        )
        index += packed
        if len(packed) < ENTRY_SIZE:
            index += b'\x00' * (ENTRY_SIZE - len(packed))

    if len(index) > index_size:
        raise RuntimeError("Index overflow")
    index += b'\x00' * (index_size - len(index))

    out = bytearray()
    out += header
    out += index

    out_len = len(out)
    for offset, data in blobs:
        if out_len < offset:
            out += b'\x00' * (offset - out_len)
            out_len = len(out)
        out += data
        out_len = len(out)

    if len(out) < total_size:
        out += b'\x00' * (total_size - len(out))

    output_file.write_bytes(out)
    return {'count': len(entries), 'total': total_size}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--input', required=True, help='assets input directory')
    parser.add_argument('--output', required=True, help='output bin file')
    parser.add_argument('--max-size', type=lambda x: int(x, 0), default=SYS_IMAGE_RESOURCES_SIZE)
    args = parser.parse_args()

    input_dir = Path(args.input)
    output_file = Path(args.output)

    if not input_dir.exists():
        raise SystemExit(f"Input dir not found: {input_dir}")

    res = build_pack(input_dir, output_file, args.max_size)
    print(f"Packed {res['count']} assets -> {output_file} ({res['total']} bytes)")


if __name__ == '__main__':
    main()

