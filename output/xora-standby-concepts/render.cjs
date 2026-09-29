// Local vector animation. Reads the actual WebConfig logo; no network calls.
const fs = require('node:fs');
const path = require('node:path');
const sharp = require('../../application/www/node_modules/sharp');
const out = __dirname;
const source = fs.readFileSync(path.join(out, '../../application/www/public/images/xora-mono-slate.svg'), 'utf8');
const paths = [...source.matchAll(/<path[^>]* d="([^"]+)"/g)].map(m => m[1]);
const clamp = x => Math.max(0, Math.min(1, x));
const ease = x => 1-Math.pow(1-clamp(x), 3);
const smooth = x => { x=clamp(x); return x*x*(3-2*x); };
const n = x => Number(x.toFixed(3));
const C = ['#6fe5ff', '#ffa85a', '#f186ff', '#e9c58e'];
const names = ['01-mechanical-assembly','02-reactor-awakening','03-neon-arcade','04-titanium-fold'];
const P=(d,fill='url(#metal)',stroke='none',sw=0)=>`<path d="${d}" fill="${fill}" stroke="${stroke}" stroke-width="${sw}"/>`;
const circle=(x,y,r,stroke,w=1,fill='none',extra='')=>`<circle cx="${n(x)}" cy="${n(y)}" r="${n(Math.max(0,r))}" stroke="${stroke}" stroke-width="${w}" fill="${fill}" ${extra}/>`;
const line=(x,y,xx,yy,c,w=1)=>`<path d="M${n(x)} ${n(y)}L${n(xx)} ${n(yy)}" stroke="${c}" stroke-width="${w}" fill="none"/>`;
const group=(s,tr='',op=1)=>`<g transform="${tr}" opacity="${n(clamp(op))}">${s}</g>`;
function part(i,fill='url(#metal)',stroke='none',sw=0){
 if(i<4) return P(paths[i],fill,stroke,sw);
 if(i===4) return circle(384,120,101,fill,20);
 if(i===5) return circle(384,120,77,stroke,sw,fill);
 return P(paths[i-2],fill,stroke,sw);
}
function full(fill='url(#metal)',stroke='none',sw=0){return Array.from({length:8},(_,i)=>part(i,fill,stroke,sw)).join('');}
const placement=s=>group(s,'translate(25 53.6) scale(.27)');
const centers=[[93,70],[206,70],[93,170],[206,170],[384,120],[384,120],[614,120],[850,120]];
function transformPart(i,dx,dy,angle,sx=1,sy=1,fill='url(#metal)'){
 const [cx,cy]=centers[i];
 return group(part(i,fill),'translate('+n(cx+dx)+' '+n(cy+dy)+') rotate('+n(angle)+') scale('+n(sx)+' '+n(sy)+') translate('+(-cx)+' '+(-cy)+')');
}
function defs(k,t){const c=C[k];return `<defs>
 <linearGradient id="metal" x1="0" y1="0" x2=".12" y2="1" gradientUnits="objectBoundingBox"><stop stop-color="#fcffff"/><stop offset=".30" stop-color="${k===3?'#c6ab82':'#b2cbd5'}"/><stop offset=".48" stop-color="#f2f7fa"/><stop offset=".50" stop-color="${k===3?'#79654f':'#456170'}"/><stop offset=".75" stop-color="${k===3?'#dfcaa5':'#9eb9c5'}"/><stop offset="1" stop-color="#e7f5f9"/></linearGradient>
 <linearGradient id="neon"><stop stop-color="#63e8fa"/><stop offset=".5" stop-color="#ded7ff"/><stop offset="1" stop-color="#ff77d7"/></linearGradient>
 <radialGradient id="bg"><stop stop-color="${['#112431','#272019','#22152f','#24201c'][k]}"/><stop offset="1" stop-color="#03060b"/></radialGradient>
 <filter id="glow" x="-100%" y="-100%" width="300%" height="300%"><feGaussianBlur stdDeviation="2.2"/></filter>
 <filter id="soft"><feGaussianBlur stdDeviation="7"/></filter>
 <clipPath id="logo">${placement(full('white'))}</clipPath>
 </defs>`;}
function background(k,t){
 let s='<rect width="320" height="172" fill="#020409"/><ellipse cx="160" cy="86" rx="190" ry="108" fill="url(#bg)"/>';
 const phase=Math.sin(Math.PI*clamp(t/6));
 if(k===0){
  for(let i=0;i<6;i++){let y=35+i*21;s+=group(line(0,y,320,y,'#315260',.35), '',.24);}
  for(let x=25;x<320;x+=45)s+=group(line(x,23,x,149,'#315260',.35),'',.15);
  for(const x of [14,306])s+=group(line(x,60,x,112,C[k],.7),'',.35*phase);
 } else if(k===2){
  for(let x=-200;x<500;x+=40)s+=group(line(160+(x-160)*.25,113,x,172,'#5a2b69',.5),'',.45);
  for(let i=0;i<6;i++){let y=113+Math.pow((i+t)%6/6,2)*59;s+=group(line(0,y,320,y,'#613171',.5),'',.5);}
 } else {
  s+=group(`<ellipse cx="160" cy="127" rx="111" ry="8" fill="${C[k]}" filter="url(#soft)"/>`,'',.09*phase);
 }
 for(let i=0;i<19;i++){
  let x=(i*73.7+9)%320, y=(i*39.3+18)%172;
  let a=(.11+.1*Math.sin(t*1.7+i))*phase;
  s+=group(circle(x,y,.45,C[k],0,C[k]),'',a);
 }
 return s;
}
function mechanical(t){
 let s='',exit=smooth((t-4.85)/1.0), incoming=clamp(t/2.4);
 for(let i=0;i<8;i++){
  let p=ease((t-.18-i*.105)/1.25), q=1-p+exit;
  let dx=([-350,170,-230,310,0,0,230,400][i])*q;
  let dy=([-175,-200,195,220,-260,260,-160,130][i])*q;
  let r=([-115,100,105,-95,180,-180,70,-80][i])*q;
  s+=transformPart(i,dx,dy,r,1-.25*q,1, 'url(#metal)');
  if(q>.02)s+=group(transformPart(i,dx,dy,r,1-.25*q,1,'none'),'',.5);
 }
 let body=placement(s);
 let scan=clamp((t-2.0)/1.05)*400-40;
 body+=`<g clip-path="url(#logo)" opacity="${n((1-exit)*.75)}">${line(scan-25,20,scan+45,150,'#bcf4ff',11)}</g>`;
 let ring=clamp((t-1.85)/.9);
 if(ring>0&&ring<1)body+=group(`<ellipse cx="160" cy="86" rx="${n(20+ring*170)}" ry="${n(8+ring*48)}" fill="none" stroke="#83e9ff" stroke-width=".9"/>`,'',1-ring);
 for(let i=0;i<10;i++){
  let a=i*Math.PI*2/10, p=clamp((t-1.85)/.65), r=28+p*100;
  body+=group(line(160+Math.cos(a)*r,86+Math.sin(a)*r*.45,160+Math.cos(a)*(r+5),86+Math.sin(a)*(r+5)*.45,C[0],1),'',(1-p)*p*3);
 }
 body+=group(line(104,132,216,132,C[0],.7),'',smooth((t-2.3)/.5)*(1-exit)*.5);
 return group(body,'',smooth(t/.3)*(1-smooth((t-5.6)/.32)));
}
function reactor(t){
 let s='',p=ease((t-.85)/1.3),exit=smooth((t-4.85)/.95),power=smooth(t/.5)*(1-exit);
 let cx=160+(128.68-160)*p, cy=86;
 for(let i=0;i<3;i++){
  let r=18+i*8+(1-p)*5;
  s+=group(circle(cx,cy,r,C[1],.8,'none',`stroke-dasharray="${r*2} ${r*.6}" transform="rotate(${n(t*(i%2?-100:90)+i*40)} ${n(cx)} 86)"`),'',(1-p*.7)*power);
 }
 s+=group(circle(cx,cy,13+3*Math.sin(t*5),C[1],0,C[1]),'',.23*power);
 s+=group(circle(cx,cy,24,C[1],1.5),'',power*.5);
 for(let i of [0,1,2,3,6,7]){
  let v=ease((t-.6-i*.08)/1.3)*(1-exit),q=1-v;
  const [x,y]=centers[i];
  let dx=(384-x)*q+Math.sin(q*5+i)*80*q,dy=(120-y)*q+Math.cos(q*5+i)*160*q;
  s+=placement(group(transformPart(i,dx,dy,(i%2?120:-120)*q,.2+.8*v,.2+.8*v),'',v));
 }
 s+=group(placement(part(4,'url(#metal)')+part(5,'url(#metal)')),'',p*(1-exit));
 // The arcade button in the real logo becomes the ignition source.
 for(let i=0;i<3;i++){
  let v=clamp((t-1.7-i*.18)/1.2);
  if(v>0&&v<1)s+=group(circle(128.68,86,22+v*170,i===1?'#82e1f5':C[1],1),'',(1-v)*.7);
 }
 let beam=clamp((t-2.4)/1.1)*390-35;
 s+=`<g clip-path="url(#logo)" opacity="${n(.6*power)}">${line(beam,20,beam-30,150,'#ffe2af',13)}</g>`;
 return group(s,'',power);
}
function arcade(t){
 let s='',exit=smooth((t-4.9)/.9);
 for(let i=0;i<8;i++){
  let raw=clamp((t-.2-i*.075)/1.25),p=1-Math.pow(1-raw,3)*Math.cos(raw*7),q=1-p+exit;
  let dx=(i<4?-320:260)*q,dy=((i%2?-1:1)*210)*q;
  let r=(i%2?85:-85)*q;
  let fill='url(#neon)';
  if(Math.abs(q)>.03){
   s+=group(transformPart(i,dx-15,dy+5,r,1,1,'#25efff'),'',.3);
   s+=group(transformPart(i,dx+15,dy-5,r,1,1,'#fc39be'),'',.3);
  }
  // Small button press is a nod to the O arcade-button motif.
  let sy=i===5?1-.18*Math.max(0,1-Math.abs(t-3.5)/.25):1;
  s+=transformPart(i,dx,dy,r,1,sy,fill);
 }
 let b=placement(s),power=smooth(t/.25)*(1-smooth((t-5.65)/.3));
 for(let i=0;i<12;i++){
  let a=i*Math.PI/6+t*.45,r=58+12*Math.sin(t*2+i),x=160+Math.cos(a)*r*1.9,y=86+Math.sin(a)*r*.72;
  b+=group(`<rect x="${n(x)}" y="${n(y)}" width="2" height="2" fill="${i%2?'#fb70ce':'#70e8fa'}" transform="rotate(${i*30+t*30} ${n(x)} ${n(y)})"/>`,'',.45*(1-exit));
 }
 for(let i=0;i<3;i++)b+=group(line(133+i*20,134,144+i*20,134,i%2?'#fc72da':'#6be5fa',2),'',smooth((t-2-i*.1)/.2)*(1-exit));
 let scan=(t-2.3)*180;
 b+=`<g clip-path="url(#logo)" opacity="${n(.5*(1-exit))}">${line(scan,25,scan-40,143,'#ffffff',7)}</g>`;
 return group(b,'',power);
}
function titanium(t){
 let s='',exit=smooth((t-4.85)/.95);
 // Each source letter is divided into shutters; perspective-like width and
 // offsets give the unfolding its hinged mechanical rhythm.
 for(let i=0;i<16;i++){
  let p=ease((t-.15-i*.065)/1.25),q=1-p+exit;
  let x=i*62.5,cx=x+31.25;
  let dx=(i<8?-1:1)*210*q,dy=(i%2?1:-1)*150*q;
  let angle=(i%2?1:-1)*55*q,sx=.08+.92*(1-q);
  let clip=`<clipPath id="sl${i}"><rect x="${x}" y="0" width="62.7" height="240"/></clipPath>`;
  let shape=`<g clip-path="url(#sl${i})">${full()}</g>`;
  s+=clip+group(shape,`translate(${n(cx+dx)} ${n(120+dy)}) rotate(${n(angle)}) scale(${n(Math.max(.06,sx))} 1) translate(${-cx} -120)`);
 }
 if(t>=2.4&&t<=4.85)s=full();
 let b=placement(group(s,'translate(0 9)',.19))+placement(s);
 let power=smooth(t/.3)*(1-smooth((t-5.6)/.35));
 let sweep=(t-2.1)*160;
 b+=`<g clip-path="url(#logo)">${group(line(sweep,22,sweep-50,146,'#fff4d7',17),'',.8*(1-exit))}</g>`;
 let lock=smooth((t-2.2)/.5)*(1-exit);
 b+=group(line(117,132,203,132,C[3],.8),'',lock*.6);
 for(let i=0;i<2;i++)b+=group(`<path d="M${i?288:32} 114V${i?119:114}h${i?-19:19}" fill="none" stroke="${C[3]}" stroke-width=".7"/>`,'',lock*.6);
 return group(b,'',power);
}
const renderers=[mechanical,reactor,arcade,titanium];
async function main(){
 fs.mkdirSync(path.join(out,'frames'),{recursive:true});
 await sharp(Buffer.from(source)).resize(640).png().toFile(path.join(out,'logo-reference.png'));
 for(let k=0;k<4;k++){
  const start=Date.now();console.log('START',names[k]);
  const dir=path.join(out,'frames',names[k]);fs.mkdirSync(dir,{recursive:true});
  for(let f=0;f<72;f++){
   let t=f/12;
   let svg=`<svg xmlns="http://www.w3.org/2000/svg" width="960" height="516" viewBox="0 0 320 172">${defs(k,t)}${background(k,t)}${renderers[k](t)}</svg>`;
   await sharp(Buffer.from(svg)).resize(320,172).png().toFile(path.join(dir,String(f).padStart(3,'0')+'.png'));
  }
  console.log('DONE',names[k],((Date.now()-start)/1000).toFixed(1)+'s');
 }
}
main().catch(e=>{console.error(e);process.exitCode=1;});
