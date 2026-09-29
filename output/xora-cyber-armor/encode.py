"""Downsample rendered 3D frames and encode native-screen GIF deliverables."""
from pathlib import Path
from PIL import Image,ImageDraw,ImageFont,GifImagePlugin
import json,time
ROOT=Path(__file__).resolve().parent
NAMES=['17-xora-cyber-armor']
TITLES=['17 / CYBER ARMOR']
DELAYS=[((i+1)*100//12-i*100//12)*10 for i in range(120)]
sheet=Image.new('RGB',(1280,208),'#090e16');draw=ImageDraw.Draw(sheet)
font=ImageFont.truetype('C:/Windows/Fonts/consola.ttf',16)
results=[]
for row,name in enumerate(NAMES):
    start=time.monotonic()
    sources=sorted((ROOT/'frames'/name).glob('*.png'))
    assert len(sources)==120,(name,len(sources))
    assert [p.stem for p in sources]==[f'{f:03}' for f in range(120)]
    frames=[]
    for p in sources:
        with Image.open(p) as im:
            assert im.size==(640,344)
            frames.append(im.convert('RGB').resize((320,172),Image.Resampling.LANCZOS))
    atlas=Image.new('RGB',(320*12,172*10))
    for i,im in enumerate(frames):atlas.paste(im,((i%12)*320,(i//12)*172))
    palette=atlas.quantize(colors=256,method=Image.Quantize.MEDIANCUT)
    indexed=[im.quantize(palette=palette,dither=Image.Dither.NONE) for im in frames]
    target=ROOT/(name+'.gif')
    with target.open('wb') as output:
        header,_=GifImagePlugin.getheader(indexed[0],info={'loop':0})
        for block in header:output.write(block)
        for i,im in enumerate(indexed):
            for block in GifImagePlugin.getdata(im,duration=DELAYS[i],disposal=2):output.write(block)
        output.write(b';')
    gif=Image.open(target);assert gif.n_frames==120 and gif.info['loop']==0
    duration=0
    for i in range(gif.n_frames):
        gif.seek(i);gif.load();assert gif.size==(320,172)
        assert gif.info['duration']==DELAYS[i]
        duration+=gif.info['duration']
    assert duration==10000
    results.append({'file':target.name,'size':[320,172],'frames':120,'duration_ms':duration,'average_fps':12,'bytes':target.stat().st_size,'loop':'infinite','render_engine':'Blender 5.0.1 / Cycles / OptiX','master_size':[640,344]})
    print('PASS',results[-1],round(time.monotonic()-start,2),'seconds',flush=True)
    draw.text((14,row*208+9),TITLES[row],font=font,fill='#dce7ef')
    for col,idx in enumerate([9,18,52,88]):sheet.paste(frames[idx],(col*320,row*208+32))
    frames[88].save(ROOT/(name+'-poster.png'))
sheet.save(ROOT/'contact-sheet.png')
(ROOT/'validation.json').write_text(json.dumps(results,indent=2),encoding='utf-8')


