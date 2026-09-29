// Offline scenarios for the USB producer/decoder contract. Do not run while
// the repository's monitor regression pause is in force.
const {test}=require('node:test');
const assert=require('node:assert/strict');
const {UsbMonitorPeer}=require('../dist/electron/sources/usb-monitor.js');
function fixture() { const events=[];return {events,peer:new UsbMonitorPeer({},e=>events.push(e))}; }
function stats(time,done,session=1,received=done) {
  const b=Buffer.alloc(32);b.writeUInt32LE(0x31534d55);b[4]=1;b[5]=3;b[6]=2;
  [session,time,received,done,0].forEach((n,i)=>b.writeUInt32LE(n>>>0,8+4*i));b.writeUInt16LE(8000,28);return b;
}
function edge(page,revision=0,flags=7,session=1,previous=0,mask=1) {
  const b=Buffer.alloc(32);b.writeUInt32LE(0x31454d55);b[4]=1;b[5]=page;b[6]=revision;b[7]=flags;
  [session,1,previous,mask].forEach((v,i)=>b.writeUInt32LE(v,8+4*i));
  const values=[20,30,5,15,40,125,230,290];b.writeUInt32LE(values[page*2],24);b.writeUInt32LE(values[page*2+1],28);return b;
}
const latest=(events,kind)=>events.filter(e=>e.kind===kind).at(-1);
test('rate uses device completion counts and time, including a valid zero window',()=>{
  const {peer,events}=fixture();peer.parse(stats(100000,0),1000);
  assert.equal(latest(events,'usb_statistics').rateHz,null);
  peer.parse(stats(350000,2000),1300);assert.equal(latest(events,'usb_statistics').rateHz,8000);
  peer.parse(stats(600000,2000),1600);assert.equal(latest(events,'usb_statistics').rateHz,0);
});
test('counter wrap works; reset and stale delivery invalidate the rate',()=>{
  const {peer,events}=fixture();peer.parse(stats(0xffff0000,0xfffffff0),1000);
  peer.parse(stats((0xffff0000+250000)>>>0,(0xfffffff0+2000)>>>0),1250);
  assert.equal(latest(events,'usb_statistics').rateHz,8000);
  peer.parse(stats(100,1,2),1400);assert.equal(latest(events,'usb_statistics').rateHz,null);
  peer.parse(stats(250100,2001,2),5000);assert.equal(latest(events,'usb_statistics').rateHz,null);
  peer.expire(8001);assert.equal(latest(events,'device_status').rateValid,false);
});
test('pages update one real edge, preserve bounds, and reject stale revisions',()=>{
  const {peer,events}=fixture();peer.parse(edge(3),1000);
  const first=latest(events,'button_latency');assert.equal(first.latencyMs,null);
  [0,2,1].forEach(p=>peer.parse(edge(p),1100));
  const complete=latest(events,'button_latency');assert.equal(complete.traceId,first.traceId);
  assert.equal(complete.latencyMinMs,.23);assert.equal(complete.latencyMaxMs,.29);
  assert.equal(complete.sourceMode,'USB');assert.equal(complete.action,'press');
  peer.parse(edge(0,1,15),1200);assert.equal(latest(events,'button_latency').latencyMs,null);
  const count=events.length;peer.parse(edge(1,0),1300);assert.equal(events.length,count);
});
test('missing/stale pages and another device never form a complete measurement',()=>{
  const a=fixture(),b=fixture();a.peer.parse(edge(0),1000);a.peer.parse(edge(1),1000);
  b.peer.parse(edge(2),1000);b.peer.parse(edge(3),1000);
  assert.equal(latest(a.events,'button_latency').latencyMs,null);
  assert.equal(latest(b.events,'button_latency').latencyMs,null);
  assert.notEqual(latest(a.events,'button_latency').traceId,latest(b.events,'button_latency').traceId);
  a.peer.parse(edge(2),4000);a.peer.parse(edge(3),4000);
  assert.equal(latest(a.events,'button_latency').latencyMs,null);
});
test('32/33-byte reports, bad version, no edge, and release',()=>{
  const {peer,events}=fixture();assert.equal(peer.parse(Buffer.concat([Buffer.from([0]),stats(1,1)]),1000),true);
  const bad=stats(2,2);bad[4]=2;assert.equal(peer.parse(bad,1001),false);
  const count=events.length;peer.parse(edge(0,0,7,1,1,1),1100);assert.equal(events.length,count);
  peer.parse(edge(0,0,1,1,1,0),1200);assert.equal(latest(events,'button_latency').action,'release');
});
