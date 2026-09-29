"""Articulated XORA mechanical transformation, animated and rendered locally."""
import bpy,sys,math,random,re,time
from pathlib import Path
from mathutils import Vector
ROOT=Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT))
import base_scene as base
NAME='13-mechanical-transformation'
argv=sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else []
MODE=argv[0] if argv else 'preview'
smooth=base.smooth

def clip(poly,axis,bound,keep_greater):
    out=[]
    def inside(p):return p[axis]>=bound if keep_greater else p[axis]<=bound
    for i,p in enumerate(poly):
        a=poly[i-1];ai=inside(a);pi=inside(p)
        if ai!=pi:
            t=(bound-a[axis])/(p[axis]-a[axis]);out.append(tuple(a[j]+t*(p[j]-a[j]) for j in range(2)))
        if pi:out.append(p)
    return out

def cylinder(name,radius,depth,mat,vertices=16):
    bpy.ops.mesh.primitive_cylinder_add(vertices=vertices,radius=radius,depth=depth)
    ob=bpy.context.object;ob.name=name;ob.data.materials.append(mat)
    bevel=ob.modifiers.new('Machined edge','BEVEL');bevel.width=.012;bevel.segments=2
    return ob

def rod(ob,a,b,r):
    a=Vector(a);b=Vector(b);ob.location=(a+b)/2
    d=b-a;ob.rotation_euler=d.to_track_quat('Z','Y').to_euler();ob.scale=(r,r,max(.005,d.length))

def emission(name,color,strength):
    m=bpy.data.materials.new(name);m.use_nodes=True;p=m.node_tree.nodes.get('Principled BSDF')
    p.inputs['Base Color'].default_value=(*color,1);p.inputs['Metallic'].default_value=.65;p.inputs['Roughness'].default_value=.3
    p.inputs['Emission Color'].default_value=(*color,1);p.inputs['Emission Strength'].default_value=strength
    return m

def build():
    sc,_=base.scene(0);sc.frame_end=120;sc.cycles.samples=48
    # Replace only the original logo meshes in this new scene; reuse its proven studio.
    for ob in list(bpy.data.objects):
        if ob.name.startswith('XORA original') or ob.name in ['O machined ring','O arcade button','O circular machined detail']:
            bpy.data.objects.remove(ob,do_unlink=True)
    silver=bpy.data.materials.get('Brushed alloy / micro finish')
    graphite=base.material('Graphite motor housings',(.025,.035,.052),.82,.3)
    chrome=base.material('Machined piston steel',(.28,.34,.4),.96,.18)
    glow=emission('Core cyan emission',(.04,.42,1),2)
    paths=re.findall(r'<path[^>]* d="([^"]+)"',base.SVG.read_text(encoding='utf-8'))
    specs=[]
    # Four original X armour blades, individually hinged.
    for j in range(4):specs.append(('X blade '+str(j),base.parse(paths[j]),1.15+j*.16,'X'))
    # The R's stem, bowl and diagonal foot are independent folded armour plates.
    rp=base.parse(paths[4])
    for j,(lo,hi) in enumerate([(.11,.70),(.70,1.48),(1.48,2.17)]):
        specs.append(('R sliding plate '+str(j),clip(clip(rp,0,lo,True),0,hi,False),1.7+j*.18,'R'))
    ap=base.parse(paths[5]);low=clip(ap,1,.66,False)
    specs += [('A left wing',clip(low,0,3.5,False),2.10,'A'),('A right wing',clip(low,0,3.5,True),2.30,'A'),('A crown',clip(ap,1,.66,True),2.50,'A')]
    # The O outer ring is eight solid interlocking sectors, not flat sprites.
    for j in range(8):
        a=j*math.tau/8;b=(j+1)*math.tau/8
        poly=[(-1.16+1.11*math.cos(a+(b-a)*i/18),1.11*math.sin(a+(b-a)*i/18)) for i in range(19)]
        poly += [(-1.16+.91*math.cos(a+(b-a)*i/18),.91*math.sin(a+(b-a)*i/18)) for i in range(18,-1,-1)]
        specs.append(('O locking sector '+str(j),poly,.7+j*.06,'O'))
    panels=[]
    for i,(name,poly,delay,letter) in enumerate(specs):
        # Shared split boundaries meet flush; a tiny clearance keeps the seams readable.
        cx=(min(x for x,y in poly)+max(x for x,y in poly))/2
        cy=(min(y for x,y in poly)+max(y for x,y in poly))/2
        ob=base.curve(name,[[(x-cx,y-cy) for x,y in poly]],silver,depth=.17,bevel=.018)
        target=Vector((cx,cy,0));ob.location=target
        ang=i*2.39996
        if letter=='X':start=Vector((-1.35+.3*(i%2),(.38 if i<2 else -.38),.25+.16*(i%2)))
        elif letter=='R':start=Vector((.20+(i%3)*.22,(i%3-1)*.25,.4+(i%3)*.15))
        elif letter=='A':start=Vector((1.0+(i%2)*.3,(i%3-1)*.34,.15+(i%2)*.18))
        else:start=Vector((math.cos(ang)*.48,math.sin(ang)*.48,-.2))
        rotation=Vector(((.6 if i%2 else -.55),(1.35 if i%2 else -1.35),(.55 if i%3 else -.65)))
        if letter=='O':rotation=Vector((math.pi/2,0,math.pi*.8+i*.25))
        # Hinges travel behind the armour, attached to the same transformation.
        hinge=cylinder('Hinge '+name,.072,.20,chrome)
        housing=cylinder('Sleeve '+name,1,1,graphite)
        shaft=cylinder('Piston '+name,1,1,chrome)
        panels.append(dict(ob=ob,target=target,start=start,rotation=rotation,delay=delay,letter=letter,hinge=hinge,housing=housing,shaft=shaft,i=i))
    # Actual concentric core mechanism behind the O's original face.
    core=base.curve('O central button',[base.ring(0,0,.77)],silver,depth=.18,bevel=.026)
    gears=[]
    for idx,r in enumerate([.81,.69,.53]):
        bpy.ops.mesh.primitive_torus_add(major_radius=r,minor_radius=.027 if idx==0 else .018,major_segments=96,minor_segments=10)
        ob=bpy.context.object;ob.name='Core gyroscope '+str(idx);ob.data.materials.append(glow if idx==0 else chrome);gears.append(ob)
    # Slim mechanical spine extends beneath the moving plates, retracting behind the logo.
    spine=[]
    for i in range(12):
        bpy.ops.mesh.primitive_cube_add(size=1);ob=bpy.context.object;ob.name='Telescopic chassis '+str(i)
        ob.data.materials.append(graphite);bev=ob.modifiers.new('Beveled armour','BEVEL');bev.width=.06;bev.segments=2;spine.append(ob)
    corelight=base.light('Core ignition',(0,0,1.2),(0,0,-.5),(.06,.5,1),0,1,1,kind='POINT')
    corelight.data.shadow_soft_size=.3
    cam=sc.camera
    lamps={name:bpy.data.objects[name] for name in ['Travelling softbox','Gradual frontal reveal','Back rim','Low grazing edge','Opposite rim']}
    dust=[(o,o.location.copy()) for o in bpy.data.objects if o.name.startswith('Dust ')]
    animated=[cam,core,*gears,*spine,corelight]+list(lamps.values())
    for p in panels:animated.extend([p['ob'],p['hinge'],p['housing'],p['shaft']])
    def pose(f,key=False):
        sc.frame_set(f+1);t=f/12
        fade=smooth(t/.5)*(1-smooth((t-8.65)/1.15))
        spread=smooth((t-.8)/3.2)
        corepos=Vector((-1.16*smooth((t-.7)/2.2),0,.08))
        for p in panels:
            u=smooth((t-p['delay'])/2.65)
            # A telescoping movement leads the hinge unfold, giving weight and causality.
            slide=smooth((t-p['delay'])/1.8)
            turn=smooth((t-p['delay']-.45)/2.2)
            ob=p['ob'];ob.location=p['start'].lerp(p['target'],slide)
            ob.location.z+=.3*math.sin(math.pi*slide)
            ob.rotation_euler=p['rotation']*(1-turn)
            if p['letter']=='O':ob.rotation_euler.z+=(1-turn)*math.tau*.45
            # Short damped settling after each panel reaches its mechanical stop.
            lock=t-p['delay']-2.65
            if 0<lock<.45:ob.location.z+=.038*math.sin(lock*26)*math.exp(-lock*9)
            hinge=p['hinge'];hinge.location=ob.location+Vector((0,0,-.25));hinge.rotation_euler=ob.rotation_euler
            conceal=1-smooth((t-4.85)/.55)
            hinge.scale=(max(.001,conceal),)*3
            a=corepos+Vector((0,0,-.38));b=ob.location+Vector((0,0,-.27))
            mid=a.lerp(b,.55)
            rod(p['housing'],a,mid,.045*max(.001,conceal))
            rod(p['shaft'],mid,b,.022*max(.001,conceal))
        core.location=corepos
        core.rotation_euler=(.95*(1-smooth((t-.45)/2.8)),0,(1-smooth((t-.3)/3.1))*math.tau)
        for idx,ob in enumerate(gears):
            ob.location=corepos+Vector((0,0,-.1 if idx else .02))
            ob.rotation_euler=(math.sin(t*2+idx)*(1-spread)*1.2,math.cos(t*1.7+idx)*(1-spread),t*(1 if idx%2 else -1))
        for i,ob in enumerate(spine):
            ob.location=((i-5.5)*(.16+.56*spread),0,-.45-.6*smooth((t-4.8)/.65))
            ob.scale=(.27,.22*(1-smooth((t-4.8)/.65))+.005,.22)
            ob.rotation_euler=(0,(i%2*2-1)*(1-spread)*.8,0)
        pulse=math.exp(-((t-5.25)/.18)**2)
        corelight.location=corepos+Vector((0,0,1));corelight.data.energy=(35+120*pulse)*fade
        glow.node_tree.nodes['Principled BSDF'].inputs['Emission Strength'].default_value=(2.2+5*pulse)*fade
        cam.location=(1.7*(1-smooth(t/5.5)),.9+.35*(1-smooth(t/4)),12.4+4.4*smooth((t-.4)/5))
        cam.location.x+=.023*pulse*math.sin(t*70);base.point(cam,(0,0,0))
        cam.data.dof.focus_distance=cam.location.length;cam.data.dof.aperture_fstop=4.5+2.5*smooth(t/5)
        keylamp=lamps['Travelling softbox'];x=-5+10*smooth((t-4.2)/2.7)
        keylamp.location=(x,1.8,4);base.point(keylamp,(x*.7,0,0));keylamp.data.energy=(750+350*pulse)*fade
        lamps['Gradual frontal reveal'].data.energy=(170+380*smooth((t-3.3)/2.1))*fade
        lamps['Back rim'].data.energy=(700+500*pulse)*fade
        lamps['Low grazing edge'].data.energy=370*fade;lamps['Opposite rim'].data.energy=340*fade
        sc.world.node_tree.nodes['Background'].inputs[1].default_value=.08*fade
        for i,(o,start) in enumerate(dust):o.location=start+Vector((.11*math.sin(t*.7+i),t*.025,0))
        if key:
            for ob in animated:
                for field in ['location','rotation_euler','scale']:ob.keyframe_insert(data_path=field,frame=f+1)
            for ob in [*lamps.values(),corelight]:ob.data.keyframe_insert(data_path='energy',frame=f+1)
            cam.data.dof.keyframe_insert(data_path='focus_distance',frame=f+1)
            cam.data.dof.keyframe_insert(data_path='aperture_fstop',frame=f+1)
            glow.node_tree.nodes['Principled BSDF'].inputs['Emission Strength'].keyframe_insert(data_path='default_value',frame=f+1)
            sc.world.node_tree.nodes['Background'].inputs[1].keyframe_insert(data_path='default_value',frame=f+1)
            for o,_ in dust:o.keyframe_insert(data_path='location',frame=f+1)
    # Bake every frame so the saved .blend plays the actual delivered animation.
    for f in range(120):pose(f,True)
    return sc,pose

def main():
    sc,pose=build();folder=ROOT/'frames'/NAME;folder.mkdir(parents=True,exist_ok=True)
    frames=[10,26,42,58,72,88] if MODE=='preview' else range(120)
    start=time.monotonic()
    for f in frames:
        pose(f);sc.render.filepath=str(folder/f'{f:03}.png');bpy.ops.render.render(write_still=True)
        print('PROGRESS',f+1,'/120',round(time.monotonic()-start,1),'seconds',flush=True)
    sc.frame_set(73);bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/(NAME+'.blend')))
    print('COMPLETE',MODE,round(time.monotonic()-start,1),'seconds',flush=True)
if __name__ == "__main__": main()
