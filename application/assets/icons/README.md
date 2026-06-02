# Status bar icons

These icons are optional. If an icon is missing, firmware falls back to text.

Recommended size: 24x24 px.

Use `#FF00FF` magenta for transparent pixels. The status bar renderer skips
that color when drawing these icons.

Required asset names:

- `status_pc.png` -> PC / XInput mode
- `status_ns.png` -> Nintendo Switch mode
- `status_xbox.png` -> Xbox mode
- `status_ps.png` -> PS4 / PS5 mode
- `status_usb.png` -> USB connection mode
- `status_rf24g.png` -> 2.4G wireless connection mode

Build and flash:

```powershell
python tools/hbox.py build assets
python tools/hbox.py flash assets
```

The build step converts PNG/JPG/BMP images into RGB565 assets and packs them into
`application/build/system_assets.bin`.
