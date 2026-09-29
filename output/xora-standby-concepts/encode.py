"""Encode locally rendered vector frames as exact-average-12-FPS GIFs."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
import json, time

ROOT=Path(__file__).resolve().parent
NAMES=['01-mechanical-assembly','02-reactor-awakening','03-neon-arcade','04-titanium-fold']
TITLES=['01 / MECHANICAL ASSEMBLY','02 / REACTOR AWAKENING','03 / NEON ARCADE','04 / TITANIUM FOLD']
# GIF ticks are 10 ms. 80/80/90 ms gives exactly 12 frames per second;
# flooring cumulative boundaries also preserves the WebConfig sample indices.
DELAYS=[((i+1)*100//12-i*100//12)*10 for i in range(72)]

def encode():
    results=[]
    all_frames=[]
    for name in NAMES:
        start=time.monotonic()
        frames=[Image.open(p).convert('RGB') for p in sorted((ROOT/'frames'/name).glob('*.png'))]
        assert len(frames)==72
        # One palette per concept avoids palette flicker between frames.
        atlas=Image.new('RGB',(320*12,172*6))
        for i,frame in enumerate(frames): atlas.paste(frame,((i%12)*320,(i//12)*172))
        palette=atlas.quantize(colors=256,method=Image.Quantize.MEDIANCUT)
        indexed=[f.quantize(palette=palette,dither=Image.Dither.NONE) for f in frames]
        target=ROOT/(name+'.gif')
        indexed[0].save(target,save_all=True,append_images=indexed[1:],duration=DELAYS,loop=0,disposal=2,optimize=False)
        gif=Image.open(target)
        duration=0
        for i in range(gif.n_frames):
            gif.seek(i);gif.load();assert gif.size==(320,172)
            assert gif.info['duration']==DELAYS[i]
            duration+=gif.info['duration']
        assert gif.n_frames==72 and duration==6000 and gif.info['loop']==0
        results.append(dict(file=target.name,width=320,height=172,frames=72,duration_ms=duration,average_fps=12,bytes=target.stat().st_size,loop='infinite',delay_ms_pattern=[80,80,90]))
        frames[36].save(ROOT/(name+'-poster.png'))
        all_frames.append(frames)
        print('VERIFIED',name,results[-1],f'{time.monotonic()-start:.1f}s',flush=True)
    sheet=Image.new('RGB',(1280,4*208),(9,14,22));d=ImageDraw.Draw(sheet)
    font=ImageFont.truetype('C:/Windows/Fonts/consola.ttf',16)
    for row,frames in enumerate(all_frames):
        d.text((14,row*208+9),TITLES[row],font=font,fill='#dce7ef')
        for col,idx in enumerate([8,17,26,40]): sheet.paste(frames[idx],(col*320,row*208+32))
    sheet.save(ROOT/'contact-sheet.png')
    (ROOT/'validation.json').write_text(json.dumps(results,indent=2),encoding='utf-8')

if __name__=='__main__': encode()
