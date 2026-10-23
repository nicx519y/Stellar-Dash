# XORA Screen Bitmap

The firmware uses a generated ASCII subset of Terminus 4.49.1, named **XORA
Screen Bitmap**. The original BDF files and copyright/license notice are retained
unchanged. Font software is licensed under SIL OFL 1.1; see [OFL.TXT](OFL.TXT).

Upstream: https://terminus-font.sourceforge.net/

Source archive: https://downloads.sourceforge.net/project/terminus-font/terminus-font-4.49/terminus-font-4.49.1.tar.gz

| Source | Native cell | Use |
|---|---|---|
| ter-u12n.bdf | 6x12 | Sidebars and compact hints |
| ter-u14n.bdf | 8x14 | Description and popup body |
| ter-u16b.bdf | 8x16 bold | Titles, menus and values |
| ter-u24b.bdf | 12x24 bold | Sleep-charge digits |

Run `python tools/generate_screen_fonts.py` from the repository root to regenerate
the checked-in `screen_font_data.inc`; `--check` verifies it without writes.
The generator records source SHA-256 values. Only ASCII 32 through 126 is emitted;
the firmware retains its existing unsupported-character fallback to `?`.

All drawing and text measurements use `screen_font.h`. Existing scale arguments
are compatible font selectors, not numeric multipliers. Each size is drawn directly
from its own bitmap, with no runtime resampling or font allocation.

Platform badges and Profile labels use `SCREEN_FONT_BADGE_BOLD`: the 8x14 body bitmap gains one
foreground pixel to the right and below each stroke (a 2x2 footprint), clipped
within the same cell. The counters in P and B remain open.
This improves reversed text contrast without changing normal body text or
adding another font table. Badge dimensions remain 28x14 pixels.

The 8,552-byte aligned table section is copied to DTCM during startup, before any
screen rendering. It stays readable while QSPI is busy or during STOP recovery.
The heap follows this section; the MSP location and its reserve are unchanged.
