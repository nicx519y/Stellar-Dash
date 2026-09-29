"""Resolve the real WebConfig display layout and cross-check firmware values."""
from pathlib import Path
import re,json,hashlib
ROOT=Path(__file__).resolve().parent;REPO=ROOT.parents[1]
source=REPO/'application/www/lib/device-transport/mock-device-transport.ts'
constants=REPO/'application/www/components/hitbox/hitbox-constants.ts'
firmware=REPO/'application/Inc/system/board_cfg.h'
text=source.read_text(encoding='utf-8');c=constants.read_text(encoding='utf-8');fw=firmware.read_text(encoding='utf-8')
block=text.split('const HITBOX_LAYOUT = [',1)[1].split('] as const',1)[0]
layout=[dict(zip(('x','y','r'),map(float,m))) for m in re.findall(r'x:\s*([\d.]+),\s*y:\s*([\d.]+),\s*r:\s*([\d.]+)',block)]
firmware_block=fw.split('#define HITBOX_ADC_BUTTON_POS_DATA',1)[1].split('#define HITBOX_AMBIENT_POS_DATA',1)[0]
firmware_values=[tuple(map(float,m)) for m in re.findall(r'\{\s*([\d.]+)f,\s*([\d.]+)f,\s*([\d.]+)f\s*\}',firmware_block)]
assert len(layout)==22 and [tuple(item.values()) for item in layout]==firmware_values
def constant(name):return float(re.search(r'\b'+name+r'\s*=\s*([\d.]+)',c)[1])
w=constant('HITBOX_WIDTH');h=constant('HITBOX_HEIGHT');scale=constant('HITBOX_LAYOUT_SCALE')
board_mm=float(re.search(r'#define BOARD_WIDTH\s+([\d.]+)f',fw)[1])
for i,item in enumerate(layout):
    item.update(id=i+1,label='Fn' if i==21 else str(i+1),display_x=item['x']*scale,display_y=item['y']*scale,display_radius=item['r'])
result=dict(width=w,height=h,coordinate_scale=scale,board_width_mm=board_mm,ambient_thickness_mm=3,buttons=layout,
    note='WebConfig scales x/y by 2.55 and leaves r unscaled; use these actual display proportions.',
    sources={str(p.relative_to(REPO)).replace('\\','/'):hashlib.sha256(p.read_bytes()).hexdigest() for p in [source,constants,firmware]})
(ROOT/'layout-source.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
print('LAYOUT VERIFIED: 22 controls, firmware matches WebConfig; canvas',w,h,'scale',scale,'board width',board_mm,'mm')
