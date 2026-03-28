#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import argparse
import binascii
import os
import re
import struct
from pathlib import Path
from typing import Optional, Tuple, List

try:
    from PIL import Image, ImageSequence
except Exception:
    Image = None
    ImageSequence = None

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

try:
    from firmware_metadata import USER_IMAGE_RESOURCES_SIZE
except Exception:
    USER_IMAGE_RESOURCES_SIZE = 0x210000


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


def _clamp_int(v: int, lo: int, hi: int) -> int:
    return max(lo, min(hi, int(v)))


def _img_to_rgb565le_fixed_canvas(img_rgba, out_w: int = 320, out_h: int = 172) -> Tuple[bytes, str]:
    bg = Image.new("RGBA", (out_w, out_h), (0, 0, 0, 255))
    sw, sh = img_rgba.size
    scale = min(1.0, min(out_w / sw, out_h / sh))
    tw = max(1, int(sw * scale))
    th = max(1, int(sh * scale))
    resized = img_rgba.resize((tw, th), resample=Image.Resampling.LANCZOS)
    dx = (out_w - tw) // 2
    dy = (out_h - th) // 2
    bg.alpha_composite(resized, (dx, dy))

    rgb = bg.convert("RGB")
    pixels = rgb.tobytes()  # RGBRGB...
    out = bytearray(out_w * out_h * 2)
    j = 0
    for i in range(0, len(pixels), 3):
        r = pixels[i]
        g = pixels[i + 1]
        b = pixels[i + 2]
        v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
        out[j] = v & 0xFF
        out[j + 1] = (v >> 8) & 0xFF
        j += 2
    return bytes(out), bg.convert("RGB").tobytes()


def _select_frame_indices(frame_times_us: List[int], total_us: int, target_fps: int, max_frames: int) -> List[int]:
    if not frame_times_us:
        return [0]
    frame_count = len(frame_times_us)
    if total_us <= 0:
        return [0]
    interval_us = int(1_000_000 / target_fps)
    target_count = min(max_frames, max(1, int(total_us / interval_us)))
    used = set()
    selected: List[int] = []
    for n in range(target_count):
        target_time = n * interval_us
        best = -1
        best_diff = 10**18
        for i in range(frame_count):
            if i in used:
                continue
            diff = abs(frame_times_us[i] - target_time)
            if diff < best_diff:
                best_diff = diff
                best = i
        if best >= 0:
            used.add(best)
            selected.append(best)
    if not selected:
        selected = [0]
    selected = sorted(set([i for i in selected if 0 <= i < frame_count]))
    last = frame_count - 1
    if len(selected) < max_frames and selected[-1] != last:
        selected.append(last)
        selected = sorted(set(selected))
    return selected


def build_sysbg(sysbg_dir: Path, output_file: Path, target_fps: int, max_frames: int, max_size: int, image_id: str):
    if Image is None:
        raise RuntimeError("Pillow is required to build sysbg. Please install pillow.")

    candidates = []
    for ext in (".png", ".jpg", ".jpeg", ".gif"):
        candidates.extend(sorted(sysbg_dir.glob(f"sysbg{ext}")))
        candidates.extend(sorted(sysbg_dir.glob(f"sysbg{ext.upper()}")))
    if not candidates:
        raise FileNotFoundError(f"sysbg image not found in {sysbg_dir} (expect sysbg.png/jpg/jpeg/gif)")
    src = candidates[0]

    target_fps = _clamp_int(target_fps, 1, 5)
    max_frames = _clamp_int(max_frames, 1, 8)

    img = Image.open(src)
    frames_rgb565: List[bytes] = []

    if src.suffix.lower() != ".gif" or getattr(img, "is_animated", False) is False or getattr(img, "n_frames", 1) <= 1:
        rgba = img.convert("RGBA")
        rgb565, _ = _img_to_rgb565le_fixed_canvas(rgba, 320, 172)
        frames_rgb565 = [rgb565]
        fps = 0
    else:
        canvas_w, canvas_h = img.size
        canvas = Image.new("RGBA", (canvas_w, canvas_h), (0, 0, 0, 0))

        frame_times_us: List[int] = []
        total_us = 0
        durations_ms: List[int] = []
        for i in range(img.n_frames):
            img.seek(i)
            dur = int(img.info.get("duration", 100))
            if dur <= 0:
                dur = 100
            frame_times_us.append(total_us)
            total_us += dur * 1000
            durations_ms.append(dur)
        if total_us <= 0:
            total_us = len(frame_times_us) * 200_000

        avg_fps = len(frame_times_us) / (total_us / 1_000_000)
        if avg_fps > target_fps:
            selected = _select_frame_indices(frame_times_us, total_us, target_fps, max_frames)
        else:
            selected = list(range(min(max_frames, img.n_frames)))
            if len(selected) < max_frames and selected[-1] != img.n_frames - 1:
                selected.append(img.n_frames - 1)
        selected_set = set(selected)

        for i in range(img.n_frames):
            img.seek(i)
            disposal = getattr(img, "disposal_method", 0) or 0
            restore = canvas.copy() if disposal == 3 else None

            # frame bbox via tile
            bbox = None
            try:
                if img.tile and len(img.tile) > 0:
                    bbox = img.tile[0][1]  # (x0,y0,x1,y1)
            except Exception:
                bbox = None
            if not bbox:
                bbox = (0, 0, canvas_w, canvas_h)

            frame_rgba = img.convert("RGBA")
            x0, y0, x1, y1 = bbox
            patch = frame_rgba.crop((x0, y0, x1, y1))
            canvas.alpha_composite(patch, (x0, y0))

            if i in selected_set:
                rgb565, _ = _img_to_rgb565le_fixed_canvas(canvas, 320, 172)
                frames_rgb565.append(rgb565)

            if disposal == 2:
                clear = Image.new("RGBA", (x1 - x0, y1 - y0), (0, 0, 0, 0))
                canvas.alpha_composite(clear, (x0, y0))
            elif disposal == 3 and restore is not None:
                canvas = restore

        fps = target_fps

    frame_count = len(frames_rgb565)
    if frame_count <= 0:
        frame_count = 1
        frames_rgb565 = [b"\x00" * (320 * 172 * 2)]
        fps = 0

    frame_size = 320 * 172 * 2
    total_size = frame_size * frame_count
    header_size = 4096
    file_size = header_size + total_size
    if file_size > max_size:
        raise ValueError(f"sysbg too large: {file_size} > max {max_size}")

    # UserImageIndexHeader v2 (packed)
    magic = 0x474D4955  # 'UIMG'
    version = 2
    valid = 1
    fmt = 2 if frame_count > 1 else 1
    width = 320
    height = 172
    reserved0 = 0
    frames_offset = header_size
    frame_offsets = [(frames_offset + i * frame_size) if i < frame_count else 0 for i in range(10)]
    image_id_bytes = image_id.encode("utf-8")[:15] + b"\x00"
    image_id_bytes = image_id_bytes.ljust(16, b"\x00")

    header = struct.pack(
        "<I H B B H H B B H I I I " + ("I" * 10) + "16s",
        magic,
        version,
        valid,
        fmt,
        width,
        height,
        frame_count,
        fps,
        reserved0,
        frame_size,
        frames_offset,
        total_size,
        *frame_offsets,
        image_id_bytes,
    )
    header = header.ljust(header_size, b"\x00")
    payload = b"".join(frames_rgb565)

    output_file.parent.mkdir(parents=True, exist_ok=True)
    output_file.write_bytes(header + payload)
    return {"frames": frame_count, "fps": fps, "total": len(header) + len(payload)}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--input', help='assets input directory (legacy, packs icons)')
    parser.add_argument('--output', help='output bin file (legacy, icons pack)')
    parser.add_argument('--max-size', type=lambda x: int(x, 0), default=SYS_IMAGE_RESOURCES_SIZE)
    parser.add_argument('--icons-dir', help='icons directory to pack into HIMG')
    parser.add_argument('--icons-output', help='output bin for icons pack')
    parser.add_argument('--icons-max-size', type=lambda x: int(x, 0), default=SYS_IMAGE_RESOURCES_SIZE)
    parser.add_argument('--sysbg-dir', help='sysbg directory containing sysbg.(png|jpg|jpeg|gif)')
    parser.add_argument('--sysbg-output', help='output bin for sysbg (UserImageIndexHeader v2 + frames)')
    parser.add_argument('--sysbg-max-size', type=lambda x: int(x, 0), default=USER_IMAGE_RESOURCES_SIZE)
    parser.add_argument('--sysbg-fps', type=int, default=3)
    parser.add_argument('--sysbg-max-frames', type=int, default=8)
    parser.add_argument('--sysbg-id', default='SYSTEM_DEFAULT')
    args = parser.parse_args()

    icons_dir = Path(args.icons_dir) if args.icons_dir else (Path(args.input) if args.input else None)
    icons_out = Path(args.icons_output) if args.icons_output else (Path(args.output) if args.output else None)

    if icons_dir and icons_out:
        if not icons_dir.exists():
            raise SystemExit(f"Icons dir not found: {icons_dir}")
        res = build_pack(icons_dir, icons_out, int(args.icons_max_size))
        print(f"Packed {res['count']} assets -> {icons_out} ({res['total']} bytes)")

    if args.sysbg_output:
        sysbg_dir = Path(args.sysbg_dir) if args.sysbg_dir else None
        if not sysbg_dir:
            raise SystemExit("--sysbg-dir is required when using --sysbg-output")
        if not sysbg_dir.exists():
            raise SystemExit(f"sysbg dir not found: {sysbg_dir}")
        res2 = build_sysbg(sysbg_dir, Path(args.sysbg_output), int(args.sysbg_fps), int(args.sysbg_max_frames), int(args.sysbg_max_size), str(args.sysbg_id))
        print(f"Packed sysbg frames={res2['frames']} fps={res2['fps']} -> {args.sysbg_output} ({res2['total']} bytes)")


if __name__ == '__main__':
    main()
