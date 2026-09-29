"""A recognisable arcade fightstick unfolds directly into XORA."""
import bpy,sys,math,re,time
from pathlib import Path
from mathutils import Vector,Euler
ROOT=Path(__file__).resolve().parent;sys.path.insert(0,str(ROOT))
import base_scene as base
from mechanics import clip,cylinder,rod,emission
NAME='14-arcade-to-xora'
argv=sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else []
MODE=argv[0] if argv else 'preview'
smooth=base.smooth
TILT=-math.radians(55)
BOARD=Euler((TILT,0,0)).to_matrix()
ORIGIN=Vector((0,.12,0))
def world(v):return ORIGIN+BOARD@Vector(v)

def resample(poly,n=128):
    if sum(poly[i-1][0]*p[1]-p[0]*poly[i-1][1] for i,p in enumerate(poly))<0:poly=list(reversed(poly))
    start=min(range(len(poly)),key=lambda i:poly[i][0]+poly[i][1]);poly=poly[start:]+poly[:start]
    lengths=[math.hypot(poly[(i+1)%len(poly)][0]-p[0],poly[(i+1)%len(poly)][1]-p[1]) for i,p in enumerate(poly)]
    total=sum(lengths);out=[];edge=0;offset=0
    for k in range(n):
        d=total*k/n
        while edge<len(poly)-1 and offset+lengths[edge]<d:offset+=lengths[edge];edge+=1
        t=(d-offset)/max(lengths[edge],1e-8);a=poly[edge];b=poly[(edge+1)%len(poly)]
        out.append((a[0]+t*(b[0]-a[0]),a[1]+t*(b[1]-a[1])))
    return out

def morph_plate(name,poly,material,start_shape,depth=.10):
    cx=(min(x for x,y in poly)+max(x for x,y in poly))/2;cy=(min(y for x,y in poly)+max(y for x,y in poly))/2
    target=resample([(x-cx,y-cy) for x,y in poly]);initial=resample(start_shape)
    ob=base.curve(name,[initial],material,depth=depth,bevel=.019)
    ob.shape_key_add(name='Controller');key=ob.shape_key_add(name='Logo')
    for p,(x,y) in zip(key.data,target):p.co=(x,y,0)
    return ob,key,Vector((cx,cy,0))

def box(name,dimensions,location,material,bevel=.08):
    bpy.ops.mesh.primitive_cube_add(size=1,location=location)
    ob=bpy.context.object;ob.name=name;ob.dimensions=dimensions
    bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
    ob.data.materials.append(material);mod=ob.modifiers.new('Rounded casing','BEVEL');mod.width=bevel;mod.segments=4
    return ob

def build():
    sc,_=base.scene(0);sc.frame_end=120;sc.cycles.samples=48
    for ob in list(bpy.data.objects):
        if ob.name.startswith('XORA original') or ob.name in ['O machined ring','O arcade button','O circular machined detail']:
            bpy.data.objects.remove(ob,do_unlink=True)
    silver=bpy.data.materials.get('Brushed alloy / micro finish')
    black=base.material('Controller graphite chassis',(.018,.026,.038),.55,.32)
    trim=base.material('Controller satin edge',(.09,.14,.19),.8,.25)
    chrome=base.material('Joystick chrome stem',(.45,.5,.57),.94,.18)
    blue=base.material('Arcade blue buttons',(.025,.27,.52),.5,.24)
    glow=emission('Arcade cyan accents',(.025,.35,.7),1.2)
    # An intact, bevelled rectangular fightstick shell is visible for two seconds.
    shell=box('Arcade controller enclosure',(7.5,3.08,.55),world((0,0,-.12)),black,.16);shell.rotation_euler=(TILT,0,0)
    border=box('Aluminium faceplate rim',(7.3,2.88,.07),world((0,0,.18)),trim,.1);border.rotation_euler=(TILT,0,0)
    shell_start=shell.location.copy();border_start=border.location.copy()
    paths=re.findall(r'<path[^>]* d="([^"]+)"',base.SVG.read_text(encoding='utf-8'))
    specs=[]
    tilepositions=[(-2.8,.65),(-1.4,.65),(-2.8,-.65),(-1.4,-.65),(0,.65),(1.4,.65),(0,-.65),(1.4,-.65),(2.8,-.65),(2.8,.65)]
    for j in range(4):specs.append(('X faceplate '+str(j),base.parse(paths[j])))
    rp=base.parse(paths[4])
    for j,(lo,hi) in enumerate([(.11,.70),(.70,1.48),(1.48,2.17)]):specs.append(('R faceplate '+str(j),clip(clip(rp,0,lo,True),0,hi,False)))
    ap=base.parse(paths[5]);low=clip(ap,1,.66,False)
    specs.extend([('A left faceplate',clip(low,0,3.5,False)),('A right faceplate',clip(low,0,3.5,True)),('A crown faceplate',clip(ap,1,.66,True))])
    panels=[];rectangle=[(-.69,-.64),(.69,-.64),(.69,.64),(-.69,.64)]
    for i,(name,poly) in enumerate(specs):
        ob,key,target=morph_plate(name,poly,silver,rectangle,depth=.075)
        x,y=tilepositions[i];start=world((x,y,.29));ob.location=start;ob.rotation_euler=(TILT,0,0)
        panels.append((ob,key,target,start,2.55+i*.09))
    # Eight arcade buttons each become one of the eight segments of the O.
    buttons=[];rims=[]
    for i in range(8):
        col=i%4;row=i//4;x=.12+col*.77;y=(.48 if row==0 else -.34)+[0,.12,.18,.10][col]
        a=i*math.tau/8;b=(i+1)*math.tau/8
        poly=[(-1.16+1.11*math.cos(a+(b-a)*j/24),1.11*math.sin(a+(b-a)*j/24)) for j in range(25)]
        poly += [(-1.16+.91*math.cos(a+(b-a)*j/24),.91*math.sin(a+(b-a)*j/24)) for j in range(24,-1,-1)]
        ob,key,target=morph_plate('Arcade button '+str(i+1),poly,blue,base.ring(0,0,.275),depth=.075)
        start=world((x,y,.44));ob.location=start;ob.rotation_euler=(TILT,0,0)
        buttons.append((ob,key,target,start,2.30+i*.065))
        bpy.ops.mesh.primitive_torus_add(major_radius=.30,minor_radius=.038,major_segments=64,minor_segments=10)
        rim=bpy.context.object;rim.name='Button socket '+str(i+1);rim.data.materials.append(black);rim.location=world((x,y,.36));rim.rotation_euler=(TILT,0,0);rims.append((rim,rim.location.copy()))
    # The joystick is prominent: broad dust washer, chrome shaft and coloured ball.
    joystick_xy=(-2.18,.03)
    socket=cylinder('Joystick dust washer',.5,.065,black,64);socket.location=world((*joystick_xy,.36));socket.rotation_euler=(TILT,0,0)
    shaft=cylinder('Joystick shaft',.085,.90,chrome,32);shaft.location=world((*joystick_xy,.88));shaft.rotation_euler=(TILT,0,0)
    bpy.ops.mesh.primitive_uv_sphere_add(segments=48,ring_count=24,radius=1,location=world((*joystick_xy,1.43)))
    ball=bpy.context.object;ball.name='Joystick ball becomes O centre';ball.scale=(.43,.43,.43);ball.data.materials.append(blue)
    for p in ball.data.polygons:p.use_smooth=True
    ball_start=ball.location.copy();socket_start=socket.location.copy();shaft_start=shaft.location.copy()
    # Small service controls, four screws and feet anchor the initial product identity.
    casing_details=[]
    for i,(x,y) in enumerate([(-3.12,1.2),(-2.78,1.2)]):
        ob=cylinder('Service button '+str(i),.105,.04,black,24);ob.location=world((x,y,.26));ob.rotation_euler=(TILT,0,0);casing_details.append((ob,ob.location.copy()))
    for i,(x,y) in enumerate([(-3.52,1.3),(3.52,1.3),(-3.52,-1.3),(3.52,-1.3)]):
        ob=cylinder('Faceplate screw '+str(i),.055,.025,chrome,16);ob.location=world((x,y,.25));ob.rotation_euler=(TILT,0,0);casing_details.append((ob,ob.location.copy()))
    rail=box('Controller front light strip',(5.8,.025,.03),world((0,-1.5,-.02)),glow,.012);rail.rotation_euler=(TILT,0,0);casing_details.append((rail,rail.location.copy()))
    cam=sc.camera;lamps={name:bpy.data.objects[name] for name in ['Travelling softbox','Gradual frontal reveal','Back rim','Low grazing edge','Opposite rim']}
    allobjects=[shell,border,socket,shaft,ball,cam]+[o for o,*_ in panels]+[o for o,*_ in buttons]+[o for o,_ in rims]+[o for o,_ in casing_details]+list(lamps.values())
    dust=[(o,o.location.copy()) for o in bpy.data.objects if o.name.startswith('Dust ')]
    bluebsdf=blue.node_tree.nodes['Principled BSDF'];glowbsdf=glow.node_tree.nodes['Principled BSDF']
    def pose(f,bake=False):
        sc.frame_set(f+1);t=f/12
        fade=smooth(t/.35)*(1-smooth((t-8.85)/.95))
        change=smooth((t-2.3)/3.7)
        # Plates depart directly from their distributed positions on the controller.
        for i,(ob,key,target,start,delay) in enumerate(panels):
            u=smooth((t-delay)/2.55)
            ob.location=start.lerp(target,u)+Vector((0,.18*math.sin(math.pi*u),.72*math.sin(math.pi*u)))
            ob.rotation_euler=(TILT*(1-u),(1 if i%2 else -1)*1.25*math.sin(math.pi*u),.12*(1 if i%3 else -1)*math.sin(math.pi*u))
            key.value=smooth((u-.18)/.64)
        for i,(ob,key,target,start,delay) in enumerate(buttons):
            u=smooth((t-delay)/2.6)
            ob.location=start.lerp(target,u)+Vector((0,.75*math.sin(math.pi*u),.4*math.sin(math.pi*u)))
            ob.rotation_euler=(TILT*(1-u),.35*math.sin(math.pi*u),math.sin(math.pi*u)*(.8 if i%2 else -.8))
            key.value=smooth((u-.2)/.65)
        # Ball travels continuously from the lever tip to the O's centre.
        u=smooth((t-2.2)/2.85)
        ball.location=ball_start.lerp(Vector((-1.16,0,.15)),u)+Vector((0,.4*math.sin(math.pi*u),.15*math.sin(math.pi*u)))
        ball.scale=(.43+.34*u,.43+.34*u,.43-.24*u)
        retract=smooth((t-2.15)/1.15)
        shaft.location=shaft_start-BOARD@Vector((0,0,.38*retract));shaft.scale=(1,1,max(.001,1-retract))
        socket.location=socket_start-BOARD@Vector((0,0,.20*retract));socket.scale=(max(.001,1-retract),)*3
        # The shell folds back only after its surface panels have lifted away.
        fold=smooth((t-3.0)/2.75)
        for ob,start in [(shell,shell_start),(border,border_start)]+rims+casing_details:
            ob.location=start+Vector((0,-.42*fold,-1.8*fold))
            ob.rotation_euler=(TILT-.7*fold,0,0)
            ob.scale=(1-.92*fold,1-.985*fold,1-.98*fold)
        c0=Vector((.025,.27,.52));c1=Vector((.34,.4,.48));c=c0.lerp(c1,smooth((t-4.0)/1.45));bluebsdf.inputs['Base Color'].default_value=(*c,1)
        bluebsdf.inputs['Metallic'].default_value=.5+.38*smooth((t-4)/1.45)
        glowbsdf.inputs['Emission Strength'].default_value=(1.2+2*math.exp(-((t-2.1)/.18)**2))*fade
        # Opening angle clearly shows the rectangular case, stick and button layout.
        c=smooth((t-2.0)/4.3);cam.location=Vector((3.0,4.6,14.8)).lerp(Vector((0,.8,16.8)),c)
        base.point(cam,(0,.25*(1-c),0));cam.data.dof.focus_distance=cam.location.length;cam.data.dof.aperture_fstop=7
        sweep=smooth((t-5.5)/2.5);x=-5+10*sweep
        keylamp=lamps['Travelling softbox'];keylamp.location=(x,3.5-1.7*c,5);base.point(keylamp,(x*.65,0,0));keylamp.data.energy=850*fade
        lamps['Gradual frontal reveal'].data.energy=(480+50*c)*fade
        lamps['Back rim'].data.energy=750*fade;lamps['Low grazing edge'].data.energy=270*fade;lamps['Opposite rim'].data.energy=280*fade
        sc.world.node_tree.nodes['Background'].inputs[1].default_value=.08*fade
        for i,(ob,pos) in enumerate(dust):ob.location=pos+Vector((.08*math.sin(t*.5+i),t*.02,0))
        if bake:
            for ob in allobjects:
                for field in ['location','rotation_euler','scale']:ob.keyframe_insert(data_path=field,frame=f+1)
            for ob,key,*_ in panels+buttons:key.keyframe_insert(data_path='value',frame=f+1)
            for ob in lamps.values():ob.data.keyframe_insert(data_path='energy',frame=f+1)
            for inp in ['Base Color','Metallic']:bluebsdf.inputs[inp].keyframe_insert(data_path='default_value',frame=f+1)
            glowbsdf.inputs['Emission Strength'].keyframe_insert(data_path='default_value',frame=f+1)
            sc.world.node_tree.nodes['Background'].inputs[1].keyframe_insert(data_path='default_value',frame=f+1)
            cam.data.dof.keyframe_insert(data_path='focus_distance',frame=f+1)
            for ob,_ in dust:ob.keyframe_insert(data_path='location',frame=f+1)
    for f in range(120):pose(f,True)
    return sc,pose

def main():
    sc,pose=build();folder=ROOT/'frames'/NAME;folder.mkdir(parents=True,exist_ok=True)
    frames=[12,24,38,50,64,84] if MODE=='preview' else range(120)
    start=time.monotonic()
    for f in frames:
        pose(f);sc.render.filepath=str(folder/f'{f:03}.png');bpy.ops.render.render(write_still=True)
        print('PROGRESS',f+1,'/120',round(time.monotonic()-start,1),'seconds',flush=True)
    sc.frame_set(85);bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/(NAME+'.blend')))
    print('COMPLETE',MODE,round(time.monotonic()-start,1),'seconds',flush=True)
main()
