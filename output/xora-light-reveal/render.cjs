// Animated native SVG logo: deterministic, local, no image upload or network.
const fs=require('node:fs'),path=require('node:path');
const sharp=require('../../application/www/node_modules/sharp');
const root=__dirname;
const svg=fs.readFileSync(path.join(root,'../../application/www/public/images/xora-mono-slate.svg'),'utf8');
const mark=svg.slice(svg.indexOf('<g '),svg.lastIndexOf('</svg>')).replaceAll('#9FA9AD','currentColor');
const names=['05-silver-diagonal','06-blue-horizon','07-aurora-ribbon','08-gold-emergence'];
const clamp=x=>Math.max(0,Math.min(1,x));
const smooth=x=>{x=clamp(x);return x*x*(3-2*x)};
const num=x=>Number(x.toFixed(3));
const use=(fill,extra='')=>`<g ${extra}><g transform="translate(25 53.6) scale(.27)" fill="none">${mark.replaceAll('currentColor',fill)}</g></g>`;
const g=(s,a=1,extra='')=>`<g opacity="${num(clamp(a))}" ${extra}>${s}</g>`;
const rect=(fill,extra='')=>`<rect width="320" height="172" fill="${fill}" ${extra}/>`;
const colors=['#b9e9ff','#52d9ff','#be98ff','#f4cf8d'];
function frame(k,t){
 const c=colors[k],fade=1-smooth((t-6.25)/1.5),entry=smooth(t/.65),p=smooth((t-.45)/4.4),q=clamp((t-.45)/4.4);
 const x=-60+440*q,y=146-122*q,r=280*q;
 let mask,beam,ambient;
 if(k===0){
  // Broad feathered diagonal wake; the narrow leading highlight remains local.
  mask=`<linearGradient id="reveal" gradientUnits="userSpaceOnUse" x1="${num(x-46)}" y1="0" x2="${num(x+28)}" y2="-16"><stop stop-color="white"/><stop offset="1" stop-color="black"/></linearGradient>`;
  beam=`<path d="M${num(x+24)} 28L${num(x-15)} 144" fill="none" stroke="${c}" stroke-width="22" filter="url(#wide)" opacity=".8"/><path d="M${num(x+24)} 28L${num(x-15)} 144" fill="none" stroke="#ffffff" stroke-width="5" filter="url(#blur)"/>`;
  ambient=`<ellipse cx="${num(x)}" cy="86" rx="22" ry="57" fill="${c}" filter="url(#wide)" opacity=".075"/>`;
 }else if(k===1){
  mask=`<linearGradient id="reveal" gradientUnits="userSpaceOnUse" x1="0" y1="${num(y+16)}" x2="0" y2="${num(y-16)}"><stop stop-color="white"/><stop offset="1" stop-color="black"/></linearGradient>`;
  beam=`<path d="M15 ${num(y)}H305" stroke="${c}" stroke-width="14" filter="url(#wide)"/><path d="M15 ${num(y)}H305" stroke="#c8f6ff" stroke-width="2.5" filter="url(#blur)"/>`;
  ambient=`<ellipse cx="160" cy="${num(y)}" rx="112" ry="4" fill="${c}" opacity=".24" filter="url(#blur)"/><path d="M28 ${num(y)}H292" stroke="${c}" stroke-width=".35" opacity=".2"/>`;
 }else if(k===2){
  mask=`<linearGradient id="reveal" gradientUnits="userSpaceOnUse" x1="${num(x-62)}" y1="130" x2="${num(x+36)}" y2="80"><stop stop-color="white"/><stop offset="1" stop-color="black"/></linearGradient>`;
  beam=`<path d="M${num(x-38)} 142C${num(x+80)} 98 ${num(x-60)} 80 ${num(x+20)} 30" fill="none" stroke="#ff95de" stroke-width="23" filter="url(#wide)"/><path d="M${num(x-32)} 142C${num(x+86)} 98 ${num(x-54)} 80 ${num(x+26)} 30" fill="none" stroke="#91eeff" stroke-width="6" filter="url(#blur)"/>`;
  ambient=g(beam,.045);
 }else{
  mask=`<radialGradient id="reveal" gradientUnits="userSpaceOnUse" cx="160" cy="86" r="${num(Math.max(1,r))}" gradientTransform="translate(0 -86) scale(1 2)"><stop offset="0" stop-color="white"/><stop offset=".62" stop-color="white"/><stop offset="1" stop-color="black"/></radialGradient>`;
  beam=`<ellipse cx="160" cy="86" rx="${num(Math.max(1,r*.86))}" ry="${num(Math.max(1,r*1.72))}" fill="none" stroke="${c}" stroke-width="13" filter="url(#wide)"/><ellipse cx="160" cy="86" rx="${num(Math.max(1,r*.86))}" ry="${num(Math.max(1,r*1.72))}" fill="none" stroke="#fff1d3" stroke-width="3" filter="url(#blur)"/>`;
  ambient=`<ellipse cx="160" cy="86" rx="${num(5+q*125)}" ry="38" fill="${c}" filter="url(#wide)" opacity="${num(.07*Math.sin(q*Math.PI))}"/>`;
 }
 const defs=`<defs><g id="mark" transform="translate(25 53.6) scale(.27)">${mark}</g>
 <linearGradient id="metal" x1="0" y1="0" x2=".1" y2="1"><stop stop-color="${k===3?'#fff1d7':'#edf7ff'}"/><stop offset=".42" stop-color="${k===3?'#d6bd93':k===2?'#bab6d8':'#a3becd'}"/><stop offset=".56" stop-color="${k===3?'#a58c64':'#6b8796'}"/><stop offset="1" stop-color="${k===3?'#f0dcc0':k===2?'#e9d5ef':'#dcebf1'}"/></linearGradient>
 <linearGradient id="aurora"><stop stop-color="#83e5f2"/><stop offset=".55" stop-color="#cbd1f5"/><stop offset="1" stop-color="#ecb9e4"/></linearGradient>
 <filter id="wide" x="-100%" y="-100%" width="300%" height="300%"><feGaussianBlur stdDeviation="6"/></filter>
 <filter id="blur" x="-100%" y="-100%" width="300%" height="300%"><feGaussianBlur stdDeviation="1.3"/></filter>
 ${mask}<mask id="wake" maskUnits="userSpaceOnUse" x="0" y="0" width="320" height="172">${rect('url(#reveal)')}</mask>
 <clipPath id="logo">${use('white')}</clipPath></defs>`;
 // All colored effects fade to black for a gentle, seamless loop.
 let body=g(ambient,1-smooth((t-4.4)/.7));
 const fill=k===2?'url(#aurora)':'url(#metal)';
 body+=g(use(c,'filter="url(#wide)" mask="url(#wake)"'),.11);
 body+=use(fill,'mask="url(#wake)"');
 body+=`<g clip-path="url(#logo)">${g(beam,1-smooth((t-4.6)/.65))}</g>`;
 // A restrained second reflection passes across the fully revealed mark.
 const hx=-60+440*clamp((t-4.75)/1.45);
 const hold=smooth((t-4.6)/.3)*(1-smooth((t-6)/.3));
 body+=`<g clip-path="url(#logo)" opacity="${num(hold*.3)}"><path d="M${num(hx+20)} 35L${num(hx-20)} 136" stroke="${c}" stroke-width="28" filter="url(#wide)"/></g>`;
 return `<svg xmlns="http://www.w3.org/2000/svg" width="960" height="516" viewBox="0 0 320 172">${defs}${rect('#020409')}${g(body,fade*entry)}</svg>`;
}
async function main(){
 for(let k=0;k<4;k++){
  let start=Date.now();console.log('START',names[k]);
  let dir=path.join(root,'frames',names[k]);fs.mkdirSync(dir,{recursive:true});
  for(let f=0;f<96;f++)await sharp(Buffer.from(frame(k,f/12))).resize(320,172).png().toFile(path.join(dir,String(f).padStart(3,'0')+'.png'));
  console.log('DONE',names[k],((Date.now()-start)/1000).toFixed(1)+'s');
 }
}
main().catch(e=>{console.error(e);process.exitCode=1});
