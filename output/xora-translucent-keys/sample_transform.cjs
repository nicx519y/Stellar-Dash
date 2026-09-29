// Sample the real WebConfig Transform algorithm locally for the rendered keys.
const fs=require('node:fs'),path=require('node:path'),vm=require('node:vm'),crypto=require('node:crypto');
const root=__dirname,repo=path.resolve(root,'../..');
const {transform}=require(path.join(repo,'application/www/node_modules/sucrase'));
function load(file,imports={}){
 const source=fs.readFileSync(path.join(repo,file),'utf8'),exports={};
 vm.runInNewContext(transform(source,{transforms:['typescript','imports']}).code,{exports,require:id=>{if(!(id in imports))throw Error(id);return imports[id]},Math,Set});
 return exports;
}
const {GamePadColor}=load('application/www/types/gamepad-color.ts');
const source='application/www/components/hitbox/hitbox-animation.ts';
const {transformAnimation}=load(source,{'@/types/gamepad-color':{GamePadColor},'@/types/gamepad-config':{LedsEffectStyle:{STATIC:0,BREATHING:1,STAR:2,FLOWING:3,RIPPLE:4,TRANSFORM:5}}});
const data=JSON.parse(fs.readFileSync(path.join(root,'layout-source.json'),'utf8'));
const layout=data.buttons.map(b=>({x:b.display_x,y:b.display_y,r:b.display_radius}));
// Full-width ambient band shares the same horizontal sweep as the keys.
layout.push({x:0,y:0,r:0},{x:data.width,y:0,r:0});
const blue=new GamePadColor(0,70,255),green=new GamePadColor(0,255,80);
const frames=[];
for(let f=0;f<120;f++){
 const progress=(f/12/2)%1;
 const colors=layout.map((_,index)=>{
  const color=transformAnimation({index,progress,backColor1:blue,backColor2:green,brightness:100,colorEnabled:true,pressed:false,frontColor:blue,defaultBackColor:blue,layout});
  return ['red','green','blue'].map(c=>color.getChannelValue(c));
 });
 frames.push(colors.slice(0,22));
}
const result={source,sha256:crypto.createHash('sha256').update(fs.readFileSync(path.join(repo,source))).digest('hex'),effect:'Transform / 质变',blue:[0,70,255],green:[0,255,80],cycle_seconds:2,speed:5,band_width:140,min_x:-100,max_x:data.width+100,frames};
fs.writeFileSync(path.join(root,'transform-samples.json'),JSON.stringify(result));
console.log('SAMPLED WebConfig Transform: 120 frames, 22 keys, blue/green, 2-second alternating sweep');
