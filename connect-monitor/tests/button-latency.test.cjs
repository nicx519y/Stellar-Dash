const test = require('node:test');
const assert = require('node:assert/strict');
const {ButtonLatencyTracker} = require('../dist/electron/sources/button-latency-source');
const {parseDongleHidTelemetryFrame:parse} = require('../dist/electron/sources/dongle-hid-telemetry-source');
const fs = require('node:fs'), path = require('node:path'), ts = require('typescript');
function fixture() {
  const tracker = new ButtonLatencyTracker(), out=[];
  const publish=e=>out.push(e);
  const poll=(mask,time,prev=time-1000,slots=1)=>tracker.handleNativePoll(Array.from({length:slots},(_,slot)=>({slot,
    standardMask:mask,beforeUs:time,afterUs:time+10,previousBeforeUs:prev})),time+10,publish);
  const sync=(seq=1,receive=1000,send=1020,start=11000,end=11220)=>{
    tracker.noteTimeSyncSent(seq,start);
    tracker.handleTracePacket({messageType:'RFH_RHC3',syncSeq:seq,syncRxTickUs:receive,syncTxTickUs:send,hostMonoUs:end,traceDrops:0},publish);
  };
  const edge=(seq,mask,sample,time=14000,baseline=false,drops=0)=>tracker.handleTracePacket({messageType:'RFH_RHE3',
    inputSeq:seq,inputKeyMask:mask,sampleTickUs:sample,hostMonoUs:time,traceBaseline:baseline,traceDrops:drops},publish);
  const rows=()=>out.filter(e=>e.kind==='button_latency' && e.measurement==='windows');
  sync(); poll(0,11500); edge(1,0,1000,12000,true);
  return {tracker,out,publish,poll,sync,edge,rows};
}
test('sampling to first Windows observation returns conservative bounds, not stage sum',()=>{
  const f=fixture(); f.poll(1<<13,13000,12500); f.edge(2,2,2000);
  f.poll(1<<13,200000);
  const [r]=f.rows(); assert.equal(f.rows().length,1); assert.equal(r.keyMask,2);
  assert.equal(r.action,'press'); assert.ok(r.latencyMinMs<0.3); assert.ok(r.latencyMaxMs>1);
  assert.equal(r.pollIntervalUs,510); assert.equal(r.xinputPcUs,13010);
});
test('duplicate metadata and held Windows state do not manufacture samples',()=>{
  const f=fixture(); f.edge(2,2,2000); f.edge(2,2,2000); f.poll(1<<13,13000,12500); f.poll(1<<13,200000);
  f.poll(1<<13,220000); assert.equal(f.rows().length,1);
});
test('sequence gap establishes baseline, not false latency',()=>{
  const f=fixture(); f.edge(3,2,2000); f.poll(1<<13,13000,12500); f.poll(1<<13,200000);
  assert.equal(f.rows().length,0);
});
test('out of order trace cannot advance state',()=>{
  const f=fixture(); f.edge(2,2,2000); f.edge(1,0,1000); f.poll(1<<13,13000,12500); f.poll(1<<13,200000);
  assert.equal(f.rows().length,1);
});
test('two possible Windows press transitions are ambiguous',()=>{
  const f=fixture(); f.edge(2,2,2000);
  f.poll(1<<13,13000,12500); f.poll(0,16000,15000); f.poll(1<<13,19000,18000); f.poll(1<<13,200000);
  assert.equal(f.rows().length,0);
});
test('two origin edges cannot consume one Windows transition',()=>{
  const f=fixture(); f.edge(2,2,2000); f.edge(3,0,3000); f.edge(4,2,4000);
  f.poll(1<<13,16000,15000); f.poll(1<<13,220000); assert.equal(f.rows().length,0);
});
test('identical transitions on two controllers remain ambiguous',()=>{
  const f=fixture(); f.poll(0,12000,11000,2); f.edge(2,2,2000); f.poll(1<<13,14000,13000,2);
  f.poll(1<<13,200000,140000,2); assert.equal(f.rows().length,0);
});
test('unplug/reconnect does not treat the initial held state as a fresh edge',()=>{
  const f=fixture(); f.edge(2,2,2000); f.poll(0,12500,12000,0); f.poll(1<<13,13000);
  f.poll(1<<13,200000); assert.equal(f.rows().length,0);
});
test('stale clock and long polling stalls produce no precise result',()=>{
  const f=fixture(); f.edge(2,2,2000); f.poll(1<<13,50000,12000); f.poll(1<<13,200000);
  assert.equal(f.rows().length,0);
  f.edge(3,0,9_000_000,9_020_000); f.poll(0,9_030_000); f.poll(0,9_200_000);
  assert.equal(f.rows().length,0);
});
test('8-bit event sequence wraps without interpreting repeats as loss',()=>{
  const f=fixture(); f.edge(255,0,1500,12500,true); f.edge(0,2,2000);
  f.poll(1<<13,13000,12500); f.poll(1<<13,200000); assert.equal(f.rows().length,1);
});
test('32-bit timestamp wrap keeps clock and edge domains continuous',()=>{
  const f=fixture(); f.tracker.reset();
  f.sync(2,0xfffffff0,0xfffffff8,0x100000000+9984,0x100000000+10200);
  f.poll(0,0x100000000+10300); f.edge(10,0,0xfffffff9,0x100000000+10400,true);
  f.edge(11,2,0x100,0x100000000+12000);
  f.poll(1<<13,0x100000000+11500,0x100000000+11000);
  f.poll(1<<13,0x100000000+200000); assert.equal(f.rows().length,1);
});
test('legacy saturated stages are explicitly low confidence and never Windows results',()=>{
  const f=fixture(); f.tracker.handleLatencyPacket({messageType:'RFH_RHL2',latencyUs:7050,inputKeyMask:2,
    latencyStm32Us:6016,latencyStageFlags:3},f.publish);
  const row=f.out.find(e=>e.measurement==='stages'); assert.equal(row.latencyStageFlags,3); assert.equal(row.confidence,'low');
  assert.equal(f.rows().length,0);
});
test('v3 CRC/version checks and timestamp semantics are separate from legacy duration',()=>{
  const b=Buffer.alloc(32); b.write('RHE3'); b[4]=3; b[5]=7; b.writeUInt32LE(0x80000002,8); b.writeUInt32LE(8000000,12);
  let crc=0;for(let i=0;i<31;i++){crc^=b[i];for(let bit=0;bit<8;bit++)crc=((crc<<1)^((crc&128)?7:0))&255;}b[31]=crc;
  const [p]=parse(b,1,123); assert.equal(p.sampleTickUs,8000000); assert.equal(p.latencyUs,undefined);
  b[12]^=1; assert.deepEqual(parse(b),[]);
});
function loadRenderer(name) {
  const code=ts.transpileModule(fs.readFileSync(path.join(__dirname,'../renderer/src/ui',name+'.ts'),'utf8'),{
    compilerOptions:{module:ts.ModuleKind.CommonJS,target:ts.ScriptTarget.ES2022}}).outputText;
  const m={exports:{}}; new Function('require','module','exports',code)(id=>id.startsWith('./')?loadRenderer(id.slice(2)):require(id),m,m.exports);return m.exports;
}
test('UI excludes legacy/saturated stages from Windows total and aggregate',()=>{
  const {buildLatencyTableSnapshot}=loadRenderer('latencyTableModel');
  const row={timestampMs:1,inputSeq:1,standardMask:8192,previousStandardMask:0,stm32Ms:6.016,latencyMs:7,
    latencyStageFlags:3,measurement:'stages'};
  const view=buildLatencyTableSnapshot([row],null);
  assert.match(view.rows[0].stm32Text,/SAT/); assert.equal(view.rows[0].totalText,'—');
  assert.equal(view.headerText,'No Windows measurement');
});

test('partial USB rows do not incorrectly tell the user capture is disabled',()=>{
  const {buildLatencyTableSnapshot}=loadRenderer('latencyTableModel');
  const row={timestampMs:1,inputSeq:1,standardMask:8192,previousStandardMask:0,latencyMs:null,
    measurement:'usb',relativeStagesUs:[null,null,null,null,null,92,1800,1900],measurementReason:'Source record missing'};
  const view=buildLatencyTableSnapshot([row],null);
  assert.match(view.statusText,/1 partial samples/);assert.doesNotMatch(view.statusText,/enable/i);
  assert.equal(view.rows[0].totalText,'Source record missing');
  assert.equal(view.headerText,'No complete USB measurement');
});

test('late origin metadata is rejected rather than matched to a later press',()=>{
  const f=fixture(); f.poll(1<<13,13000,12500); f.edge(2,2,2000,70000);
  f.poll(1<<13,220000); assert.equal(f.rows().length,0);
});

test('unsynchronized edges remain visible with a reason and no invented duration',()=>{
  const f=fixture(); f.tracker.reset(); f.poll(0,11500); f.edge(1,0,1000,12000,true); f.edge(2,2,2000);
  const row=f.out.find(e=>e.measurement==='trace');assert.equal(row.measurementReason,'Syncing');assert.equal(row.latencyMs,null);
  const view=loadRenderer('latencyTableModel').buildLatencyTableSnapshot([row],null);
  assert.equal(view.rows[0].totalText,'Syncing');assert.equal(view.headerText,'No Windows measurement');
});
test('matched result keeps the same trace ID as its pending row',()=>{
  const f=fixture();f.poll(1<<13,13000,12500);f.edge(2,2,2000);f.poll(1<<13,220000);
  const pending=f.out.find(e=>e.measurement==='trace');assert.equal(pending.traceId,f.rows()[0].traceId);
});

test('sync acquisition is faster only while the clock is unavailable',()=>{
 const f=fixture();assert.equal(f.tracker.syncIntervalMs(12000),137);
 assert.equal(f.tracker.syncIntervalMs(9000000),17);f.tracker.reset();assert.equal(f.tracker.syncIntervalMs(12000),17);
});

test('measured RX residence removes ACK-window wait without assuming symmetric paths',()=>{
 const t=new ButtonLatencyTracker(),out=[];t.noteTimeSyncSent(1,10000);
 t.handleTracePacket({messageType:'RFH_RHC4',syncSeq:1,syncRxTickUs:100000,syncTxTickUs:100020,
  syncQueueWaitUs:99000,hostMonoUs:110500,traceDrops:0},e=>out.push(e));
 assert.equal(out.at(-1).status,'Locked');assert.ok(out.at(-1).clockWidthUs<1700);
 const rejected=[];t.noteTimeSyncSent(2,120000);
 t.handleTracePacket({messageType:'RFH_RHC4',syncSeq:2,syncRxTickUs:121000,syncTxTickUs:121020,
  syncQueueWaitUs:99000,hostMonoUs:122000,traceDrops:0},e=>rejected.push(e));assert.equal(rejected.length,0);
});
test('RHC4 queue residence is CRC/version protected and stays separate from RHE3',()=>{
 const b=Buffer.alloc(32);b.write('RHC4');b[4]=4;b[5]=9;b.writeUInt32LE(99000,20);
 let c=0;for(let i=0;i<31;i++){c^=b[i];for(let j=0;j<8;j++)c=((c<<1)^((c&128)?7:0))&255;}b[31]=c;
 const [e]=parse(b,1,2);assert.equal(e.messageType,'RFH_RHC4');assert.equal(e.syncQueueWaitUs,99000);
 b[20]^=1;assert.deepEqual(parse(b),[]);
});

test('an idle second controller does not discard the unique matching transition',()=>{
 const f=fixture();
 const poll=(mask,t)=>f.tracker.handleNativePoll([
  {slot:0,standardMask:0,beforeUs:t,afterUs:t+10,previousBeforeUs:t-500},
  {slot:1,standardMask:mask,beforeUs:t,afterUs:t+10,previousBeforeUs:t-500}],t+10,f.publish);
 poll(0,12000); poll(1<<13,13000); f.edge(2,2,2000); poll(1<<13,200000);
 assert.equal(f.rows().length,1);
});
