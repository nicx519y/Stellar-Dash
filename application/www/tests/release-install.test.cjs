const test = require('node:test');
const assert = require('node:assert/strict');
const { releaseBlockReason, installRelease } = require('../lib/device-transport/release-install-client.ts');
const inventory = () => ({ protocol:1, deviceModel:'STM32H750_HBOX',hardwareVersion:'2.0.0',currentSlot:'A',configVersion:34,
  securityVersion:1,metadataConsistent:true,stm32:{protocol:1,maintenance:1,version:'2.0.0',buildId:'old'},
  tx:{protocol:1,maintenance:1,version:'2.0.0',buildId:'old'},phase:'completed',installationState:'installed' });
const release = version => ({ id:'target',installable:true,bundleSha256:'hash',manifest:{schemaVersion:2,version,
  deviceModel:'STM32H750_HBOX',hardwareVersion:'2.0.0',bootSecurityMode:'unlocked-development',requiresManualLifecycleProvisioning:false,
  install:{protocol:1,order:'tx-then-stm32',configRead:{min:34,max:34},configWrite:34,stm32Maintenance:{min:1,max:1},txMaintenance:{min:1,max:1}}} });
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
    request:async name=>{calls.push(name);if(name==='get_firmware_inventory')return inventory();
      if(name==='activate_release_install')throw Error('lost activation ACK');return {success:true};},
    authorizedFetch:async()=>({ok:true,json:async()=>({success:true,data:selected})}),
    uploadFirmwareChunk:async()=>({success:true})
  };
  await assert.rejects(installRelease(client,{release:selected,targetSlot:'B',securityVersion:1,declaration:new Uint8Array(8),components:[]},()=>{}),/lost activation ACK/);
  assert.equal(calls.filter(c=>c==='activate_release_install').length,1);
  assert.ok(!calls.some(c=>c.includes('abort')));
});

test('withdrawal before BEGIN and a negative PREPARED result stop installation', async () => {
  for(const withdrawn of [true,false]) {
    const calls=[];const selected=release('3.0.0');
    const client={request:async name=>{calls.push(name);return name==='get_firmware_inventory'?inventory():{success:name!=='prepare_release_install'};},
      authorizedFetch:async()=>({ok:!withdrawn,json:async()=>({success:!withdrawn,data:selected,message:'Withdrawn'})}),
      uploadFirmwareChunk:async()=>({success:true})};
    await assert.rejects(installRelease(client,{release:selected,targetSlot:'B',securityVersion:1,declaration:new Uint8Array(8),components:[]},()=>{}),withdrawn?/Withdrawn/:/prepared/);
    assert.ok(!calls.includes('activate_release_install'));
    if(withdrawn)assert.ok(!calls.includes('begin_release_install'));
  }
});
