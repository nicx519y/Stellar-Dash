const test = require('node:test');
const assert = require('node:assert/strict');
const { decodeGameLink, gameLinkText, gameLinkIncidents } = require('../lib/game-link-logs.ts');
const { RxLogClient, rxLogRequest, rxLogCrc, decodeRxLog } = require('../lib/device-transport/rx-log-client.ts');
const { collectFaultLogs, logExport, logExportText } = require('../lib/device-fault-logs.ts');
const { mockFaultLogs } = require('../lib/mock-fault-logs.ts');

test('connection records preserve unknown versions and separate device clocks', async () => {
  const a = await collectFaultLogs(async q => mockFaultLogs(q, 'connection'), { source: 'tx' });
  const b = { ...a, source: 'rx' };
  assert.equal(decodeGameLink(a.items[0]).mode, 'USB');
  assert.match(gameLinkText(a.items[0], 'zh').description, /板间通信异常/);
  assert.match(gameLinkText(a.items[0], 'en').description, /Board link fault/);
  assert.equal(decodeGameLink({ ...a.items[0], data: [99] }), null);
  const groups = gameLinkIncidents([a, b]); assert.equal(groups.length, 14);
  assert.equal(groups[0].recovered, true); assert.equal(groups[0].incomplete, false);
  assert.equal(groups.at(-1).incomplete, true);
  assert.equal(gameLinkIncidents([{ ...a, dropped: 1 }])[0].incomplete, true);
  const exported = logExport([a,b], { source: 'read-sources', category: 0, severity: 0 });
  assert.equal(exported.sources.length, 2); assert.match(logExportText(exported), /Connection/);
});
test('confirmed disconnection is readable without changing legacy or unknown reason bits', () => {
  const data = [1, 1, 2, (1 << 16) | (1 << 8), 100, 5000, 0, 1, 1, 0, 5000, 5000, 8000, 0];
  const item = { event: 200, data };
  assert.match(gameLinkText(item, 'zh').description, /已确认断链/);
  assert.match(gameLinkText(item, 'en').description, /Confirmed disconnection/);
  data[3] |= 1 << 17;
  assert.match(gameLinkText(item, 'en').description, /0x20000/);
});
function crc32(bytes) { let c=0xffffffff; for(const b of bytes){c^=b;for(let i=0;i<8;i++)c=(c>>>1)^(c&1?0xedb88320:0);}return (~c)>>>0; }
function record(sequence=1) {
  const b=new Uint8Array(96),v=new DataView(b.buffer);
  v.setUint32(0,0x314c4658,true);v.setUint32(4,sequence,true);v.setUint32(8,3,true);
  v.setUint16(20,200,true);b[22]=4;b[23]=3;v.setUint32(24,1,true);b[28]=2;b[30]=14;
  [1,1,2,256].forEach((x,i)=>v.setUint32(32+i*4,x,true));
  v.setUint32(88,crc32(b.subarray(0,88)),true);v.setUint32(92,0xc04d17ed,true);return b;
}
class Device extends EventTarget {
  opened=true; mode='good'; ops=[]; count=1; reads=0; failReadAfter=Infinity;
  async sendFeatureReport(id, request) {
    assert.equal(id,0);const q=new DataView(request.buffer);this.ops.push(request[5]);
    if(this.mode==='timeout')return;
    if(request[5]===2 && ++this.reads>this.failReadAfter)throw new Error('read failed');
    const before=q.getUint32(16,true);
    const bytes=request[5]===1?new Uint8Array(36):request[5]===2?record(before?before-1:this.count):new Uint8Array(0);
    if(request[5]===1){const v=new DataView(bytes.buffer);v.setUint32(0,1,true);v.setUint32(4,this.count,true);v.setUint32(8,1,true);v.setUint32(12,this.count,true);}
    const pages=Math.max(1,Math.ceil(bytes.length/16));
    for(let page=pages-1;page>=0;page--){
      const b=new Uint8Array(32),v=new DataView(b.buffer);
      v.setUint32(0,0x31474c52,true);b[4]=1;b[5]=request[5];b[7]=page;
      v.setUint32(8,q.getUint32(8,true)+(this.mode==='stale'?1:0),true);
      b.set(bytes.subarray(page*16,page*16+16),12);b[28]=pages;b[29]=bytes.length;
      v.setUint16(30,rxLogCrc(b.subarray(0,30)),true);if(this.mode==='corrupt')b[12]^=1;
      const e=new Event('inputreport');e.reportId=0;e.data=v;this.dispatchEvent(e);
    }
  }
}
test('RX request CRC matches portable C vector and confirms clear explicitly', () => {
  const q=rxLogRequest(2,0x12345678);assert.equal(rxLogCrc(q.subarray(0,12)),0x5914);
  assert.equal(new DataView(rxLogRequest(4,1).buffer).getUint32(20,true),0x434c5231);
  const b=record();assert.equal(decodeRxLog(b).event,200);b[45]^=1;assert.throws(()=>decodeRxLog(b),/checksum/);
});
test('RX fragmented snapshot supports reverse pages and bounds cursor', async () => {
  const device=new Device(),client=new RxLogClient(device,30);
  const p=await collectFaultLogs(client.fetchPage,{source:'rx'});
  assert.equal(p.status,'ok');assert.equal(p.items.length,1);assert.equal(p.items[0].boot,3);
  await client.clear();await client.request(3);assert.deepEqual(device.ops,[1,2,4,3]);
});
test('RX corrupt, stale, busy and unsupported responses cannot become empty success', async () => {
  const device=new Device(),client=new RxLogClient(device,20);
  device.mode='corrupt';await assert.rejects(client.request(1),/Invalid/);
  device.mode='stale';await assert.rejects(client.request(1),/timeout/);
  device.mode='timeout';const first=client.request(1);await assert.rejects(client.request(1),/busy/);await assert.rejects(first,/timeout/);
  const p=await collectFaultLogs(client.fetchPage,{source:'rx'});assert.notEqual(p.status,'ok');
});
test('RX keeps records already read on failure and stops between records on cancellation', async () => {
  for(const cancel of [false,true]) {
    const device=new Device();device.count=3;if(!cancel)device.failReadAfter=1;
    const client=new RxLogClient(device,20);client.cancelled=()=>cancel && device.reads>=1;
    const page=await collectFaultLogs(client.fetchPage,{source:'rx'});
    assert.equal(page.items.length,1);assert.equal(page.items[0].sequence,3);assert.equal(page.status,'unavailable');
    assert.equal(logExport([page],{source:'rx',category:0,severity:0}).complete,false);
    if(cancel)assert.equal(device.reads,1);
  }
});
