from pathlib import Path
from PIL import Image, ImageDraw, ImageFont, GifImagePlugin
import json

ROOT=Path(__file__).resolve().parent
NAMES=['05-silver-diagonal','06-blue-horizon','07-aurora-ribbon','08-gold-emergence']
TITLES=['05 / SILVER DIAGONAL','06 / BLUE HORIZON','07 / AURORA RIBBON','08 / GOLD EMERGENCE']
DELAYS=[((i+1)*100//12-i*100//12)*10 for i in range(96)]
sheet=Image.new('RGB',(1280,208*4),'#090e16');draw=ImageDraw.Draw(sheet)
font=ImageFont.truetype('C:/Windows/Fonts/consola.ttf',16)
results=[]
for row,name in enumerate(NAMES):
    frames=[Image.open(p).convert('RGB') for p in sorted((ROOT/'frames'/name).glob('*.png'))]
    assert len(frames)==96
    atlas=Image.new('RGB',(320*12,172*8))
    for i,im in enumerate(frames):atlas.paste(im,((i%12)*320,(i//12)*172))
    palette=atlas.quantize(colors=256,method=Image.Quantize.MEDIANCUT)
    indexed=[im.quantize(palette=palette,dither=Image.Dither.NONE) for im in frames]
    target=ROOT/(name+'.gif')
    # Explicit complete frames retain the intended 96-frame timeline, including
    # black holds. A global palette prevents color flicker.
    with target.open('wb') as output:
        header,_=GifImagePlugin.getheader(indexed[0],info={'loop':0})
        for block in header:output.write(block)
        for i,im in enumerate(indexed):
            for block in GifImagePlugin.getdata(im,duration=DELAYS[i],disposal=2):output.write(block)
        output.write(b';')
    gif=Image.open(target);assert gif.n_frames==96 and gif.info['loop']==0
    duration=0
    for i in range(gif.n_frames):
        gif.seek(i);gif.load();assert gif.size==(320,172)
        assert gif.info['duration']==DELAYS[i]
        duration+=gif.info['duration']
    assert duration==8000
    results.append({'file':target.name,'size':[320,172],'frames':96,'duration_ms':duration,'average_fps':12,'bytes':target.stat().st_size,'loop':'infinite'})
    print('PASS',results[-1],flush=True)
    draw.text((14,row*208+9),TITLES[row],font=font,fill='#dce7ef')
    for col,idx in enumerate([16,29,41,62]):sheet.paste(frames[idx],(col*320,row*208+32))
    frames[62].save(ROOT/(name+'-poster.png'))
sheet.save(ROOT/'contact-sheet.png')
(ROOT/'validation.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
