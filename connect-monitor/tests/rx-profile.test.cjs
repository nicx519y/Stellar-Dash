const test=require('node:test');const assert=require('node:assert/strict');
const {RxProfileDecoder}=require('../dist/electron/sources/rx-profile.js');
const {rxQuantile,rxProfileDelta}=require('../dist/shared/rx-profile.js');
function frames(seq=1){
  const words=Array(210).fill(0);words[0]=60000000;words[1]=1000000;words[2]=2;words[4]=4;words[5]=1;words[10]=60;
  words[12]=8000;words[26]=1;words[27]=7;words[29]=1;
  return Array.from({length:35},(_,page)=>{const b=Buffer.alloc(32);b.writeUInt32LE(0x31505852);b.writeUInt16LE(seq,4);b[6]=page;b[7]=1;
    for(let i=0;i<6;i++)b.writeUInt32LE(words[page*6+i],8+i*4);return new DataView(b.buffer,b.byteOffset,32);});
}
test('complete snapshot contains exact counters and quantile bounds',()=>{
  const d=new RxProfileDecoder();let out;
  frames().forEach((v,i)=>{out=d.parse(v,i);if(i<34)assert.equal(out,undefined);});
  assert.equal(out.counters.received,8000);assert.equal(out.speed,2);assert.equal(rxQuantile(out.timings.rearm,.99),8);
  const next={...out,cycles:120000000,counters:{...out.counters,received:16000}};
  assert.equal(rxProfileDelta(out,next).delta.received,8000);
});
test('missing, reordered, invalid and cross-reset pages never fill with zeros',()=>{
  for(const mode of ['missing','reordered','reset','expired','version']){
    const d=new RxProfileDecoder(),f=frames();let complete;
    f.forEach((v,i)=>{
      if(mode==='missing'&&i===10)return;
      if(mode==='reordered'&&i===10)v=f[11];
      if(mode==='reset'&&i===10)d.reset();
      if(mode==='version')v.setUint8(7,2);
      complete=d.parse(v,mode==='expired'?i*200:i)||complete;
    });assert.equal(complete,undefined,mode);
  }
});
test('new page zero discards partial old capture and reset counters cannot imply huge rate',()=>{
  const d=new RxProfileDecoder();frames().slice(0,5).forEach(v=>d.parse(v,0));let out;
  frames(2).forEach(v=>out=d.parse(v,10)||out);assert.equal(out.snapshot,2);
  const reset={...out,cycles:out.cycles+60000000,counters:{...out.counters,received:0}};
  assert.equal(rxProfileDelta(out,reset),null);
});
