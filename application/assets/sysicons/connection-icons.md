# XORA connection icons

`wireless.svg` and `USB.svg` describe the sidebar icons at their native 28x28
size. Geometry uses 2px strokes, round caps and an 8x8 coverage grid per output
pixel. Three wireless arcs indicate the selected 2.4G mode, not signal strength.
The wired icon uses the standard USB trident with arrow, circle and square
terminals to remain recognizable at the native screen size.

Regenerate from the repository root with:

```powershell
python tools/generate_connection_icons.py
python tools/generate_connection_icons.py --check
```

The generator exports both SVGs, opaque black/white theme PNGs and RGB565LE files.
The tracked 28x28 RGB565 copies in the parent assets directory stay synchronized.
The resource packer consumes the RGB565 files in this directory; SVG and PNG files
are source/preview assets. Resource names and dimensions remain unchanged.

Normal `assets_fix.py --inplace` processing preserves little-endian raw data.
Byte swapping is only for known big-endian imports via the explicit
`--swap-rgb565-bytes` option; freshly generated PNG outputs are never swapped.

The original 40px sidebar and 32px connection slot remain in use. The visible
strokes have bottom padding to keep clear spacing above the battery icon.
