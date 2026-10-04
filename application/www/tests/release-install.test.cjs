const test = require('node:test');
const assert = require('node:assert/strict');
const { releaseBlockReason, installRelease, isLegacyPhysicalConfirmationRejection } = require('../lib/device-transport/release-install-client.ts');
const { DeviceTransportError } = require('../lib/device-transport/types.ts');
global.localStorage = { values:new Map(), getItem(k){return this.values.get(k)??null;}, setItem(k,v){this.values.set(k,v);}, removeItem(k){this.values.delete(k);} };
let transaction='';
const inventory = () => ({ protocol:2, deviceModel:'STM32H750_HBOX',hardwareVersion:'2.0.0',currentSlot:'A',configVersion:34,
  securityVersion:1,metadataConsistent:true,stm32:{protocol:2,maintenance:2,version:'2.0.0',buildId:'old'},
  tx:{protocol:2,maintenance:2,version:'2.0.0',buildId:'old'},phase:'completed',installationState:'installed' });
const release = version => ({ id:'target',installable:true,bundleSha256:'hash',manifest:{schemaVersion:2,version,
  deviceModel:'STM32H750_HBOX',hardwareVersion:'2.0.0',bootSecurityMode:'unlocked-development',requiresManualLifecycleProvisioning:false,
  install:{protocol:2,order:'tx-then-stm32',configRead:{min:34,max:34},configWrite:34,stm32Maintenance:{min:2,max:2},txMaintenance:{min:2,max:2}}} });
test('old firmware physical-confirmation rejection is distinguished from current installation failures', () => {
  const error = (command, errNo) => new DeviceTransportError('protocol', 'Device rejected command', { command, errNo });
  assert.equal(isLegacyPhysicalConfirmationRejection(error('begin_release_install', 423)), true);
  assert.equal(isLegacyPhysicalConfirmationRejection(error('begin_release_install', 409)), false);
  assert.equal(isLegacyPhysicalConfirmationRejection(error('activate_release_install', 423)), false);
  assert.equal(isLegacyPhysicalConfirmationRejection(new DeviceTransportError('timeout', 'No response')), false);
});
test('upgrade, downgrade and same-version reinstall use compatibility, never latest-version heuristics', () => {
  for(const version of ['1.0.0','2.0.0','3.0.0'])assert.equal(releaseBlockReason(release(version),inventory()),null);
});
test('unproven baseline, hardware, configuration and pending installation block writes', () => {
  for(const [change,reason] of [
    [i=>i.protocol=0,'baseline-required'],[i=>i.metadataConsistent=false,'metadata-mismatch'],
    [i=>i.hardwareVersion='1.0.0','hardware-mismatch'],[i=>i.configVersion=33,'configuration-incompatible'],
    [i=>i.phase='tx-writing','installation-pending']]) {
    const i=inventory();change(i);assert.equal(releaseBlockReason(release('1.0.0'),i),reason);
  }
});
test('a changed source slot prevents BEGIN even after the package was downloaded', async () => {
  let writes=0;const i=inventory();i.currentSlot='B';
  const client={request:async name=>{if(name==='get_firmware_inventory')return i;writes++;}};
  await assert.rejects(installRelease(client,{release:release('3.0.0'),targetSlot:'B',securityVersion:1},()=>{}),/Device changed/);
  assert.equal(writes,0);
});

test('ambiguous activation never sends abort or a second activation', async () => {
  const calls=[];const selected=release('3.0.0');
  const client={
    request:async (name,params)=>{calls.push(name);if(name==='get_firmware_inventory')return inventory();
      if(name==='begin_release_install')transaction=params.session_id;
      if(name==='get_release_install_status')return {sessionId:transaction,targetDigest:'a'.repeat(64),phase:'receiving',backupReady:true};
      if(name==='activate_release_install')throw Error('lost activation ACK');return {success:true};},
    authorizedFetch:async()=>({ok:true,json:async()=>({success:true,data:selected})}),
    uploadFirmwareChunk:async()=>({success:true})
  };
  await installRelease(client,{release:selected,digest:'a'.repeat(64),targetSlot:'B',securityVersion:1,declaration:new Uint8Array(8),components:[]},()=>{});
  assert.equal(JSON.parse(localStorage.getItem('xora-release-install-v2')).result,'waiting');
  assert.equal(calls.filter(c=>c==='activate_release_install').length,1);
  assert.ok(!calls.some(c=>c.includes('abort')));
});

test('controller upload counts application and ADC bytes as one stage before TX staging', async () => {
  const events = []; let session;
  const selected = release('3.0.0');
  const client = {
    request: async (name, p) => {
      if (name === 'get_firmware_inventory') return inventory();
      if (name === 'begin_release_install') session = p.session_id;
      if (name === 'get_release_install_status') return { sessionId: session, targetDigest: 'a'.repeat(64), phase: 'receiving', backupReady: true };
      return { success: true };
    },
    authorizedFetch: async () => ({ ok: true, json: async () => ({ success: true, data: selected }) }),
    uploadFirmwareChunk: async () => ({ success: true }),
  };
  await installRelease(client, { release: selected, digest: 'a'.repeat(64), targetSlot: 'B', securityVersion: 1,
    declaration: new Uint8Array(8), components: [
      { name: 'application', address: 0x902b0000, data: new Uint8Array(8192) },
      { name: 'adc_mapping', address: 0x90530000, data: new Uint8Array(1024) },
      { name: 'tx', address: 0x90790000, data: new Uint8Array(4096) },
    ] }, p => events.push(p));
  const controller = events.filter(p => p.stage === 'staging-controller');
  assert.deepEqual(controller.map(p => p.stageReceived), [4096, 8192, 9216]);
  assert.ok(controller.every(p => p.stageTotal === 9216));
  assert.equal(controller.at(-1).received, 1024);
  assert.equal(JSON.parse(localStorage.getItem('xora-release-install-v2')).progress.overallPercent, 80);
});

test('overall installation progress is cumulative across stages and recovery', () => {
  const { accumulateInstallProgress } = require('../lib/device-transport/release-install-progress.ts');
  let p = null;
  const update = next => (p = accumulateInstallProgress(p, next)).overallPercent;
  assert.equal(update({ stage: 'downloading', received: 50, total: 100 }), 5);
  assert.equal(update({ stage: 'extracting', received: 1, total: 1 }), 15);
  assert.equal(update({ stage: 'backup' }), 15);
  assert.equal(update({ stage: 'declaring', received: 10, total: 10 }), 20);
  assert.equal(update({ stage: 'backing-up-tx', received: 50, total: 100 }), 30);
  assert.equal(update({ stage: 'staging-controller', received: 100, total: 100, stageReceived: 100, stageTotal: 200 }), 55);
  assert.equal(update({ stage: 'staging-controller', received: 1, total: 100, stageReceived: 101, stageTotal: 200 }), 55);
  assert.equal(update({ stage: 'staging-tx', received: 100, total: 100 }), 80);
  assert.equal(update({ stage: 'waiting-device' }), 80);
  assert.equal(update({ stage: 'tx-writing', received: 100, total: 100 }), 80);
  assert.equal(update({ stage: 'tx-verified' }), 90);
  assert.equal(update({ stage: 'verifying' }), 95);
  for (const stage of ['waiting-device', 'tx-restoring', 'rollback-verifying', 'restored', 'restore-failed', 'timeout', 'failed']) {
    assert.equal(update({ stage }), 95); assert.equal(p.stepIndex, 8);
  }
  assert.equal(update({ stage: 'completed' }), 100);
});

test('display steps group all milestones and retain the current group on recovery', () => {
  const { installDisplayStep, accumulateInstallProgress } = require('../lib/device-transport/release-install-progress.ts');
  for (const stage of ['downloading', 'extracting']) assert.equal(installDisplayStep({ stage }), 0);
  for (const stage of ['backup', 'declaring', 'backing-up-tx']) assert.equal(installDisplayStep({ stage }), 1);
  for (const stage of ['staging-controller', 'staging-tx', 'prepared', 'activating', 'waiting-device', 'tx-writing', 'tx-verified', 'committing', 'verifying', 'completed'])
    assert.equal(installDisplayStep({ stage }), 2);
  const previous = accumulateInstallProgress(null, { stage: 'backing-up-tx', received: 30, total: 100 });
  assert.equal(installDisplayStep(accumulateInstallProgress(previous, { stage: 'failed' })), 1);
});

test('offline presentation estimates advance with time, never confirm completion or alter measured progress', () => {
  const { displayedInstallPercent } = require('../lib/device-transport/release-install-progress.ts');
  const p = { stage: 'waiting-device', overallPercent: 80, stepIndex: 6 };
  const original = {...p}; let displayed = 80;
  for (const elapsed of [0, 1000, 30000, 60000, 120000, 180000, 600000]) {
    const next = displayedInstallPercent(p, 1000, 1000 + elapsed, displayed);
    assert.ok(next >= displayed && next < 100); displayed = next;
  }
  assert.ok(displayed > 95); assert.deepEqual(p, original);
  assert.equal(displayedInstallPercent(p, 1000, 31000), displayedInstallPercent(p, 1000, 31000, 0)); // Refresh derives from activation time.
  assert.equal(displayedInstallPercent(p, undefined, 31000), 80);
  assert.equal(displayedInstallPercent(p, 40000, 31000), 80); // Clock moved backwards.
  for (const stage of ['timeout', 'failed', 'tx-restoring', 'restored', 'restore-failed'])
    assert.equal(displayedInstallPercent({...p, stage}, 1000, 900000, displayed), displayed);
  assert.equal(displayedInstallPercent({stage: 'completed'}, 1000, 31000, displayed), 100);
  assert.equal(displayedInstallPercent({stage: 'downloading'}, 1000, 31000), 0);
});

test('missing sizes and invalid counters cannot create false completion or reset progress', () => {
  const { accumulateInstallProgress } = require('../lib/device-transport/release-install-progress.ts');
  let p = accumulateInstallProgress(null, { stage: 'downloading', received: 1000 });
  assert.equal(p.overallPercent, 0);
  p = accumulateInstallProgress(p, { stage: 'staging-tx', received: 200, total: 100 });
  assert.equal(p.overallPercent, 80);
  assert.equal(accumulateInstallProgress(p, { stage: 'downloading', received: NaN, total: 10 }).overallPercent, 80);
  assert.equal(accumulateInstallProgress(null, { stage: 'verifying', received: 1, total: 1, overallPercent: 100 }).overallPercent, 99);
});

test('withdrawal before BEGIN and a negative PREPARED result stop installation', async () => {
  for(const withdrawn of [true,false]) {
    const calls=[];const selected=release('3.0.0');
    const client={request:async (name,params)=>{calls.push(name);if(name==='begin_release_install')transaction=params.session_id;
      if(name==='get_release_install_status')return {sessionId:transaction,targetDigest:'a'.repeat(64),phase:'receiving',backupReady:true};
      return name==='get_firmware_inventory'?inventory():{success:name!=='prepare_release_install'};},
      authorizedFetch:async()=>({ok:!withdrawn,json:async()=>({success:!withdrawn,data:selected,message:'Withdrawn'})}),
      uploadFirmwareChunk:async()=>({success:true})};
    await assert.rejects(installRelease(client,{release:selected,digest:'a'.repeat(64),targetSlot:'B',securityVersion:1,declaration:new Uint8Array(8),components:[]},()=>{}),withdrawn?/Withdrawn/:/prepared/);
    assert.ok(!calls.includes('activate_release_install'));
    if(withdrawn)assert.ok(!calls.includes('begin_release_install'));
  }
});

test('protocol 1 releases remain visible but cannot start an installation', () => {
  const r=release('1.0.1');r.manifest.install.protocol=1;
  assert.equal(releaseBlockReason(r,inventory()),'unsupported-protocol');
  const i=inventory();i.tx.protocol=1;assert.equal(releaseBlockReason(release('2.0.0'),i),'baseline-required');
});
test('backup failure prevents staging target components or activation', async () => {
  const calls=[];let session;
  const client={request:async(name,p)=>{calls.push(name);if(name==='get_firmware_inventory')return inventory();
    if(name==='begin_release_install')session=p.session_id;
    if(name==='get_release_install_status')return {sessionId:session,targetDigest:'a'.repeat(64),phase:'failed',error:'Backup digest mismatch'};
    return {success:true};},authorizedFetch:async()=>({ok:true,json:async()=>({success:true,data:release('3.0.0')})}),
    uploadFirmwareChunk:async p=>{calls.push(p.componentName);return {success:true};}};
  await assert.rejects(installRelease(client,{release:release('3.0.0'),digest:'a'.repeat(64),targetSlot:'B',securityVersion:1,
    declaration:new Uint8Array(8),components:[{name:'tx',address:0x90790000,data:new Uint8Array(8)}]},()=>{}),/Backup digest/);
  assert.ok(!calls.includes('tx') && !calls.includes('activate_release_install'));
});

test('explicit activation rejection removes its deadline; storage failure prevents activation',async()=>{
 const original=global.localStorage;
 try { for(const fault of ['response','protocol','storage']) {
  global.localStorage=original;original.values.clear();const calls=[];let session;
  if(fault==='storage')global.localStorage={getItem(){return null;},setItem(){throw Error('storage unavailable');},removeItem(){}};
  const selected=release('3.0.0');const client={request:async(name,p)=>{
   calls.push(name);if(name==='get_firmware_inventory')return inventory();
   if(name==='begin_release_install')session=p.session_id;
   if(name==='get_release_install_status')return {sessionId:session,targetDigest:'a'.repeat(64),phase:'receiving',backupReady:true};
   if(name==='activate_release_install'){
    if(fault==='protocol')throw new DeviceTransportError('protocol','rejected',{errNo:409});
    return {success:false};
   }return {success:true};
  },authorizedFetch:async()=>({ok:true,json:async()=>({success:true,data:selected})}),uploadFirmwareChunk:async()=>({success:true})};
  await assert.rejects(installRelease(client,{release:selected,digest:'a'.repeat(64),targetSlot:'B',securityVersion:1,
   declaration:new Uint8Array(8),components:[]},()=>{}),fault==='storage'?/storage unavailable/:fault==='protocol'?/rejected/:/Activation rejected/);
  assert.equal(calls.filter(n=>n==='activate_release_install').length,fault==='storage'?0:1);
  assert.equal(global.localStorage.getItem('xora-release-install-v2'),null);
  assert.ok(!calls.some(n=>n.includes('abort')));
 }} finally {global.localStorage=original;}
});
