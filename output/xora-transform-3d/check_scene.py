"""Verify the delivered .blend contains the actual playable transformation."""
import bpy,json
from pathlib import Path
ROOT=Path(__file__).resolve().parent
bpy.ops.wm.open_mainfile(filepath=str(ROOT/'13-mechanical-transformation.blend'))
sc=bpy.context.scene
assert sc.frame_start==1 and sc.frame_end==120 and sc.render.fps==12
panels=[o for o in bpy.data.objects if o.name.startswith(('X blade ','R sliding plate ','A left wing','A right wing','A crown','O locking sector '))]
assert len(panels)==18
assert all(o.animation_data and o.animation_data.action for o in panels)
sc.frame_set(1)
folded={o.name:tuple(o.location) for o in panels}
sc.frame_set(73)
assert all(max(abs(a) for a in o.rotation_euler)<.001 for o in panels)
assert all(sum((a-b)**2 for a,b in zip(folded[o.name],o.location))>.01 for o in panels)
sc.frame_set(120)
assert all(o.data.energy==0 for o in bpy.data.objects if o.type=='LIGHT')
result={'scene':'13-mechanical-transformation.blend','frames':120,'fps':12,'articulated_panels':18,'animation_actions':'present','fold_to_logo':'verified','final_lights':'off'}
(ROOT/'scene-validation.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
print('SCENE VERIFIED',result,flush=True)
