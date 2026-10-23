# Sarasa Mono SC Webfont

Self-hosted Sarasa Mono SC (更纱黑体 Mono SC), version **1.0.42**, Regular (400)
and Bold (700). English letters/digits occupy one cell; ordinary full-width Han
characters occupy two cells. The existing private-use button icon font is separate.
The website prioritizes its original system monospace families for Latin text and
uses Sarasa for Chinese. The 2:1 ratio describes Sarasa's own glyphs; mixed font
metrics depend on the selected system Latin font.

- Upstream: https://github.com/be5invis/Sarasa-Gothic
- Release: https://github.com/be5invis/Sarasa-Gothic/releases/tag/v1.0.42
- Source archive: `SarasaMonoSC-TTF-1.0.42.7z`
- Archive SHA-256: `aa2150e99eb38c5f9d3a00fe58e3f90a9d89495c795a8ac80934d1c3e6c377ee`
- License: SIL Open Font License 1.1, see [OFL.txt](OFL.txt).

The WOFF2 files are derived from the official hinted TTFs. Each weight is split
into Latin, GB2312 level-one Han, remaining Han, CJK punctuation/full-width
forms, and other symbols. CSS
`unicode-range` lets browsers fetch the required groups. Together the subsets
preserve the source font's entire Unicode cmap and advance widths. `manifest.json`
records the generated file sizes, character counts and SHA-256 hashes. The CSS
uses `font-display: swap`; explicit sans-serif Chinese fallbacks are configured
in `lib/ui-typography.ts`.

To regenerate, install `fonttools[woff]==4.67.0` (Brotli 1.2.0 was used), download
and verify the archive above, extract the two TTFs, then run from this module:

```sh
python scripts/prepare-sarasa-webfont.py path/to/extracted-ttfs
```

The preparation script checks complete character coverage, unchanged glyph
advance widths and the 2:1 Han/Latin width ratio. It does not download fonts or
install packages. Commit the resulting WOFF2, CSS and manifest together; ordinary
builds use the checked-in files and need no external font service.
