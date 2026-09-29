"""Black anodised leverless XORA board, source-accurate controls, RGB diffuser."""
import bpy,sys,math,re,time,json,colorsys
from pathlib import Path
from mathutils import Vector,Euler
ROOT=Path(__file__).resolve().parent;sys.path.insert(0,str(ROOT))
import base_scene as base
from mechanics import clip,cylinder,emission
from geometry import morph_plate,box
NAME='16-xora-translucent-rgb'
argv=sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else []
MODE=argv[0] if argv else 'preview'
L=json.loads((ROOT/'layout-source.json').read_text(encoding='utf-8'))
FX=json.loads((ROOT/'transform-samples.json').read_text(encoding='utf-8'))
def linear(rgb):
    return tuple((c/255/12.92 if c/255<=.04045 else ((c/255+.055)/1.055)**2.4) for c in rgb)
W=8.;H=W*L['height']/L['width'];MM=W/L['board_width_mm'];LED=3*MM
TILT=-math.radians(45);BOARD=Euler((TILT,0,0)).to_matrix();ORIGIN=Vector((0,.60,0))
smooth=base.smooth
def world(v):return ORIGIN+BOARD@Vector(v)
def xy(item):return ((item['display_x']/L['width']-.5)*W,(.5-item['display_y']/L['height'])*H)
def rounded(w,h,r):
    out=[]
    for cx,cy,a in [(w/2-r,h/2-r,0),(-w/2+r,h/2-r,90),(-w/2+r,-h/2+r,180),(w/2-r,-h/2+r,270)]:
        out += [(cx+r*math.cos(math.radians(a+j*90/20)),cy+r*math.sin(math.radians(a+j*90/20))) for j in range(21)]
    return out
def animated_material(m):return m.node_tree.nodes['Principled BSDF']

def dome(name,r,mat):
    # Closed clear polycarbonate lens: shallow convex crown, rounded shoulder,
    # cylindrical skirt and a flat underside. Dimensions remain source-accurate.
    rings=[(r*j/12,.046+.055*(1-(j/12)**2)) for j in range(1,13)]
    rings += [(r*.998,.025),(r*.98,-.034)]
    verts=[(0,0,.101)];faces=[];n=64
    for radius,z in rings:
        verts.extend((radius*math.cos(a*math.tau/n),radius*math.sin(a*math.tau/n),z) for a in range(n))
    for a in range(n):faces.append((0,1+a,1+(a+1)%n))
    for row in range(len(rings)-1):
        a=1+row*n;b=a+n
        for j in range(n):faces.append((a+j,b+j,b+(j+1)%n,a+(j+1)%n))
    bottom=len(verts);verts.append((0,0,-.034));a=1+(len(rings)-1)*n
    for j in range(n):faces.append((bottom,a+(j+1)%n,a+j))
    mesh=bpy.data.meshes.new(name);mesh.from_pydata(verts,[],faces);mesh.update()
    ob=bpy.data.objects.new(name,mesh);bpy.context.collection.objects.link(ob);mesh.materials.append(mat)
    for face in mesh.polygons:face.use_smooth=True
    ob['crown_rise_mm']=.055/MM
    return ob

def internal_rings(name,r,mat):
    # Physical concentric ridges sit below the smooth smoky crown.
    verts=[];faces=[];major=96;minor=8
    for fraction in [.25,.36,.47,.58,.69,.80,.90]:
        rr=r*fraction;z=.046+.023*(1-fraction*fraction);tube=.0035
        offset=len(verts)
        for a in range(major):
            theta=a*math.tau/major
            for b in range(minor):
                phi=b*math.tau/minor
                verts.append(((rr+tube*math.cos(phi))*math.cos(theta),(rr+tube*math.cos(phi))*math.sin(theta),z+tube*math.sin(phi)))
        for a in range(major):
            for b in range(minor):faces.append(tuple(offset+aa*minor+bb for aa,bb in [(a,b),((a+1)%major,b),((a+1)%major,(b+1)%minor),(a,(b+1)%minor)]))
    mesh=bpy.data.meshes.new(name);mesh.from_pydata(verts,[],faces);mesh.update()
    ob=bpy.data.objects.new(name,mesh);bpy.context.collection.objects.link(ob);mesh.materials.append(mat)
    for face in mesh.polygons:face.use_smooth=True
    ob['internal_concentric_ridges']=7
    return ob

def travelling_gradient(mat):
    n=mat.node_tree.nodes;l=mat.node_tree.links;bs=animated_material(mat)
    coord=n.new('ShaderNodeTexCoord');split=n.new('ShaderNodeSeparateXYZ');l.new(coord.outputs['Generated'],split.inputs[0])
    x=n.new('ShaderNodeMath');x.operation='MULTIPLY';x.inputs[1].default_value=L['width'];l.new(split.outputs['X'],x.inputs[0])
    edge=n.new('ShaderNodeValue');edge.name='Transform sweep right edge'
    delta=n.new('ShaderNodeMath');delta.operation='SUBTRACT';l.new(edge.outputs[0],delta.inputs[0]);l.new(x.outputs[0],delta.inputs[1])
    normalize=n.new('ShaderNodeMath');normalize.operation='DIVIDE';normalize.inputs[1].default_value=FX['band_width'];normalize.use_clamp=True;l.new(delta.outputs[0],normalize.inputs[0])
    ramp=n.new('ShaderNodeValToRGB');ramp.name='Blue green alternating colors';ramp.color_ramp.interpolation='EASE'
    l.new(normalize.outputs[0],ramp.inputs[0]);l.new(ramp.outputs[0],bs.inputs['Base Color']);l.new(ramp.outputs[0],bs.inputs['Emission Color'])
    return edge,ramp

def build():
    sc,_=base.scene(0);sc.frame_end=120;sc.cycles.samples=64;sc.cycles.max_bounces=8;sc.cycles.transmission_bounces=6
    sc.cycles.volume_bounces=2
    haze=bpy.data.materials['Soft procedural atmosphere']
    haze_density=next(n for n in haze.node_tree.nodes if n.type=='MATH')
    haze_density.name='Heavy to light atmosphere'
    volume=next(n for n in haze.node_tree.nodes if n.type=='PRINCIPLED_VOLUME')
    volume.inputs['Anisotropy'].default_value=.4
    sc.unit_settings.system='METRIC';sc.unit_settings.scale_length=L['board_width_mm']/8000
    bpy.data.objects['Dark reflection floor'].location.y=-1.53
    for ob in list(bpy.data.objects):
        if ob.name.startswith('XORA original') or ob.name in ['O machined ring','O arcade button','O circular machined detail']:
            bpy.data.objects.remove(ob,do_unlink=True)
    anodised=base.material('Black anodised aluminium satin',(.008,.010,.014),.72,.43)
    p=animated_material(anodised);p.inputs['Coat Weight'].default_value=.12;p.inputs['Coat Roughness'].default_value=.35
    for n in anodised.node_tree.nodes:
        if n.type=='BUMP':n.inputs['Strength'].default_value=.065;n.inputs['Distance'].default_value=.007
    shellmat=base.material('Black anodised lower shell',(.007,.009,.013),.66,.38)
    caps=emission('Full surface internal RGB diffuser',(.03,.32,.8),1)
    capbs=animated_material(caps);capbs.inputs['Metallic'].default_value=0;capbs.inputs['Roughness'].default_value=.30
    clear=bpy.data.materials.new('Smoky translucent polycarbonate');clear.use_nodes=True
    clearbs=animated_material(clear);clearbs.inputs['Base Color'].default_value=(.42,.45,.49,1)
    clearbs.inputs['Transmission Weight'].default_value=.88;clearbs.inputs['IOR'].default_value=1.49;clearbs.inputs['Roughness'].default_value=.10
    clearbs.inputs['Coat Weight'].default_value=.32;clearbs.inputs['Coat Roughness'].default_value=.075
    # Very fine moulded-plastic roughness leaves the broad convex reflections legible.
    noise=clear.node_tree.nodes.new('ShaderNodeTexNoise');noise.inputs['Scale'].default_value=220;noise.inputs['Detail'].default_value=2
    rough=clear.node_tree.nodes.new('ShaderNodeMapRange');rough.inputs['To Min'].default_value=.08;rough.inputs['To Max'].default_value=.14
    clear.node_tree.links.new(noise.outputs['Fac'],rough.inputs['Value']);clear.node_tree.links.new(rough.outputs['Result'],clearbs.inputs['Roughness'])
    ringmat=bpy.data.materials.new('Smoky moulded internal concentric ridges');ringmat.use_nodes=True
    rb=animated_material(ringmat);rb.inputs['Base Color'].default_value=(.20,.23,.27,1);rb.inputs['Roughness'].default_value=.22;rb.inputs['Transmission Weight'].default_value=.25;rb.inputs['IOR'].default_value=1.49;rb.inputs['Coat Weight'].default_value=.25
    socketmat=base.material('Black key sockets',(.004,.006,.008),.32,.37)
    # The complete board retains exactly the WebConfig panel aspect ratio.
    shell=box('XORA lower enclosure',(W,H,.35),world((0,0,-.04)),shellmat,.10);shell.rotation_euler=(TILT,0,0)
    rim=box('Black aluminium panel underlay',(W-.06,H-.06,.045),world((0,0,.155)),anodised,.08);rim.rotation_euler=(TILT,0,0)
    # A genuine 3 mm high annular diffuser forms the lowest perimeter layer.
    ambientmat=emission('3mm perimeter RGB diffuser',(.015,.3,.75),2)
    ambientedge,ambientramp=travelling_gradient(ambientmat)
    ambient=base.curve('3 mm full perimeter light guide',[rounded(W-.035,H-.035,.13),list(reversed(rounded(W-.20,H-.20,.09)))],ambientmat,depth=LED/2,bevel=0)
    ambient.location=world((0,0,-.215-LED/2));ambient.rotation_euler=(TILT,0,0)
    ambient['thickness_mm']=3.;ambient['model_thickness']=LED
    backing=[(o,o.location.copy()) for o in [shell,rim,ambient]]
    # Only the continuous emissive perimeter lights this volume; no point fills.
    bpy.ops.mesh.primitive_cube_add(size=1,location=world((0,0,-.42)))
    localfog=bpy.context.object;localfog.name='Continuous perimeter scattering volume';localfog.dimensions=(W+1.8,H+1.8,.9)
    bpy.ops.object.transform_apply(location=False,rotation=False,scale=True);localfog.rotation_euler=(TILT,0,0)
    fogmat=bpy.data.materials.new('Soft perimeter haze');fogmat.use_nodes=True;fn=fogmat.node_tree.nodes;fl=fogmat.node_tree.links;fn.clear()
    out=fn.new('ShaderNodeOutputMaterial');scatter=fn.new('ShaderNodeVolumePrincipled');scatter.inputs['Color'].default_value=(.72,.79,.9,1);scatter.inputs['Anisotropy'].default_value=.15
    noise=fn.new('ShaderNodeTexNoise');noise.inputs['Scale'].default_value=2.8;noise.inputs['Detail'].default_value=2
    localdensity=fn.new('ShaderNodeMath');localdensity.operation='MULTIPLY';localdensity.name='Fading perimeter haze density'
    coords=fn.new('ShaderNodeTexCoord');offset=fn.new('ShaderNodeVectorMath');offset.operation='SUBTRACT';offset.inputs[1].default_value=(.5,.5,.5);fl.new(coords.outputs['Generated'],offset.inputs[0])
    absolute=fn.new('ShaderNodeVectorMath');absolute.operation='ABSOLUTE';fl.new(offset.outputs[0],absolute.inputs[0])
    axes=fn.new('ShaderNodeSeparateXYZ');fl.new(absolute.outputs[0],axes.inputs[0])
    maximum=fn.new('ShaderNodeMath');maximum.operation='MAXIMUM';fl.new(axes.outputs['X'],maximum.inputs[0]);fl.new(axes.outputs['Y'],maximum.inputs[1])
    extent=fn.new('ShaderNodeMath');extent.operation='MAXIMUM';fl.new(maximum.outputs[0],extent.inputs[0]);fl.new(axes.outputs['Z'],extent.inputs[1])
    feather=fn.new('ShaderNodeMapRange');feather.interpolation_type='SMOOTHSTEP';feather.inputs['From Min'].default_value=.28;feather.inputs['From Max'].default_value=.5;feather.inputs['To Min'].default_value=1;feather.inputs['To Max'].default_value=0;fl.new(extent.outputs[0],feather.inputs['Value'])
    shaped=fn.new('ShaderNodeMath');shaped.operation='MULTIPLY';fl.new(noise.outputs['Fac'],shaped.inputs[0]);fl.new(feather.outputs['Result'],shaped.inputs[1])
    fl.new(shaped.outputs[0],localdensity.inputs[0]);fl.new(localdensity.outputs[0],scatter.inputs['Density']);fl.new(scatter.outputs['Volume'],out.inputs['Volume']);localfog.data.materials.append(fogmat)
    paths=re.findall(r'<path[^>]* d="([^"]+)"',base.SVG.read_text(encoding='utf-8'))
    specs=[]
    for j in range(4):specs.append(('X panel '+str(j),base.parse(paths[j])))
    rp=base.parse(paths[4])
    for j,(lo,hi) in enumerate([(.11,.70),(.70,1.48),(1.48,2.17)]):specs.append(('R panel '+str(j),clip(clip(rp,0,lo,True),0,hi,False)))
    ap=base.parse(paths[5]);low=clip(ap,1,.66,False)
    specs += [('A left panel',clip(low,0,3.5,False)),('A right panel',clip(low,0,3.5,True)),('A crown panel',clip(ap,1,.66,True))]
    cols=[(-2,1),(-1,1),(-2,-1),(-1,-1),(0,1),(1,1),(0,-1),(1,-1),(2,-1),(2,1)]
    cw=(W-.14)/5;ch=(H-.14)/2
    rectangle=[(-cw/2+.004,-ch/2+.004),(cw/2-.004,-ch/2+.004),(cw/2-.004,ch/2-.004),(-cw/2+.004,ch/2-.004)]
    panels=[]
    for i,(name,poly) in enumerate(specs):
        ob,key,target=morph_plate(name,poly,anodised,rectangle,depth=.023);ob.data.bevel_depth=.003
        c,r=cols[i];start=world((c*cw,r*ch/2,.202));ob.location=start;ob.rotation_euler=(TILT,0,0)
        panels.append((ob,key,target,start,2.6+i*.085))
    buttons=[];lenses=[];keymats=[];sockets=[];layout_report=[]
    for item in L['buttons']:
        ident=item['id'];x,y=xy(item);r=item['display_radius']/L['width']*W
        start=world((x,y,.275))
        if 11<=ident<=18:
            i=ident-11;a=i*math.tau/8;b=(i+1)*math.tau/8
            poly=[(-1.16+1.11*math.cos(a+(b-a)*j/24),1.11*math.sin(a+(b-a)*j/24)) for j in range(25)]
            poly += [(-1.16+.91*math.cos(a+(b-a)*j/24),.91*math.sin(a+(b-a)*j/24)) for j in range(24,-1,-1)]
            ob,key,target=morph_plate('Key '+item['label'],poly,caps,base.ring(0,0,r*.93),depth=.035)
            role='O ring';delay=2.45+i*.06
        elif ident==2:
            ob,key,target=morph_plate('Key 2',base.ring(-1.16,0,.77),caps,base.ring(0,0,r*.93),depth=.035)
            target.z=.06;role='O centre';delay=2.25
        else:
            ob=base.curve('Key '+item['label'],[base.ring(0,0,r*.93)],caps,depth=.035,bevel=.01)
            key=None;target=None;role='retract';delay=2.7
        ob.data.bevel_depth=.004
        mat=caps.copy();mat.name='Full key RGB '+item['label'];ob.data.materials[0]=mat
        keymats.append((mat,ident-1))
        ob.location=start;ob.rotation_euler=(TILT,0,0);ob['button_id']=ident;ob['display_x']=item['display_x'];ob['display_y']=item['display_y'];ob['display_radius']=item['display_radius']
        buttons.append((ob,key,target,start,delay,role))
        socket=cylinder('Socket '+item['label'],r+.022,.024,socketmat,64);socket.location=world((x,y,.235));socket.rotation_euler=(TILT,0,0);sockets.append((socket,socket.location.copy()))
        lens=dome('Transparent convex cap '+item['label'],r,clear);lens.location=start;lens.rotation_euler=(TILT,0,0)
        lenses.append((lens,ob))
        ridges=internal_rings('Internal concentric ridges '+item['label'],r,ringmat);ridges.location=start;ridges.rotation_euler=(TILT,0,0);lenses.append((ridges,ob))
        boss=cylinder('Internal centre hub '+item['label'],r*.17,.018,socketmat,48);boss.location=start;boss.rotation_euler=(TILT,0,0)
        # Geometry offset preserves shared animated transforms with the outer cap.
        for v in boss.data.vertices:v.co.z+=.061
        lenses.append((boss,ob))
        layout_report.append(dict(id=ident,label=item['label'],x=x,y=y,radius=r))
    (ROOT/'model-layout.json').write_text(json.dumps(dict(width=W,height=H,mm_per_unit=1/MM,led_layer_thickness=LED,buttons=layout_report),indent=2),encoding='utf-8')
    spill=[]
    cam=sc.camera;lamps={n:bpy.data.objects[n] for n in ['Travelling softbox','Gradual frontal reveal','Back rim','Low grazing edge','Opposite rim']}
    dust=[(o,o.location.copy()) for o in bpy.data.objects if o.name.startswith('Dust ')]
    allobjects=[cam]+[o for o,_ in backing+sockets+spill]+[o for o,*_ in panels]+[o for o,*_ in buttons]+[o for o,_ in lenses]+list(lamps.values())
    def pose(f,bake=False):
        sc.frame_set(f+1);t=f/12;fade=smooth(t/.35)*(1-smooth((t-8.85)/.95));fold=smooth((t-3.0)/2.75)
        clearing=smooth(t/5.8)
        haze_density.inputs[1].default_value=.035*(1-clearing)+.004*clearing
        localdensity.inputs[1].default_value=.30*(1-clearing)+.012*clearing
        for i,(ob,key,target,start,delay) in enumerate(panels):
            u=smooth((t-delay)/2.55);ob.location=start.lerp(target,u)+Vector((0,.17*math.sin(math.pi*u),.65*math.sin(math.pi*u)))
            ob.rotation_euler=(TILT*(1-u),(1 if i%2 else -1)*1.2*math.sin(math.pi*u),.09*math.sin(math.pi*u));key.value=smooth((u-.18)/.64)
        for ob,key,target,start,delay,role in buttons:
            if role!='retract':
                u=smooth((t-delay)/2.65);ob.location=start.lerp(target,u)+Vector((0,.35*math.sin(math.pi*u),.30*math.sin(math.pi*u)))
                ob.rotation_euler=(TILT*(1-u),.3*math.sin(math.pi*u),.5*math.sin(math.pi*u));key.value=smooth((u-.18)/.65)
            else:
                r=smooth((t-2.65)/1.5);ob.location=start-BOARD@Vector((0,0,.28*r))+Vector((0,-.5*fold,-1.8*fold));ob.scale=(max(.001,1-r),)*3
        for ob,start in backing+sockets:
            ob.location=start+Vector((0,-.50*fold,-1.8*fold));ob.rotation_euler=(TILT-.7*fold,0,0);ob.scale=(1-.92*fold,1-.985*fold,1-.98*fold)
        for lens,button in lenses:
            # The clear crown sinks into the same moving key as it becomes metal.
            flatten=1-smooth((t-2.4)/1.15)
            lens.location=button.location;lens.rotation_euler=button.rotation_euler
            lens.scale=tuple(max(.001,s*flatten) for s in button.scale)
        warm=smooth((t-4.1)/1.55)
        col=Vector((.008,.010,.014)).lerp(Vector((.34,.40,.48)),warm);p.inputs['Base Color'].default_value=(*col,1);p.inputs['Metallic'].default_value=.72+.16*warm;p.inputs['Roughness'].default_value=.43-.17*warm
        for mat,index in keymats:
            rgb=Vector(linear(FX['frames'][f][index]));cap=animated_material(mat);col=(rgb*.24).lerp(Vector((.34,.4,.48)),warm)
            cap.inputs['Base Color'].default_value=(*col,1);cap.inputs['Emission Color'].default_value=(*rgb,1)
            cap.inputs['Emission Strength'].default_value=.17*fade*(1-smooth((t-3.4)/1.2))
            cap.inputs['Metallic'].default_value=.88*warm;cap.inputs['Roughness'].default_value=.30-.04*warm
        cycle=int(t/FX['cycle_seconds']);progress=(t/FX['cycle_seconds'])%1
        centre=FX['min_x']+(FX['max_x']-FX['min_x'])*progress*1.6
        ambientedge.outputs[0].default_value=centre+FX['band_width']/2
        old,new=(FX['green'],FX['blue']) if cycle%2 else (FX['blue'],FX['green'])
        ambientramp.color_ramp.elements[0].color=(*linear(old),1)
        ambientramp.color_ramp.elements[1].color=(*linear(new),1)
        bs=animated_material(ambientmat);bs.inputs['Emission Strength'].default_value=4*fade*(1-fold)
        for i,(ob,start) in enumerate(spill):
            ob.location=start+Vector((0,-.5*fold,-1.8*fold));ob.data.energy=(20*(1-clearing)+5*clearing)*fade*(1-fold)
            x=(start.x/W+.5)*L['width'];amount=smooth((centre+70-x)/140)
            ob.data.color=Vector(linear(old)).lerp(Vector(linear(new)),amount)
        c=smooth((t-2)/4.4);cam.location=Vector((1.2,4.5,16.1)).lerp(Vector((0,.8,16.8)),c);base.point(cam,(0,.4*(1-c),0));cam.data.dof.focus_distance=cam.location.length;cam.data.dof.aperture_fstop=8
        x=-5+10*smooth((t-5.5)/2.5);keylamp=lamps['Travelling softbox'];keylamp.location=(x,4-2.2*c,5);base.point(keylamp,(x*.65,0,0));keylamp.data.energy=600*fade
        lamps['Gradual frontal reveal'].data.energy=(350+180*c)*fade;lamps['Back rim'].data.energy=450*fade;lamps['Low grazing edge'].data.energy=180*fade;lamps['Opposite rim'].data.energy=180*fade
        sc.world.node_tree.nodes['Background'].inputs[1].default_value=.055*fade
        for i,(o,start) in enumerate(dust):o.location=start+Vector((.07*math.sin(t*.5+i),t*.018,0))
        if bake:
            haze_density.inputs[1].keyframe_insert(data_path='default_value',frame=f+1)
            localdensity.inputs[1].keyframe_insert(data_path='default_value',frame=f+1)
            for ob in allobjects:
                for field in ['location','rotation_euler','scale']:ob.keyframe_insert(data_path=field,frame=f+1)
            for ob,key,*_ in panels+buttons:
                if key:key.keyframe_insert(data_path='value',frame=f+1)
            for ob in list(lamps.values())+[ob for ob,_ in spill]:
                ob.data.keyframe_insert(data_path='energy',frame=f+1);ob.data.keyframe_insert(data_path='color',frame=f+1)
            ambientedge.outputs[0].keyframe_insert(data_path='default_value',frame=f+1)
            for elem in ambientramp.color_ramp.elements:elem.keyframe_insert(data_path='color',frame=f+1)
            for mat in [anodised,ambientmat]+[mat for mat,*_ in keymats]:
                bs=animated_material(mat)
                for name in ['Base Color','Metallic','Roughness','Emission Color','Emission Strength']:bs.inputs[name].keyframe_insert(data_path='default_value',frame=f+1)
            sc.world.node_tree.nodes['Background'].inputs[1].keyframe_insert(data_path='default_value',frame=f+1);cam.data.dof.keyframe_insert(data_path='focus_distance',frame=f+1)
            for ob,_ in dust:ob.keyframe_insert(data_path='location',frame=f+1)
    for f in range(120):pose(f,True)
    return sc,pose

def main():
    sc,pose=build();folder=ROOT/'frames'/NAME;folder.mkdir(parents=True,exist_ok=True)
    frames=[6,9,18,30,40,52,68,88] if MODE=='preview' else range(120);start=time.monotonic()
    for f in frames:
        pose(f);sc.render.filepath=str(folder/f'{f:03}.png');bpy.ops.render.render(write_still=True);print('PROGRESS',f+1,'/120',round(time.monotonic()-start,1),'seconds',flush=True)
    sc.frame_set(13);bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/(NAME+'.blend')))
    if MODE=='preview':
        # Orthographic top view independently exposes all source layout positions.
        sc.camera.animation_data_clear();pose(12);cam=sc.camera;cam.animation_data_clear();centre=world((0,0,0));cam.location=centre+BOARD@Vector((0,0,14));base.point(cam,centre);cam.rotation_euler=(TILT,0,0);cam.data.type='ORTHO';cam.data.ortho_scale=8.4;cam.data.dof.use_dof=False
        sc.render.resolution_x=787;sc.render.resolution_y=489;sc.render.filepath=str(ROOT/'layout-top-check.png');bpy.ops.render.render(write_still=True)
        # An additional close view makes smoke tint and internal moulding inspectable.
        pose(6);cam.animation_data_clear();cam.data.type='PERSP';cam.data.lens=63
        target=world((-.35,.1,.27));cam.location=target+BOARD@Vector((1,-3.2,9.2));base.point(cam,target)
        sc.render.resolution_x=1280;sc.render.resolution_y=720;sc.render.filepath=str(ROOT/'material-detail.png');bpy.ops.render.render(write_still=True)
    print('COMPLETE',MODE,round(time.monotonic()-start,1),'seconds',flush=True)
main()
