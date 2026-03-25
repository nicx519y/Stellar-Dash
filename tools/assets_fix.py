#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import argparse
import re
import struct
from pathlib import Path


_re_dim = re.compile(r'^(?P<name>.+?)[_\-](?P<w>\d{1,4})x(?P<h>\d{1,4})$', re.IGNORECASE)


def _parse_dim_from_stem(stem: str):
    m = _re_dim.match(stem)
    if not m:
        return None
    return m.group('name'), int(m.group('w')), int(m.group('h'))


def _convert_rgb888_to_rgb565le_bytes(rgb_bytes: bytes, w: int, h: int) -> bytes:
    out = bytearray(w * h * 2)
    o = 0
    i = 0
    total = w * h
    for _ in range(total):
        r = rgb_bytes[i + 0]
        g = rgb_bytes[i + 1]
        b = rgb_bytes[i + 2]
        i += 3
        c = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
        out[o + 0] = c & 0xFF
        out[o + 1] = (c >> 8) & 0xFF
        o += 2
    return bytes(out)


def _try_import_pillow():
    try:
        from PIL import Image  # type: ignore
        return Image
    except Exception:
        return None


def _fix_rgb565_endianness(p: Path, inplace: bool) -> bool:
    data = p.read_bytes()
    if len(data) % 2 != 0:
        return False
    swapped = bytearray(len(data))
    swapped[0::2] = data[1::2]
    swapped[1::2] = data[0::2]
    if swapped == data:
        return True
    if inplace:
        p.write_bytes(bytes(swapped))
    return True


def _fit_dims(orig_w: int, orig_h: int, max_w: int, max_h: int) -> tuple[int, int]:
    if orig_w <= max_w and orig_h <= max_h:
        return orig_w, orig_h
    scale_w = max_w / float(orig_w)
    scale_h = max_h / float(orig_h)
    scale = scale_w if scale_w < scale_h else scale_h
    new_w = max(1, int(orig_w * scale))
    new_h = max(1, int(orig_h * scale))
    return new_w, new_h


def _process_image_with_pillow(src: Path, out_dir: Path, resize: str | None, fit: str | None, dither: bool) -> Path:
    Image = _try_import_pillow()
    if Image is None:
        raise RuntimeError("Pillow not available")

    img = Image.open(src)
    img = img.convert("RGB")

    if resize:
        w_s, h_s = resize.lower().split("x", 1)
        tw = int(w_s)
        th = int(h_s)
        img = img.resize((tw, th), Image.BILINEAR)
    elif fit:
        w_s, h_s = fit.lower().split("x", 1)
        max_w = int(w_s)
        max_h = int(h_s)
        ow, oh = img.size
        nw, nh = _fit_dims(ow, oh, max_w, max_h)
        if (nw, nh) != (ow, oh):
            img = img.resize((nw, nh), Image.BILINEAR)

    w, h = img.size
    rgb = img.tobytes()
    raw565 = _convert_rgb888_to_rgb565le_bytes(rgb, w, h)

    name = src.stem
    out_name = f"{name}_{w}x{h}.rgb565"
    out_path = out_dir / out_name
    out_path.write_bytes(raw565)
    return out_path


def _process_gif_with_pillow(src: Path, out_dir: Path, max_colors: int, dither: bool) -> Path:
    Image = _try_import_pillow()
    if Image is None:
        raise RuntimeError("Pillow not available")

    img = Image.open(src)
    frames = []
    durations = []
    disposal = []
    transparency = None

    try:
        transparency = img.info.get("transparency", None)
    except Exception:
        transparency = None

    dither_flag = Image.FLOYDSTEINBERG if dither else Image.NONE

    try:
        i = 0
        while True:
            img.seek(i)
            frame = img.convert("RGBA")
            pal = frame.convert("P", palette=Image.ADAPTIVE, colors=max_colors, dither=dither_flag)
            frames.append(pal)
            durations.append(img.info.get("duration", 10))
            disposal.append(img.disposal_method if hasattr(img, "disposal_method") else 0)
            i += 1
    except EOFError:
        pass

    if not frames:
        raise RuntimeError(f"Invalid GIF: {src}")

    out_path = out_dir / src.name
    save_kwargs = {
        "save_all": True,
        "append_images": frames[1:],
        "duration": durations,
        "loop": 0,
        "optimize": False,
        "disposal": disposal[0] if disposal else 0,
    }
    if transparency is not None:
        save_kwargs["transparency"] = transparency

    frames[0].save(out_path, format="GIF", **save_kwargs)
    return out_path


def main():
    parser = argparse.ArgumentParser(description="assets 校验/修复工具（RGB565LE/GIF）")
    parser.add_argument("--dir", default="application/assets", help="assets 目录（默认 application/assets）")
    parser.add_argument("--out", default="", help="输出目录（默认原地修改/生成）")
    parser.add_argument("--inplace", action="store_true", help="原地修复（仅对 rgb565 字节序修复生效）")
    parser.add_argument("--resize", default="", help="把 png/jpg 等图片转换为 rgb565 并缩放到精确 WxH（需要 Pillow），例如 320x170")
    parser.add_argument("--fit", default="320x170", help="把图片按比例缩小以适配最大 WxH（默认 320x170；小图不放大）")
    parser.add_argument("--gif-colors", type=int, default=256, help="GIF 调色板最大颜色数（需要 Pillow）")
    parser.add_argument("--dither", action="store_true", help="颜色量化启用抖动（需要 Pillow）")
    args = parser.parse_args()

    assets_dir = Path(args.dir)
    if not assets_dir.exists():
        raise SystemExit(f"assets dir not found: {assets_dir}")

    out_dir = Path(args.out) if args.out else assets_dir
    out_dir.mkdir(parents=True, exist_ok=True)

    pillow = _try_import_pillow()
    resize = args.resize.strip() or None
    fit = args.fit.strip() or None

    changed = 0
    warnings = 0

    for p in sorted(assets_dir.rglob("*")):
        if not p.is_file():
            continue

        ext = p.suffix.lower()

        if ext in (".rgb565", ".rgb565le", ".raw"):
            parsed = _parse_dim_from_stem(p.stem)
            if not parsed:
                warnings += 1
                continue
            name, w, h = parsed
            data = p.read_bytes()
            expect = w * h * 2
            if len(data) != expect:
                warnings += 1
                continue
            if args.inplace and ext != ".rgb565le":
                ok = _fix_rgb565_endianness(p, inplace=True)
                if ok:
                    changed += 1
            continue

        if ext == ".gif":
            if pillow is None:
                continue
            try:
                _process_gif_with_pillow(p, out_dir, args.gif_colors, args.dither)
                changed += 1
            except Exception:
                warnings += 1
            continue

        if ext in (".png", ".jpg", ".jpeg", ".bmp", ".tga"):
            if pillow is None:
                continue
            try:
                _process_image_with_pillow(p, out_dir, resize, fit, args.dither)
                changed += 1
            except Exception:
                warnings += 1
            continue

    print(f"done: changed={changed}, warnings={warnings}, out={out_dir}")


if __name__ == "__main__":
    main()
