# XORA / 星溯

Selected design: A (star-cut X). WebConfig uses the soft slate monochrome version.

- `xora-champagne.svg`: editable vector master, transparent background, outlined lettering with no font dependency. Named groups separate the X, arcade button O, and R/A. Gradients are in `<defs>`.
- `xora-champagne.png`: transparent 2000 × 480 export of the vector master.
- `colorway-reference.png`: original generated color comparison; selected option 04 is at bottom right. The SVG is a vector reconstruction of that selected concept.
- `xora-mono-{pearl,slate,green,gold}.svg`: editable single-color variants. The artwork and button O use one solid fill per variant.
- `xora-monochrome-webconfig-options.png`: the four variants shown against the actual WebConfig header.
- WebConfig serves a copy of `xora-mono-slate.svg` from `application/www/public/images/`. Its 165 × 44 px image box uses centered `scale(2/3)`, giving equal horizontal and vertical safe areas within that box.

After editing the master, sync the web asset from the repository root:

```powershell
Copy-Item -LiteralPath assets/branding/xora/xora-mono-slate.svg -Destination application/www/public/images/xora-mono-slate.svg
```

Regenerate the transparent PNG from `application/www` using its installed Sharp dependency:

```powershell
node --input-type=module -e "import sharp from 'sharp'; await sharp('../../assets/branding/xora/xora-champagne.svg').resize(2000,480).png().toFile('../../assets/branding/xora/xora-champagne.png');"
```
