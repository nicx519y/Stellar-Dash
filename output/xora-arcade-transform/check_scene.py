import bpy,json
from pathlib import Path
ROOT=Path(__file__).resolve().parent
bpy.ops.wm.open_mainfile(filepath=str(ROOT/'14-arcade-to-xora.blend'))
sc=bpy.context.scene
assert (sc.frame_start,sc.frame_end,sc.render.fps)==(1,120,12)
plates=[o for o in bpy.data.objects if o.name.startswith(('X faceplate ','R faceplate ','A left faceplate','A right faceplate','A crown faceplate'))]
buttons=[o for o in bpy.data.objects if o.name.startswith('Arcade button ')]
assert len(plates)==10 and len(buttons)==8
assert bpy.data.objects.get('Joystick shaft') and bpy.data.objects.get('Arcade controller enclosure')
sc.frame_set(13)
opening={o.name:tuple(o.location) for o in plates+buttons}
assert all(o.data.shape_keys.key_blocks['Logo'].value==0 for o in plates+buttons)
sc.frame_set(25)
assert all(tuple(o.location)==opening[o.name] for o in plates+buttons)
assert all(o.data.shape_keys.key_blocks['Logo'].value==0 for o in plates+buttons)
sc.frame_set(85)
assert all(o.data.shape_keys.key_blocks['Logo'].value==1 for o in plates+buttons)
assert all(max(abs(a) for a in o.rotation_euler)<.001 for o in plates+buttons)
assert all(sum((a-b)**2 for a,b in zip(opening[o.name],o.location))>.01 for o in plates+buttons)
assert abs(bpy.data.objects['Joystick ball becomes O centre'].location.x+1.16)<.001
sc.frame_set(120)
assert all(o.data.energy==0 for o in bpy.data.objects if o.type=='LIGHT')
result={'frames':120,'fps':12,'opening':'complete controller held through 2 seconds','faceplates':10,'buttons':8,'transformation':'same objects animate from controller surfaces to logo','joystick_ball':'ends at O centre','final_lights':'off'}
(ROOT/'scene-validation.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
print('SCENE VERIFIED',result,flush=True)
