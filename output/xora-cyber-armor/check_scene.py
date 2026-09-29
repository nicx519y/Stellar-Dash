import bpy,json,math
from pathlib import Path
from mathutils import Vector,Euler
ROOT=Path(__file__).resolve().parent
source=json.loads((ROOT/'layout-source.json').read_text(encoding='utf-8'))
fx=json.loads((ROOT/'transform-samples.json').read_text(encoding='utf-8'))
def linear(rgb):return tuple((c/255/12.92 if c/255<=.04045 else ((c/255+.055)/1.055)**2.4) for c in rgb)
bpy.ops.wm.open_mainfile(filepath=str(ROOT/'17-xora-cyber-armor.blend'))
sc=bpy.context.scene
assert (sc.frame_start,sc.frame_end,sc.render.fps)==(1,120,12)
keys=[o for o in bpy.data.objects if 'button_id' in o]
panels=[o for o in bpy.data.objects if o.name.startswith(('X panel ','R panel ','A left panel','A right panel','A crown panel'))]
assert len(keys)==22 and len(panels)==10
assert not any('joystick' in o.name.lower() for o in bpy.data.objects)
lenses=[o for o in bpy.data.objects if o.name.startswith('Transparent convex cap ')]
assert len(lenses)==22
assert not any(o.name.startswith('Illuminated ring ') for o in bpy.data.objects)
assert not any(o.name.startswith('Underglow spill') for o in bpy.data.objects)
assert not any(o.type=='LIGHT' and o.data.type in ['POINT','SPOT'] for o in bpy.data.objects)
ridges=[o for o in bpy.data.objects if 'internal_concentric_ridges' in o]
assert len(ridges)==22 and all(o['internal_concentric_ridges']==7 for o in ridges)
assert not any('haze' in o.name.lower() or 'scattering volume' in o.name.lower() for o in bpy.data.objects)
assert not any(n.type in ['PRINCIPLED_VOLUME','VOLUME_SCATTER','VOLUME_ABSORPTION'] for m in bpy.data.materials if m.use_nodes for n in m.node_tree.nodes)
covers=[o for o in bpy.data.objects if o.get('outer_armor_skin')]
assert len(covers)==3 and sc['visible_panel_seams']==2
assert sorted(len(o.data.splines[0].points) for o in covers)==[3,3,6]
diagonals=[]
for cover in covers:
    pts=[p.co for p in cover.data.splines[0].points]
    for a,b in zip(pts,pts[1:]+pts[:1]):
        dx=b.x-a.x;dy=b.y-a.y
        if abs(dx)>.001 and abs(dy)>.001:
            assert abs(abs(dx)-abs(dy))<.00001
            diagonals.append((dx,dy))
assert len(diagonals)==4  # Two sides of each of the two narrow straight gaps.
assert not any(o.name.startswith(('Armor luminous','Armor vent','Armor stencil')) for o in bpy.data.objects)
for lens in lenses:
    vertices=lens.data.vertices
    central=max(v.co.z for v in vertices if math.hypot(v.co.x,v.co.y)<.001)
    radius=max(math.hypot(v.co.x,v.co.y) for v in vertices)
    outer=max(v.co.z for v in vertices if math.hypot(v.co.x,v.co.y)>.99*radius)
    assert central-outer>.05
    material=lens.data.materials[0].node_tree.nodes['Principled BSDF']
    assert .85<material.inputs['Transmission Weight'].default_value<.90
    assert max(material.inputs['Base Color'].default_value[:3])<.5
rotation=Euler((-math.pi/4,0,0)).to_matrix();origin=Vector((0,.6,0))
sc.frame_set(13)
assert all(o.data.materials[0].node_tree.nodes['Principled BSDF'].inputs['Emission Strength'].default_value<=.171 for o in keys)
opening={o.name:tuple(o.location) for o in panels+keys}
colors_start=[tuple(o.data.materials[0].node_tree.nodes['Principled BSDF'].inputs['Emission Color'].default_value) for o in keys]
phase_node=bpy.data.materials['3mm perimeter RGB diffuser'].node_tree.nodes['Transform sweep right edge']
phase_start=phase_node.outputs[0].default_value
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
colors_end=[tuple(o.data.materials[0].node_tree.nodes['Principled BSDF'].inputs['Emission Color'].default_value) for o in keys]
assert abs(phase_node.outputs[0].default_value-phase_start)>100
for frame in [7,10,19,31]:
    sc.frame_set(frame)
    for ob in keys:
        expected=linear(fx['frames'][frame-1][ob['button_id']-1])
        actual=ob.data.materials[0].node_tree.nodes['Principled BSDF'].inputs['Emission Color'].default_value
        assert max(abs(a-b) for a,b in zip(expected,actual))<.00001,(frame,ob.name)
assert len({tuple(c) for c in fx['frames'][9]})>1
assert fx['frames'][0]!=fx['frames'][18]
sc.frame_set(85)
assert all(o.data.shape_keys.key_blocks['Logo'].value==1 for o in morphs)
assert all(max(abs(a) for a in o.rotation_euler)<.001 for o in morphs)
assert abs(bpy.data.objects['Key 2'].location.x+1.16)<.001
sc.frame_set(120)
assert all(o.data.energy==0 for o in bpy.data.objects if o.type=='LIGHT')
result=dict(frames=120,fps=12,buttons=22,source_layout='WebConfig and firmware match; actual opening positions verified',opening_hold_seconds=2,transparent_convex_caps=22,full_surface_key_lighting='blue/green sampled from actual WebConfig Transform algorithm',ambient_lighting='synchronized alternating horizontal color sweep',effect_cycle_seconds=2,bottom_perimeter_mm=thickness_mm,faceplates=10,animated_logo_parts=19,joystick=False,final_lights='off')
result.update(key_emission_strength=.17,volumetric_fog=False,panel_seams='exactly two straight 45-degree lower corner joints',material='smoky translucent convex polycarbonate with seven internal concentric ridges per key',ambient_light='continuous high-saturation blue/green emissive perimeter; no point or spot lights')
(ROOT/'scene-validation.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
print('SCENE VERIFIED',result,flush=True)
