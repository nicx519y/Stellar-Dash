const test = require("node:test"),
  assert = require("node:assert/strict");
const {
  MockResourceDevice,
  compileResource,
} = require("../lib/resource-fetch-mock.ts");
const {
  installLighting,
  verifyResource,
  resourceKey,
} = require("../lib/resources.ts");
const extra = require("../../../resources/examples/pulse-scan.xora-resource.json");
test("download, readback, apply, reboot, profile isolation, deletion guards and new composition", async () => {
  const profiles = [
    { id: "one", ledsConfigs: {} },
    { id: "two", ledsConfigs: {} },
  ];
  let device = new MockResourceDevice();
  const send = (cmd, p = {}) => device.command(cmd, p, profiles, "one");
  const before = await send("resources_list");
  assert.equal(before.items.length, 10);
  const bytes = await compileResource(extra);
  await installLighting(send, extra, bytes);
  assert.equal(
    (await send("resources_list")).profiles[0].keys.resourceId,
    "key-static",
  );
  await send("resources_apply", { ...extra, profileId: "one" });
  const persisted = structuredClone(device.state);
  device = new MockResourceDevice();
  device.state = persisted;
  const state = await send("resources_list");
  assert.equal(state.profiles[0].keys.resourceId, extra.resourceId);
  assert.equal(state.profiles[1].keys.resourceId, "key-static");
  await assert.rejects(send("resources_remove", extra), /in use/);
  const newer = { ...extra, revision: 2 };
  await installLighting(send, newer, await compileResource(newer));
  assert.equal((await send("resources_list")).profiles[0].keys.revision, 1);
  await send("resources_apply", { ...newer, profileId: "one" });
  await send("resources_remove", extra);
  assert.equal(
    (await send("resources_list")).items.some(
      (r) => resourceKey(r) === resourceKey(extra),
    ),
    false,
  );
  const damaged = bytes.slice();
  damaged[64] ^= 1;
  await assert.rejects(verifyResource(damaged), /SHA/);
  await installLighting(send, newer, await compileResource(newer));
  assert.equal((await send("resources_list")).items.length, 11);
});
test("failed transfers preserve installed resources", async () => {
  const device = new MockResourceDevice(),
    send = (cmd, p = {}) =>
      device.command(cmd, p, [{ id: "one", ledsConfigs: {} }], "one");
  const { transferId } = await send("resources_begin", { size: 192 });
  await assert.rejects(
    send("resources_chunk", { transferId, offset: 1, hex: "00" }),
    /chunk/,
  );
  await assert.rejects(send("resources_commit", { transferId }), /Incomplete/);
  await send("resources_abort", { transferId });
  assert.equal((await send("resources_list")).items.length, 10);
});

test("profile import validates exact revisions and migrates legacy effect selections", () => {
  const device = new MockResourceDevice();
  const old = { id: "legacy", ledsConfigs: { ledsEffectStyle: 4, aroundLedEffectStyle: 3 } };
  device.importProfile(old);
  assert.equal(old.lightingResources.keys.resourceId, "key-ripple");
  assert.equal(old.lightingResources.ambient.resourceId, "ambient-meteor");
  const before = structuredClone(device.state);
  assert.throws(() => device.importProfile({ ...old, lightingResources: { ...old.lightingResources, keys: { resourceId: "missing", revision: 9 } } }), /Install/);
  assert.deepEqual(device.state, before);
});

test("maximum library with escaped names and sixteen profiles fits the WebHID response limit", async () => {
  const device = new MockResourceDevice();
  device.state.sources = Array.from({length:32}, (_,i) => ({ ...extra, name:'"'.repeat(79), resourceId:`resource-${String(i).padStart(6,'0')}`, revision: 4294967295 }));
  const profiles = Array.from({length:16},(_,i)=>({id:`profile-${String(i).padStart(7,'0')}`,ledsConfigs:{}}));
  const result = await device.command('resources_list',{},profiles,profiles[0].id);
  assert.ok(Buffer.byteLength(JSON.stringify({success:true,data:result})) < 16*1024);
  await assert.rejects(compileResource({...extra,name:'\u0001'.repeat(79)}), /name/);
});
const {staticResource, protectedResource} = require('../lib/resources.ts');
test('independent eight-effect limits count revisions and duplicate install remains idempotent', async () => {
  const device = new MockResourceDevice(), profiles=[{id:'one',ledsConfigs:{}}];
  const send=(cmd,p={})=>device.command(cmd,p,profiles,'one');
  for(let revision=1;revision<=2;revision++) {const item={...extra,revision}; await installLighting(send,item,await compileResource(item));}
  let inv=await send('resources_list');
  assert.deepEqual(inv.counts,{'key-lighting':8,'ambient-lighting':4});
  assert.equal(inv.removeWithFallback,true);
  await installLighting(send,extra,await compileResource(extra));
  const third={...extra,revision:3};
  await assert.rejects(installLighting(send,third,await compileResource(third)),/RESOURCE_COUNT_LIMIT/);
  for(let revision=1;revision<=4;revision++) {const item={...extra,type:'ambient-lighting',resourceId:'ambient-extra',revision}; await installLighting(send,item,await compileResource(item));}
  const ambient={...extra,type:'ambient-lighting',resourceId:'ambient-extra',revision:5};
  await assert.rejects(installLighting(send,ambient,await compileResource(ambient)),/RESOURCE_COUNT_LIMIT/);
  inv=await send('resources_list'); assert.equal(inv.counts['ambient-lighting'],8);
});
test('factory uninstall resets every referencing profile, preserves the other zone, and survives restart',async()=>{
  let device=new MockResourceDevice(); const profiles=[{id:'one',ledsConfigs:{ledsEffectStyle:4}},{id:'two',ledsConfigs:{ledsEffectStyle:4}}];
  const send=(cmd,p={})=>device.command(cmd,p,profiles,'one');
  const result=await send('resources_remove',{resourceId:'key-ripple',revision:1,resetReferences:true});
  assert.deepEqual(result.affectedProfiles,['one','two']); assert.equal(result.removed,true);
  const state=structuredClone(device.state); device=new MockResourceDevice(); device.state=state;
  const inv=await send('resources_list'); assert.equal(inv.items.some(r=>r.resourceId==='key-ripple'),false);
  for(const p of inv.profiles){assert.equal(p.keys.resourceId,'key-static');assert.equal(p.ambient.resourceId,'ambient-static');}
  await assert.rejects(send('resources_apply',{resourceId:'key-ripple',revision:1,profileId:'one'}),/not installed/);
  for(const type of ['key-lighting','ambient-lighting']) {const ref=staticResource(type);assert.ok(protectedResource(ref));assert.equal((await send('resources_remove',{...ref,resetReferences:true})).errorCode,'RESOURCE_PROTECTED');}
});
test('failed config save does not delete or alter references; failed removal retains the persisted fallback',async()=>{
  const device=new MockResourceDevice(), profiles=[{id:'one',ledsConfigs:{ledsEffectStyle:4}}];
  const send=(cmd,p={})=>device.command(cmd,p,profiles,'one');
  await send('resources_list'); const old=structuredClone(device.state);
  device.failConfigSave=true;
  let reply=await send('resources_remove',{resourceId:'key-ripple',revision:1,resetReferences:true});
  assert.equal(reply.errorCode,'RESOURCE_CONFIG_SAVE_FAILED'); assert.deepEqual(device.state,old);
  device.failConfigSave=false;device.failRemoval=true;
  reply=await send('resources_remove',{resourceId:'key-ripple',revision:1,resetReferences:true});
  assert.equal(reply.configurationSaved,true); assert.equal(reply.removed,false);
  assert.equal(device.state.profiles.one.keys.resourceId,'key-static');
  assert.ok(device.state.sources.some(s=>s.resourceId==='key-ripple'));
  device.failRemoval=false;
  assert.equal((await send('resources_remove',{resourceId:'key-ripple',revision:1,resetReferences:true})).removed,true);
});
