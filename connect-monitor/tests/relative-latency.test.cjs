const {test}=require('node:test');
const assert=require('node:assert/strict');
const {RelativeLatencyDecoder}=require('../dist/electron/sources/relative-latency.js');
const {parseDongleHidTelemetryFrame}=require('../dist/electron/sources/dongle-hid-telemetry-source.js');
function page(row,rev,part,flags,values,previous=0){
 const b=Buffer.alloc(32);b.writeUInt32LE(0x32544c52);b.writeUInt16LE(1,4);b.writeUInt16LE(row,6);
 b[8]=1;b[11]=(rev<<1)|part;b[12]=flags;b[13]=previous;
 values.forEach((v,i)=>b.writeUInt32LE(v,16+i*4));return b;
}
test('relative durations are independent of delayed HID delivery; incomplete stays partial',()=>{
 const d=new RelativeLatencyDecoder();
 let e=d.parse(page(1,0,1,4,[0,108,30,900]),100)[0];
 assert.equal(e.latencyMs,null);assert.deepEqual(e.relativeStagesUs,[null,null,null,null,null,108,30,900]);
 assert.equal(e.measurementReason,'Source record missing');
 e=d.parse(page(1,1,1,36,[0,108,30,900]),200)[0];
 assert.equal(e.measurementReason,'TX attempt mismatch');assert.equal(e.latencyMs,null);
 d.parse(page(1,2,0,7,[10,20,30,40]),10000);
 e=d.parse(page(1,2,1,7,[50,108,30,900]),999999)[0];
 assert.equal(e.latencyMs,1.188);assert.equal(e.measurement,'usb');assert.equal(e.timestampMs,100);
 assert.equal(e.traceId,'usb:0:1:1');
 assert.equal(d.parse(page(1,0,1,4,[0,108,0,900]),1000000).length,0);
});
test('out-of-order pages, record identity, missing stages and reset cannot mix',()=>{
 const d=new RelativeLatencyDecoder();
 const a=d.parse(page(1,2,1,7,[1,2,3,4]))[0];assert.equal(a.latencyMs,null);
 const b=d.parse(page(2,2,0,7,[10,20,30,40],1))[0];assert.equal(b.latencyMs,null);
 assert.equal(b.previousStandardMask,b.standardMask);
 assert.equal(d.parse(page(1,2,0,7,[1,2,3,4]))[0].latencyMs,.02);
 d.reset();const c=d.parse(page(1,2,1,7,[1,2,3,4]))[0];assert.notEqual(c.traceId,a.traceId);assert.equal(c.latencyMs,null);
 const bad=page(3,1,0,15,[1,2,3,4]);d.parse(bad);
 assert.equal(d.parse(page(3,1,1,15,[1,2,3,4]))[0].latencyMs,null);
 assert.equal(d.parse(Buffer.alloc(32)),null);
});
test('short packet counters never masquerade as input throughput',()=>{
 const b=Buffer.alloc(32);b.writeUInt32LE(0x32504852);[1,10,20,30,40,50,60].forEach((v,i)=>b.writeUInt32LE(v,4+4*i));
 const p=parseDongleHidTelemetryFrame(b,123)[0];
 assert.equal(p.messageType,'RFH_RHP2');assert.equal(p.rfTx5ByteTotal,10);assert.equal(p.rfTx7ByteTotal,20);
 assert.equal(p.rfTx12ByteTotal,30);assert.equal(p.rfAckReservedSlots,40);assert.equal(p.rfControlGuardSlots,50);
 assert.equal(p.rfTraceOverwrites,60);assert.equal(p.sampleCount,undefined);assert.equal(p.rateHz,undefined);
});

test('valid source stages remain visible when TX physical boundary is unavailable',()=>{
 const d=new RelativeLatencyDecoder();
 d.parse(page(1,0,0,37,[92,37,4,9]));
 const e=d.parse(page(1,0,1,37,[0xffffff,92,120,1800]))[0];
 assert.deepEqual(e.relativeStagesUs,[92,37,4,9,null,92,120,1800]);
 assert.equal(e.measurementReason,'TX timing unavailable');assert.equal(e.latencyMs,null);
});

test('source timeout keeps RX/USB durations and source diagnostics are not throughput',()=>{
 const d=new RelativeLatencyDecoder();
 const e=d.parse(page(1,1,1,4|64,[0,92,180,200]))[0];
 assert.equal(e.measurementReason,'Source record timeout');assert.equal(e.latencyMs,null);
 assert.deepEqual(e.relativeStagesUs,[null,null,null,null,null,92,180,200]);
 const b=Buffer.alloc(32);b.writeUInt32LE(0x31534c52);
 [6,2,1,0,1,3,4].forEach((n,i)=>b.writeUInt32LE(n,4+i*4));
 const p=parseDongleHidTelemetryFrame(b,123)[0];
 assert.equal(p.messageType,'RFH_RLS1');assert.equal(p.rfSourceReceived,6);
 assert.equal(p.rfSourceMatched,2);assert.equal(p.rfSourceExpired,1);
 assert.equal(p.rfSourceBoundaryMissing,1);assert.equal(p.rfSourceSpiDrops,3);
 assert.equal(p.rfSourceIdentityWaits,4);assert.equal(p.sampleCount,undefined);
 assert.equal(p.rateHz,undefined);
});
