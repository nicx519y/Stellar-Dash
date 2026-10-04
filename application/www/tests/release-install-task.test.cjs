const test=require('node:test');const assert=require('node:assert/strict');
const {createInstallMonitor,readInstallTask,saveInstallTask,reconcileInstallTask,INSTALL_TIMEOUT_MS}=require('../lib/device-transport/release-install-task.ts');
const {readResumableInstallTask}=require('../lib/device-transport/release-install-task.ts');
const task=()=>({protocol:2,sessionId:'rel-test',digest:'a'.repeat(64),version:'2.0.0',activatedAt:1000,
  release:{manifest:{version:'2.0.0'}},result:'waiting'});
const status=(phase='completed')=>({protocol:2,sessionId:'rel-test',targetDigest:'a'.repeat(64),targetVersion:'2.0.0',
  phase,installationState:'installed',confirmedDigest:'a'.repeat(64),confirmedVersion:'2.0.0'});
function clock(){let time=1000,sequence=0;const pending=new Map();return {now:()=>time,
 schedule:(fn,delay)=>{pending.set(++sequence,{fn,at:time+delay});return sequence;},cancel:id=>pending.delete(id),
 async advance(n){const end=time+n;for(let guard=0;guard<1000;guard++){
   const next=[...pending].sort((a,b)=>a[1].at-b[1].at)[0];if(!next||next[1].at>end)break;
   time=next[1].at;pending.delete(next[0]);next[1].fn();await new Promise(resolve=>setImmediate(resolve));
 }time=end;await new Promise(resolve=>setImmediate(resolve));},pending};}

test('reopening retires completed records, while pending and recovery tasks remain resumable', () => {
 const values=new Map();const storage={getItem:k=>values.get(k)||null,setItem:(k,v)=>values.set(k,v),removeItem:k=>values.delete(k)};
 for(const result of ['waiting','timeout','restored','restore-failed','failed']) {
  const t={...task(),result};saveInstallTask(t,storage);assert.deepEqual(readResumableInstallTask(storage),t);
 }
 saveInstallTask({...task(),result:'completed'},storage);
 assert.equal(readResumableInstallTask(storage),null);assert.equal(readInstallTask(storage),null);
 saveInstallTask({...task(),result:'completed'},storage);
 assert.equal(readResumableInstallTask({...storage,removeItem(){throw Error('read only');}}),null);
});
test('refresh restores the same transaction and corrupt storage does not fabricate a task',()=>{
 const values=new Map();const storage={getItem:k=>values.get(k)||null,setItem:(k,v)=>values.set(k,v)};
 saveInstallTask(task(),storage);assert.deepEqual(readInstallTask(storage),task());
 storage.setItem('xora-release-install-v2','{}');assert.equal(readInstallTask(storage),null);
 assert.throws(()=>saveInstallTask(task(),{setItem(){},getItem(){return null;}}),/persist/);
});
test('wrong transaction, digest, device version and unconfirmed completion cannot finish task',()=>{
 for(const change of [i=>i.sessionId='other',i=>i.targetDigest='b'.repeat(64),i=>i.targetVersion='3.0.0',i=>i.protocol=1]){
  const i=status();change(i);assert.equal(reconcileInstallTask(task(),i),null);
 }
 const i=status();i.confirmedDigest='b'.repeat(64);assert.equal(reconcileInstallTask(task(),i).result,'waiting');
 assert.equal(reconcileInstallTask(task(),i).progress.overallPercent,95);
});

test('refresh and timeout preserve overall progress; only matched verified completion reaches 100', () => {
 const values=new Map();const storage={getItem:k=>values.get(k)||null,setItem:(k,v)=>values.set(k,v)};
 let t=reconcileInstallTask(task(),status('verifying'));
 assert.equal(t.progress.overallPercent,95);saveInstallTask(t,storage);
 t=readInstallTask(storage);assert.equal(t.progress.overallPercent,95);
 const c=clock();let timedOut;
 const m=createInstallMonitor({task:t,...c,now:()=>200000,connected:()=>false,reconnect:async()=>{},query:async()=>status(),changed:next=>timedOut=next});
 m.start();assert.equal(timedOut.progress.overallPercent,95);assert.equal(timedOut.progress.stepIndex,8);
 assert.equal(reconcileInstallTask(timedOut,{...status('restored'),recoveryResult:'restored'}).progress.overallPercent,95);
 assert.equal(reconcileInstallTask(timedOut,{...status('restore-failed'),recoveryResult:'failed'}).progress.overallPercent,95);
 assert.equal(reconcileInstallTask(timedOut,status()).progress.overallPercent,100);m.stop();
});
test('restored and failed recovery outcomes retain errors; a late result replaces browser timeout',()=>{
 const t={...task(),result:'timeout'};
 assert.equal(reconcileInstallTask(t,status()).result,'completed');
 assert.equal(reconcileInstallTask(t,{...status('restored'),recoveryResult:'restored',installError:'write failed'}).result,'restored');
 const failed=reconcileInstallTask(t,{...status('restore-failed'),recoveryResult:'failed',recoveryError:'no image'});
 assert.equal(failed.result,'restore-failed');assert.equal(failed.error,'no image');
});
test('offline reconnect is every 3 seconds; connected status reads are every 2 seconds',async()=>{
 const c=clock();let connected=false,reconnects=0,queries=0;const changes=[];
 const m=createInstallMonitor({task:task(),...c,connected:()=>connected,reconnect:async()=>{if(++reconnects===2)connected=true;},
  query:async()=>{queries++;return status(queries<2?'verifying':'completed');},changed:t=>changes.push(t)});
 m.start();await c.advance(2999);assert.equal(reconnects,0);await c.advance(1);assert.equal(reconnects,1);
 await c.advance(3000);assert.equal(reconnects,2);assert.equal(queries,1);
 await c.advance(1999);assert.equal(queries,1);await c.advance(1);assert.equal(queries,2);
 assert.equal(changes.at(-1).result,'completed');assert.equal(c.pending.size,0);m.stop();
});
test('180 second timeout stops retries and never sends a write; manual reconnect reconciles late completion',async()=>{
 const c=clock();let reconnects=0,connected=false;const changes=[];
 const m=createInstallMonitor({task:task(),...c,connected:()=>connected,reconnect:async()=>{reconnects++;},query:async()=>status(),changed:t=>changes.push(t)});
 m.start();await c.advance(INSTALL_TIMEOUT_MS);assert.equal(changes.at(-1).result,'timeout');const count=reconnects;
 await c.advance(30000);assert.equal(reconnects,count);assert.equal(c.pending.size,0);
 await m.manual(async()=>{connected=true;});assert.equal(changes.at(-1).result,'completed');m.stop();
});
test('slow requests and manual actions remain serialized; absolute deadline works during pending request',async()=>{
 const c=clock();let resolve;const pending=new Promise(r=>resolve=r);let queries=0;const changes=[];
 const m=createInstallMonitor({task:task(),...c,connected:()=>true,reconnect:async()=>{},query:async()=>{queries++;await pending;return status();},changed:t=>changes.push(t)});
 m.start();await c.advance(2000);const manual=m.manual();await c.advance(INSTALL_TIMEOUT_MS-2000);
 assert.equal(queries,1);assert.equal(changes.at(-1).result,'timeout');resolve();await manual;
 assert.equal(changes.at(-1).result,'completed');assert.equal(c.pending.size,0);m.stop();
});
test('refresh after deadline displays timeout without any connection attempt',()=>{
 const c=clock();let calls=0,result;
 const m=createInstallMonitor({task:{...task(),activatedAt:1},...c,now:()=>200000,connected:()=>false,reconnect:async()=>{calls++;},query:async()=>{calls++;},changed:t=>result=t.result});
 m.start();assert.equal(result,'timeout');assert.equal(calls,0);m.stop();
});

test('manual read after timeout waits for connection initialization without reconnecting twice',async()=>{
 const c=clock();let connected=false,queries=0,connections=0,ready;
 const initialized=new Promise(r=>ready=r);const changes=[];
 const m=createInstallMonitor({task:{...task(),result:'timeout'},...c,connected:()=>connected,
  reconnect:async()=>{connections++;connected=true;},waitReady:()=>initialized,
  query:async()=>{queries++;return status();},changed:t=>changes.push(t)});
 const request=m.manual();await new Promise(resolve=>setImmediate(resolve));
 assert.equal(connections,1);assert.equal(queries,0);assert.equal(m.manual(),request);
 ready();await request;assert.equal(queries,1);assert.equal(changes.at(-1).result,'completed');
 assert.equal(c.pending.size,0);m.stop();
});

test('connection finishing after deadline cannot start a new automatic status query',async()=>{
 const c=clock();let connected=false,resolve,queries=0;const opening=new Promise(r=>resolve=r);const changes=[];
 const m=createInstallMonitor({task:task(),...c,connected:()=>connected,
  reconnect:async()=>{await opening;connected=true;},query:async()=>{queries++;return status();},changed:t=>changes.push(t)});
 m.start();await c.advance(3000);await c.advance(INSTALL_TIMEOUT_MS-3000);
 assert.equal(changes.at(-1).result,'timeout');resolve();await new Promise(r=>setImmediate(r));
 assert.equal(queries,0);assert.equal(c.pending.size,0);
 await m.manual();assert.equal(queries,1);assert.equal(changes.at(-1).result,'completed');m.stop();
});
