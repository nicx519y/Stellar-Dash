// User-run after build: node --test tests/rf-tx-metrics.test.cjs
const {test}=require('node:test');const assert=require('node:assert/strict');
const {parseTxMetrics}=require('../dist/electron/sources/rf-tx-metrics.js');
const {txMetricsDelta,txMetricKeys}=require('../dist/shared/rf-tx-metrics.js');
function page(id,n,time,values,span=2000000){
  const b=new ArrayBuffer(32),v=new DataView(b);v.setUint16(4,id,true);v.setUint8(6,n);v.setUint8(7,1);
  v.setUint32(8,time,true);v.setUint32(12,span,true);
  for(let i=0;i<4;i++)v.setUint32(16+4*i,values[n*4+i]||0,true);return v;
}
test('missing, shuffled and duplicate USB pages never fabricate counts',()=>{
  const counts=Array.from({length:20},(_,i)=>i+300);
  for(const n of [3,0,1,1,4])assert.equal(parseTxMetrics(page(1,n,2000000,counts),100+n),undefined);
  const result=parseTxMetrics(page(1,2,2000000,counts),110);
  assert.equal(result.totals.due,300);assert.equal(result.totals.cancelSkip,319);
  assert.equal(parseTxMetrics(page(1,2,2000000,counts),111),undefined);
});
test('snapshot identities do not mix; incomplete snapshots expire',()=>{
  for(let n=0;n<4;n++)assert.equal(parseTxMetrics(page(2,n,4000000,[]),200),undefined);
  assert.equal(parseTxMetrics(page(3,4,6000000,[]),210),undefined);
  assert.equal(parseTxMetrics(page(2,4,4000000,[]),10201),undefined);
});
test('delta handles counter and timestamp wrap; resets are unavailable',()=>{
  const totals=Object.fromEntries(txMetricKeys.map(k=>[k,0xfffffff0]));
  const a={version:1,snapshot:65535,atUs:0xfff00000,spanUs:2000000,totals};
  const b={...a,snapshot:0,atUs:(a.atUs+2000000)>>>0,totals:Object.fromEntries(txMetricKeys.map(k=>[k,16]))};
  const d=txMetricsDelta(a,b);assert.equal(d.totals.due,32);assert.equal(d.elapsedUs,2000000);
  assert.equal(txMetricsDelta({...a,totals:{...totals,due:123456}},b),null);
});
