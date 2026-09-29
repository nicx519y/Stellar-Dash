"""Encode local 640x344 render masters with Blender's bundled FFmpeg."""
import bpy
from pathlib import Path
ROOT=Path(__file__).resolve().parent
NAMES=['13-mechanical-transformation']
for name in NAMES:
    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc=bpy.context.scene;sc.render.resolution_x=640;sc.render.resolution_y=344;sc.render.resolution_percentage=100
    sc.render.fps=12;sc.frame_start=1;sc.frame_end=120
    sc.view_settings.view_transform='Standard';sc.view_settings.look='None'
    ed=sc.sequence_editor_create();folder=ROOT/'frames'/name
    seq=ed.strips.new_image(name,str(folder/'000.png'),channel=1,frame_start=1)
    for i in range(1,120):seq.elements.append(f'{i:03}.png')
    seq.frame_final_duration=120
    sc.render.image_settings.media_type='VIDEO';sc.render.ffmpeg.format='MPEG4';sc.render.ffmpeg.codec='H264'
    sc.render.ffmpeg.constant_rate_factor='HIGH';sc.render.ffmpeg.ffmpeg_preset='GOOD'
    sc.render.filepath=str(ROOT/(name+'-master.mp4'))
    bpy.ops.render.render(animation=True)
    print('EXPORTED',name,flush=True)

