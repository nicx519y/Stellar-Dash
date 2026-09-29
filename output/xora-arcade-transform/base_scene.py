"""Local Blender/Cycles production scene; authentic XORA SVG outlines."""
import bpy, math, re, sys, json, random, time
from pathlib import Path
from mathutils import Vector, Matrix
ROOT=Path(__file__).resolve().parent
SVG=ROOT.parents[1]/'application/www/public/images/xora-mono-slate.svg'
NAMES=['09-titanium-cinema','10-obsidian-noir','11-ion-reactor','12-gold-studio']
argv=sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else []
MODE=argv[0] if argv else 'preview'
ONLY=int(argv[1]) if len(argv)>1 else None
def clamp(x):return max(0,min(1,x))
def smooth(x):x=clamp(x);return x*x*(3-2*x)
def point(obj,target):
    direction=(Vector(target)-obj.location).normalized()
    right=direction.cross(Vector((0,1,0))).normalized()
    up=right.cross(direction).normalized()
    obj.rotation_euler=Matrix((right,up,-direction)).transposed().to_euler()
def parse(d):
    ts=re.findall(r'[A-Za-z]|-?\d+(?:\.\d+)?',d);i=0;cmd='';p=(0,0);pts=[]
    def take(n):
        nonlocal i
        a=list(map(float,ts[i:i+n]));i+=n;return a
    while i<len(ts):
        if ts[i].isalpha():cmd=ts[i];i+=1
        if cmd=='Z':break
        if cmd in ('M','L'):
            p=tuple(take(2));pts.append(p)
            if cmd=='M':cmd='L'
        elif cmd=='H':p=(take(1)[0],p[1]);pts.append(p)
        elif cmd=='V':p=(p[0],take(1)[0]);pts.append(p)
        elif cmd in ('C','Q'):
            a=take(6 if cmd=='C' else 4);old=p
            for step in range(1,21):
                t=step/20;v=1-t
                if cmd=='C':pts.append(tuple(v**3*old[j]+3*v*v*t*a[j]+3*v*t*t*a[j+2]+t**3*a[j+4] for j in range(2)))
                else:pts.append(tuple(v*v*old[j]+2*v*t*a[j]+t*t*a[j+2] for j in range(2)))
            p=pts[-1]
        else:raise ValueError(cmd)
    return [((x-500)/100,(120-y)/100) for x,y in pts]
def ring(cx,cy,r,reverse=False):
    return [(cx+r*math.cos(i*math.tau/128),cy+r*math.sin(i*math.tau/128)) for i in (range(127,-1,-1) if reverse else range(128))]
def curve(name,loops,mat,depth=.17,bevel=.028):
    c=bpy.data.curves.new(name,'CURVE');c.dimensions='2D';c.resolution_u=12;c.fill_mode='BOTH';c.extrude=depth;c.bevel_depth=bevel;c.bevel_resolution=3
    for pts in loops:
        sp=c.splines.new('POLY');sp.points.add(len(pts)-1)
        for v,(x,y) in zip(sp.points,pts):v.co=(x,y,0,1)
        sp.use_cyclic_u=True
    ob=bpy.data.objects.new(name,c);bpy.context.collection.objects.link(ob);ob.data.materials.append(mat);return ob
def material(name,color,metal,rough):
    m=bpy.data.materials.new(name);m.use_nodes=True;n=m.node_tree.nodes;l=m.node_tree.links
    p=n.get('Principled BSDF');p.inputs['Base Color'].default_value=(*color,1);p.inputs['Metallic'].default_value=metal;p.inputs['Roughness'].default_value=rough
    tex=n.new('ShaderNodeTexNoise');tex.inputs['Scale'].default_value=165;tex.inputs['Detail'].default_value=2
    coord=n.new('ShaderNodeTexCoord');scale=n.new('ShaderNodeVectorMath');scale.operation='MULTIPLY';scale.inputs[1].default_value=(1,13,2)
    l.new(coord.outputs['Generated'],scale.inputs[0]);l.new(scale.outputs[0],tex.inputs['Vector'])
    bump=n.new('ShaderNodeBump');bump.inputs['Strength'].default_value=.12;bump.inputs['Distance'].default_value=.017;l.new(tex.outputs['Fac'],bump.inputs['Height']);l.new(bump.outputs[0],p.inputs['Normal'])
    return m
def light(name,loc,target,color,power,size,sy=None,kind='AREA'):
    data=bpy.data.lights.new(name,kind);data.energy=power;data.color=color
    if kind=='AREA':
        data.shape='RECTANGLE';data.size=size;data.size_y=sy if sy else size
    ob=bpy.data.objects.new(name,data);bpy.context.collection.objects.link(ob);ob.location=loc;point(ob,target);return ob
def scene(k):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc=bpy.context.scene;sc.render.engine='CYCLES';sc.cycles.samples=32;sc.cycles.use_denoising=True
    prefs=bpy.context.preferences.addons['cycles'].preferences;prefs.compute_device_type='OPTIX';prefs.get_devices()
    for device in prefs.devices:device.use=device.type=='OPTIX'
    sc.cycles.device='GPU';sc.cycles.max_bounces=5;sc.cycles.volume_bounces=1
    sc.render.resolution_x=640;sc.render.resolution_y=344;sc.render.resolution_percentage=100
    sc.render.image_settings.file_format='PNG';sc.render.image_settings.color_mode='RGB';sc.render.fps=12;sc.frame_end=96
    sc.render.film_transparent=False;sc.render.use_persistent_data=True
    sc.view_settings.view_transform='AgX';sc.view_settings.look='AgX - Medium High Contrast';sc.view_settings.exposure=.6
    world=bpy.data.worlds.new('Black studio');world.use_nodes=True;world.node_tree.nodes['Background'].inputs[0].default_value=(.012,.017,.025,1);world.node_tree.nodes['Background'].inputs[1].default_value=.15;sc.world=world
    base=[(.34,.40,.48),(.08,.09,.12),(.12,.23,.29),(.55,.34,.13)][k]
    mat=material('Brushed alloy / micro finish',base,.88,[.26,.17,.25,.27][k])
    shapes=re.findall(r'<path[^>]* d="([^"]+)"',SVG.read_text(encoding='utf-8'))
    logo=[]
    for j,d in enumerate(shapes):logo.append(curve('XORA original outline '+str(j),[parse(d)],mat,depth=[.16,.24,.19,.15][k]))
    logo.append(curve('O machined ring',[ring(-1.16,0,1.11),ring(-1.16,0,.91,True)],mat))
    logo.append(curve('O arcade button',[ring(-1.16,0,.77)],mat))
    # A polished inlay around the existing ring adds depth without changing its contour.
    bpy.ops.mesh.primitive_torus_add(major_radius=1.01,minor_radius=.008,major_segments=128,minor_segments=8,location=(-1.16,0,.203))
    bpy.context.object.name='O circular machined detail';bpy.context.object.data.materials.append(mat)
    floor=material('Black reflective studio',(.002,.003,.005),.08,.5)
    floor.node_tree.nodes['Principled BSDF'].inputs['Specular IOR Level'].default_value=.055
    bpy.ops.mesh.primitive_plane_add(size=200,location=(0,-1.44,0),rotation=(math.pi/2,0,0));bpy.context.object.name='Dark reflection floor';bpy.context.object.data.materials.append(floor)
    # Sparse three-dimensional suspended dust; lit by the same moving lamps.
    dustmat=material('Floating metal dust',(.22,.26,.32),.65,.42)
    random.seed(91+k)
    dust=[]
    for j in range(55):
        bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=1,radius=random.uniform(.006,.020),location=(random.uniform(-8,8),random.uniform(-1.2,3.2),random.uniform(-2,4)))
        o=bpy.context.object;o.name='Dust '+str(j);o.data.materials.append(dustmat);dust.append((o,o.location.copy()))
    # Low-density procedural volume instead of flat smoke sprites.
    bpy.ops.mesh.primitive_cube_add(size=1,location=(0,1,-1));fog=bpy.context.object;fog.name='Spatial haze';fog.scale=(20,8,12)
    vm=bpy.data.materials.new('Soft procedural atmosphere');vm.use_nodes=True;n=vm.node_tree.nodes;n.clear();l=vm.node_tree.links
    out=n.new('ShaderNodeOutputMaterial');v=n.new('ShaderNodeVolumePrincipled');v.inputs['Color'].default_value=(.55,.62,.7,1);v.inputs['Anisotropy'].default_value=.3
    noise=n.new('ShaderNodeTexNoise');noise.inputs['Scale'].default_value=1.3;noise.inputs['Detail'].default_value=3
    density=n.new('ShaderNodeMath');density.operation='MULTIPLY';density.inputs[1].default_value=[.012,.007,.017,.01][k]
    l.new(noise.outputs['Fac'],density.inputs[0]);l.new(density.outputs[0],v.inputs['Density']);l.new(v.outputs['Volume'],out.inputs['Volume']);fog.data.materials.append(vm)
    bpy.ops.object.camera_add(location=(.8,1,17));cam=bpy.context.object;cam.name='Cinema camera';cam.data.lens=43;sc.camera=cam
    cam.data.lens=46;cam.data.dof.use_dof=True;cam.data.dof.focus_distance=17;cam.data.dof.aperture_fstop=5.6
    palettes=[((.52,.76,1),(.18,.47,1)),((1,.7,.35),(.18,.32,.55)),((.2,.82,1),(.25,.16,1)),((1,.78,.48),(.58,.69,1))]
    a,b=palettes[k]
    key=light('Travelling softbox',(-7,1.5,4),(-4,0,0),a,1000,.65,5)
    fill=light('Gradual frontal reveal',(0,2,7),(0,0,0),(1,.96,.88),0,9,4)
    rim=light('Back rim',(0,2,-1.5),(0,0,0),b,550,7,.4)
    side=light('Low grazing edge',(-7,.2,.9),(0,0,0),a,320,.4,3)
    warm=light('Opposite rim',(7,2,-.3),(1,0,0),a,280,.4,3)
    # Sensor bloom is composited from actual highlights, not animated masks.
    tree=bpy.data.node_groups.new('Optical highlight bloom','CompositorNodeTree')
    sc.compositing_node_group=tree
    tree.interface.new_socket(name='Image',in_out='OUTPUT',socket_type='NodeSocketColor')
    inp=tree.nodes.new('CompositorNodeRLayers');glare=tree.nodes.new('CompositorNodeGlare');glare.inputs['Type'].default_value='Fog Glow';glare.inputs['Quality'].default_value='High';glare.inputs['Threshold'].default_value=1.1
    output=tree.nodes.new('NodeGroupOutput');tree.links.new(inp.outputs['Image'],glare.inputs['Image']);tree.links.new(glare.outputs['Image'],output.inputs['Image'])
    def at(f):
        t=f/12;p=smooth((t-.25)/5);fade=smooth(t/.55)*(1-smooth((t-6.8)/1.05))
        # Slow sideways/forward camera drift reveals genuine parallax and bevels.
        z=16.8
        start=[(2.2,.7,14.5),(-2.6,.35,11.5),(1.4,1.4,15.5),(2.6,1.7,15.5)][k]
        cam.location=(start[0]*(1-p),start[1]*(1-p)+.8*p,start[2]*(1-p)+z*p)
        point(cam,(0,0,0));cam.data.dof.focus_distance=cam.location.length
        cam.data.dof.aperture_fstop=3.2+3.8*p
        x=-8+16*smooth((t-.2)/4.9)
        key.location=(x,[1.4,-.2,2.5,1.7][k],3.4);point(key,(x*.72,0,0));key.data.energy=[1250,2100,1300,1450][k]*fade
        fill.data.energy=[500,1150,520,600][k]*smooth((t-2.7)/2.3)*fade
        rim.data.energy=[750,450,1500,530][k]*(.25+.75*smooth(t/2))*fade
        side.data.energy=360*(1-.7*p)*fade;warm.data.energy=400*smooth((t-1)/2)*fade
        sc.world.node_tree.nodes['Background'].inputs[1].default_value=.08*fade
        for j,(o,pos) in enumerate(dust):o.location=pos+Vector((.08*math.sin(t*.6+j),t*.02,0))
        sc.frame_set(f+1)
    return sc,at
def main():
    for k in range(4) if ONLY is None else [ONLY]:
        sc,at=scene(k);folder=ROOT/'frames'/NAMES[k];folder.mkdir(parents=True,exist_ok=True)
        if MODE=='preview':frames=[20,44,66]
        else:frames=range(96)
        start=time.monotonic()
        for f in frames:
            at(f);sc.render.filepath=str(folder/(f'{f:03}.png'));bpy.ops.render.render(write_still=True)
            print('PROGRESS',NAMES[k],f+1,'/96',round(time.monotonic()-start,1),'seconds',flush=True)
        at(66);bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/(NAMES[k]+'.blend')))
        print('COMPLETE',NAMES[k],round(time.monotonic()-start,1),'seconds',flush=True)
if __name__ == "__main__": main()
