const test = require('node:test');
const assert = require('node:assert/strict');
const { HitboxLightingPreview, factoryLightSource, ambientBorderColor } = require('../lib/hitbox-lighting-preview.ts');
const options = {
  keyEnabled:true, ambientEnabled:true, sync:false, oneShot:false,
  keyColors:[0xff0000,0x0000ff,0x00ff00], ambientColors:[0x00ff00,0xff0000,0x0000ff],
  keySpeed:3, ambientSpeed:3, keyBrightness:100, ambientBrightness:100, disabledKeys:[],
};
const preview = (key=0, ambient=0) => new HitboxLightingPreview(factoryLightSource(false,key),factoryLightSource(true,ambient),0);
test('ambient remains animated with key lighting disabled, and disabling ambient clears only the border', () => {
  const engine=preview(0,1);
  const first=engine.render(0,0,0,{...options,keyEnabled:false});
  const next=engine.render(600,0,0,{...options,keyEnabled:false});
  assert.ok(next.keys.every(c=>c.every(v=>v===0)));
  assert.notDeepEqual(first.ambient,next.ambient);
  const off=engine.render(700,0,0,{...options,ambientEnabled:false});
  assert.ok(off.ambient.every(c=>c.every(v=>v===0)));
  assert.ok(off.keys.some(c=>c.some(v=>v>0)));
});
test('sync uses the key engine palette and clock, with independent strip brightness', () => {
  const a=preview(3,0), b=preview(3,3);
  const first=a.render(800,0,0,{...options,sync:true});
  const second=b.render(800,0,0,{...options,sync:true,ambientColors:[0,0,0],ambientSpeed:1,ambientBrightness:50});
  assert.deepEqual(first.keys,second.keys);
  assert.deepEqual(second.ambient,first.ambient.map(c=>c.map(v=>Math.round(v*.5))));
});
test('pointer and hardware events trigger the same single-shot ambient animation and disabled keys cannot trigger', () => {
  const pointer=preview(4,3), hardware=preview(4,3), ignored=preview(4,3);
  const opts={...options,oneShot:true};
  for(const engine of [pointer,hardware,ignored]) engine.render(2500,0,0,opts);
  pointer.render(2600,1,0,opts);hardware.render(2600,0,1,opts);ignored.render(2600,1,1,{...opts,disabledKeys:[0]});
  const left=pointer.render(3000,0,0,opts),right=hardware.render(3000,0,0,opts);
  assert.deepEqual(left,right);
  assert.notDeepEqual(left.ambient,ignored.render(3000,0,0,opts).ambient);
  assert.deepEqual(pointer.render(6000,0,0,opts).ambient,ignored.render(6000,0,0,opts).ambient);
});
test('border smoothly interpolates colors and joins its last and first samples without a gap', () => {
  const colors=Array.from({length:40},(_,i)=>[i*6,255-i*6,0]);
  assert.deepEqual(ambientBorderColor(colors,0),colors[0]);
  assert.deepEqual(ambientBorderColor(colors,3),colors[39]);
  assert.deepEqual(ambientBorderColor(colors,3.5),[117,138,0]);
  assert.deepEqual(ambientBorderColor(colors,4),colors[0]);
  assert.ok(ambientBorderColor(colors,3.999).every((value,i)=>Math.abs(value-colors[0][i])<=1));
});
