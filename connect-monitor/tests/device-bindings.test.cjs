// Prepared for the next explicitly authorized regression run. No device I/O.
const test=require('node:test');
const assert=require('node:assert/strict');
const {DeviceBindingRegistry}=require('../dist/electron/sources/device-bindings.js');
const {deviceRole,matchesHidTelemetryDevice}=require('../dist/electron/sources/hid-device-selection.js');
const pad=(id,mode,generation=1)=>({id,name:id,backend:'wgi',generation,
  vendorId:mode==='USB'?0xcafe:0x045e,productId:mode==='USB'?0x4024:0x02ff});
const peer=(id,mode,generation=1)=>({...pad(id,mode,generation),path:id,sourceMode:mode,capable:true});

test('source binding is independent of enumeration order and unrelated controllers',()=>{
  for(const reverse of [false,true]) {
    const r=new DeviceBindingRegistry();
    const devices=[pad('wire','USB'),pad('rx','RF24G'),{...pad('other','USB'),vendorId:0x045e,productId:0x028e}];
    r.updateGamepads(reverse?devices.reverse():devices);
    r.telemetry=[peer('rx-hid','RF24G'),peer('wire-hid','USB')];
    assert.equal(r.binding('USB').gamepadId,'wire');
    assert.equal(r.binding('RF24G').gamepadId,'rx');
    assert.equal(r.binding('USB').telemetryId,'wire-hid');
  }
});
test('duplicate peers require an explicit pair and cannot broadcast configuration',()=>{
  const r=new DeviceBindingRegistry();r.updateGamepads([pad('wire','USB')]);
  r.telemetry=[peer('one','USB'),peer('two','USB')];
  assert.equal(r.binding('USB').state,'ambiguous');
  assert.equal(r.binding('USB').telemetryAvailable,false);
  r.choose('USB',{gamepadId:'wire',telemetryId:'two'});
  assert.equal(r.binding('USB').telemetryAvailable,true);
  assert.equal(r.binding('USB').telemetryId,'two');
  r.updateGamepads([]);
  assert.equal(r.binding('USB').telemetryAvailable,false);
});
test('disconnect never takes over another device; stable identity can reconnect',()=>{
  const r=new DeviceBindingRegistry();r.updateGamepads([pad('wire','USB')]);
  r.choose('USB',{gamepadId:'wire',telemetryId:null});
  const before=r.binding('USB').generation;
  r.updateGamepads([pad('second','USB')]);
  assert.equal(r.binding('USB').gamepadId,null);
  r.updateGamepads([pad('wire','USB',2),pad('second','USB')]);
  assert.equal(r.binding('USB').gamepadId,'wire');
  assert.ok(r.binding('USB').generation>before);
});
test('temporary XInput choice expires on disconnect and generation change',()=>{
  for(const disconnect of [false,true]) {
    const r=new DeviceBindingRegistry();
    const fallback={...pad('xinput:0','USB'),backend:'xinput',vendorId:0,productId:0};
    r.updateGamepads([fallback]);r.choose('USB',{gamepadId:fallback.id,telemetryId:null});
    if(disconnect)r.updateGamepads([]);
    r.updateGamepads([{...fallback,generation:2}]);
    assert.equal(r.binding('USB').gamepadId,null);
  }
});
test('role mismatch and WebConfig are rejected even when explicitly selected',()=>{
  const r=new DeviceBindingRegistry();
  r.updateGamepads([pad('rx','RF24G'),{...pad('web','USB'),productId:0x4021}]);
  assert.throws(()=>r.choose('USB',{gamepadId:'rx',telemetryId:null}));
  assert.throws(()=>r.choose('USB',{gamepadId:'web',telemetryId:null}));
  assert.equal(deviceRole({vendorId:0x045e,productId:0x028e}),null);
  assert.equal(matchesHidTelemetryDevice({vendorId:0xcafe,productId:0x4021,usagePage:0xff00},{vendorId:0xcafe,productId:0x4021}),false);
  assert.equal(matchesHidTelemetryDevice({vendorId:0xcafe,productId:0x4024,usagePage:0xff00,interface:3},{vendorId:null,productId:null}),false);
});
test('snapshots follow source and reject expired or previous-connection data',()=>{
  const r=new DeviceBindingRegistry();r.updateGamepads([pad('wire','USB',2),pad('rx','RF24G')]);
  const readings=new Map([['wire',{standardMask:1,timestampMs:100,generation:2}],['rx',{standardMask:2,timestampMs:100,generation:1}]]);
  r.activeSource='USB';assert.equal(r.read(readings,100).standardMask,1);
  r.activeSource='RF24G';assert.equal(r.read(readings,100).standardMask,2);
  assert.equal(r.read(readings,1100).standardMask,0);
  r.activeSource='USB';readings.get('wire').generation=1;
  assert.equal(r.read(readings,100).connected,false);
  r.telemetry=[{...peer('usb','USB'),capable:false}];
  readings.get('wire').generation=2;
  assert.equal(r.read(readings,100).standardMask,1);
  assert.equal(r.binding('USB').telemetryAvailable,false);
});
