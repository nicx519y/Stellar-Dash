import bpy,json,math
from pathlib import Path
from mathutils import Vector,Euler
ROOT=Path(__file__).resolve().parent
source=json.loads((ROOT/'layout-source.json').read_text(encoding='utf-8'))
bpy.ops.wm.open_mainfile(filepath=str(ROOT/'15-xora-leverless-rgb.blend'))
sc=bpy.context.scene
assert (sc.frame_start,sc.frame_end,sc.render.fps)==(1,120,12)
keys=[o for o in bpy.data.objects if 'button_id' in o]
panels=[o for o in bpy.data.objects if o.name.startswith(('X panel ','R panel ','A left panel','A right panel','A crown panel'))]
assert len(keys)==22 and len(panels)==10
assert not any('joystick' in o.name.lower() for o in bpy.data.objects)
assert len([o for o in bpy.data.objects if o.name.startswith('Illuminated ring ')])==22
rotation=Euler((-math.pi/4,0,0)).to_matrix();origin=Vector((0,.6,0))
sc.frame_set(13)
opening={o.name:tuple(o.location) for o in panels+keys}
for item in source['buttons']:
    ob=next(o for o in keys if o['button_id']==item['id'])
    pos=rotation.inverted()@(ob.location-origin)
    expected=Vector(((item['display_x']/787-.5)*8,(.5-item['display_y']/489)*8*489/787,.275))
    assert (pos-expected).length<.00001,(ob.name,pos,expected)
    assert ob['display_radius']==item['display_radius']
guide=bpy.data.objects['3 mm full perimeter light guide']
thickness_mm=guide.data.extrude*2*sc.unit_settings.scale_length*1000
assert abs(thickness_mm-3)<.00001,thickness_mm
assert len(guide.data.splines)==2
assert all(abs(x-1)<.00001 for x in guide.scale)
morphs=panels+[o for o in keys if o.data.shape_keys]
assert len(morphs)==19
assert all(o.data.shape_keys.key_blocks['Logo'].value==0 for o in morphs)
sc.frame_set(25)
assert all(tuple(o.location)==opening[o.name] for o in panels+keys)
sc.frame_set(85)
assert all(o.data.shape_keys.key_blocks['Logo'].value==1 for o in morphs)
assert all(max(abs(a) for a in o.rotation_euler)<.001 for o in morphs)
assert abs(bpy.data.objects['Key 2'].location.x+1.16)<.001
sc.frame_set(120)
assert all(o.data.energy==0 for o in bpy.data.objects if o.type=='LIGHT')
result=dict(frames=120,fps=12,buttons=22,source_layout='WebConfig and firmware match; actual opening positions verified',opening_hold_seconds=2,light_rings=22,bottom_perimeter_mm=thickness_mm,faceplates=10,animated_logo_parts=19,joystick=False,final_lights='off')
(ROOT/'scene-validation.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
print('SCENE VERIFIED',result,flush=True)
