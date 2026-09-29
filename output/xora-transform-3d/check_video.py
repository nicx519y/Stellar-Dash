import bpy,json
from pathlib import Path
ROOT=Path(__file__).resolve().parent
videos=sorted(ROOT.glob('*-master.mp4'))
assert len(videos)==1
results=[]
for path in videos:
    clip=bpy.data.movieclips.load(str(path))
    assert tuple(clip.size)==(640,344),(path,tuple(clip.size))
    assert clip.frame_duration==120,(path,clip.frame_duration)
    results.append({'file':path.name,'size':list(clip.size),'frames':clip.frame_duration,'fps':clip.fps})
    assert abs(clip.fps-12)<.001
    print('VIDEO VERIFIED',results[-1],flush=True)
(ROOT/'video-validation.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
bpy.ops.wm.read_factory_settings(use_empty=True)
sc=bpy.context.scene;sc.render.resolution_x=640;sc.render.resolution_y=344;sc.render.resolution_percentage=100
sc.view_settings.view_transform='Standard';sc.view_settings.look='None'
sc.sequence_editor_create().strips.new_movie('Video visual check',str(videos[0]),channel=1,frame_start=1)
sc.frame_set(89);sc.render.filepath=str(ROOT/'video-check.png');bpy.ops.render.render(write_still=True)

