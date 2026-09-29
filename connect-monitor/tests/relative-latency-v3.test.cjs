// Prepared for manual regression after the RF/monitor test pause is lifted.
const {test}=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs'),path=require('node:path'),ts=require('typescript');
const {RelativeLatencyDecoder}=require('../dist/electron/sources/relative-latency.js');
function page(part,values,{revision=0,flags=7,previous=0,legacy=false}={}) {
  const b=Buffer.alloc(32);b.write(legacy?'RLT2':'RLT3');
  b.writeUInt16LE(1,4);b.writeUInt16LE(1,6);b[8]=1;b[13]=previous;
  b[11]=(revision<<(legacy?1:2))|part;b[12]=flags;
  values.forEach((v,i)=>b.writeUInt32LE(v,16+i*4));return b;
}
function loadRenderer(name) {
  const code=ts.transpileModule(fs.readFileSync(path.join(__dirname,'../renderer/src/ui',name+'.ts'),'utf8'),{
    compilerOptions:{module:ts.ModuleKind.CommonJS,target:ts.ScriptTarget.ES2022}}).outputText;
  const m={exports:{}};new Function('require','module','exports',code)(id=>id.startsWith('./')?loadRenderer(id.slice(2)):require(id),m,m.exports);return m.exports;
}
test('RLT3 sums all nine stages only after three matching pages, regardless of delivery order',()=>{
  const d=new RelativeLatencyDecoder();
  let e=d.parse(Buffer.concat([Buffer.from([0]),page(2,[110])]),100)[0];
  assert.equal(e.latencyMs,null);assert.equal(e.relativeStagesUs[8],110);
  e=d.parse(page(0,[10,20,30,40]),200)[0];assert.equal(e.latencyMs,null);
  e=d.parse(page(1,[50,100,60,300]),10000)[0];
  assert.equal(e.latencyMs,.720);assert.equal(e.timestampMs,100);
  assert.equal(e.latencyFrame,'RLT3');assert.deepEqual(e.relativeStagesUs,[10,20,30,40,50,100,60,300,110]);
  const row=loadRenderer('latencyTableModel').buildLatencyTableSnapshot([e],null).rows[0];
  assert.deepEqual(row.relativeTexts.slice(7),['300us','110us']);
});
test('revision wrap, missing page, stale revision, identity mismatch and invalid page cannot fabricate a split',()=>{
  const d=new RelativeLatencyDecoder();
  d.parse(page(0,[1,2,3,4],{revision:63}));
  d.parse(page(1,[5,6,7,8],{revision:63}));
  let e=d.parse(page(2,[9],{revision:0}))[0];
  assert.equal(e.latencyMs,null);assert.equal(e.relativeStagesUs[0],null);
  assert.deepEqual(d.parse(page(0,[1,2,3,4],{revision:63})),[]);
  assert.deepEqual(d.parse(page(0,[1,2,3,4],{previous:2})),[]);
  assert.deepEqual(d.parse(page(3,[9])),[]);
  d.parse(page(0,[1,2,3,4]));e=d.parse(page(1,[5,6,7,8]))[0];
  assert.equal(e.latencyMs,.045);
  const before=e.traceId;d.reset();e=d.parse(page(2,[9]))[0];
  assert.notEqual(e.traceId,before);assert.equal(e.latencyMs,null);
});
test('USB completion flag and sentinel gate both split stages, while true edges remain visible',()=>{
  const d=new RelativeLatencyDecoder();
  let e=d.parse(page(1,[50,100,60,300],{flags:3}))[0];
  assert.equal(e.relativeStagesUs[7],null);
  e=d.parse(page(2,[110],{flags:3}))[0];assert.equal(e.relativeStagesUs[8],null);
  d.parse(page(0,[1,2,3,4],{revision:1}));d.parse(page(1,[5,6,7,8],{revision:1}));
  e=d.parse(page(2,[0xffffffff],{revision:1}))[0];assert.equal(e.latencyMs,null);
  assert.equal(e.relativeStagesUs[8],null);
});
test('legacy combined USB stays in history and total but never appears as split USB',()=>{
  const d=new RelativeLatencyDecoder();
  d.parse(page(0,[10,20,30,40],{legacy:true}));
  const e=d.parse(page(1,[50,100,60,410],{legacy:true}))[0];
  assert.equal(e.latencyMs,.720);assert.equal(e.relativeStagesUs[7],410);
  const build=loadRenderer('latencyTableModel').buildLatencyTableSnapshot;
  for(const event of [e,{...e,latencyFrame:undefined}]) {
    const row=build([event],null).rows[0];
    assert.deepEqual(row.relativeTexts.slice(7),['-','-']);
    assert.match(row.totalText,/旧版 USB 未拆分/);
  }
  assert.equal(d.parse(page(2,[110]))[0].latencyMs,null);
});
test('wired USB continues to expose wait and in-flight independently with the same total',()=>{
  const row={kind:'button_latency',sourceMode:'USB',measurement:'usb',standardMask:1,previousStandardMask:0,
    latencyFrame:'UME1',relativeStagesUs:[10,20,30,40,300,110,null,null],latencyMs:null};
  const view=loadRenderer('latencyTableModel').buildLatencyTableSnapshot([row],null);
  assert.deepEqual(view.rows[0].relativeTexts.slice(4,6),['300us','110us']);
  assert.equal(view.rows[0].totalText,'0.510ms');
});
