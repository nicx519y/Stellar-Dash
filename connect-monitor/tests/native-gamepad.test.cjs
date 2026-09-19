const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const ts = require('typescript');
const { createNativeGamepadReader } = require('../dist/electron/sources/native-gamepad.js');

function loadRenderer(name) {
  const code = ts.transpileModule(fs.readFileSync(path.join(__dirname, '../renderer/src/ui', `${name}.ts`), 'utf8'), {
    compilerOptions: { module: ts.ModuleKind.CommonJS, target: ts.ScriptTarget.ES2022 },
  }).outputText;
  const module = { exports: {} };
  new Function('require', 'module', 'exports', code)(
    (id) => id === './hitboxButtonMap' ? loadRenderer('hitboxButtonMap') : require(id), module, module.exports);
  return module.exports;
}

test('native XInput maps every direction/button and trigger threshold', () => {
  let buttons = 0, left = 0, right = 0;
  const read = createNativeGamepadReader((slot, state) => {
    if (slot) return 1167;
    state.Gamepad = { wButtons: buttons, bLeftTrigger: left, bRightTrigger: right }; return 0;
  });
  const bits = [0x1000,0x2000,0x4000,0x8000,0x100,0x200,0,0,0x20,0x10,0x40,0x80,1,2,4,8];
  bits.forEach((bit, index) => { if (bit) { buttons=bit; assert.equal(read().standardMask, 1<<index); } });
  buttons=0;left=127;right=128;assert.equal(read().standardMask,1<<7);
  left=255;right=0;assert.equal(read().standardMask,1<<6);
});

test('unplug clears pressed state; same slot can reconnect with a held key', () => {
  let connected=true;
  const read=createNativeGamepadReader((slot,state)=>{
    if(!connected || slot!==2)return 1167;
    state.Gamepad={wButtons:2,bLeftTrigger:0,bRightTrigger:0};return 0;
  });
  assert.equal(read().standardMask,1<<13);
  connected=false;assert.equal(read().standardMask,0);assert.equal(read().connected,false);
  connected=true;assert.equal(read().standardMask,1<<13);
});

test('selected XInput slot is stable; failed slots do not prevent discovery', () => {
  const online=new Set([2]);
  const read=createNativeGamepadReader((slot,state)=>{
    if(slot===1)throw Error('transient');
    if(!online.has(slot))return 1167;
    state.Gamepad={wButtons:1,bLeftTrigger:0,bRightTrigger:0};return 0;
  });
  assert.equal(read().deviceId,'Windows XInput 3');
  online.add(0);assert.equal(read().deviceId,'Windows XInput 3');
  online.delete(2);assert.equal(read().deviceId,'Windows XInput 1');
});

test('canvas mapping works without browser Gamepad access and releases stale state', () => {
  const { nativeGamepadButtonsSnapshot }=loadRenderer('gamepadButtons');
  const { HITBOX_BUTTON_MAP }=loadRenderer('hitboxButtonMap');
  const native={connected:true,deviceId:'Windows XInput 1',standardMask:1<<13,timestampMs:100};
  const snapshot=nativeGamepadButtonsSnapshot(native,100);
  const labels=snapshot.buttonStates.filter(b=>b.isPressed).map(b=>HITBOX_BUTTON_MAP[b.buttonIndex].label);
  assert.deepEqual(labels,['DOWN']);
  assert.equal(nativeGamepadButtonsSnapshot({...native,standardMask:0},110).buttonStates.some(b=>b.isPressed),false);
  assert.equal(nativeGamepadButtonsSnapshot(native,1100).connected,false);
  assert.equal(nativeGamepadButtonsSnapshot(native,1100).buttonStates.some(b=>b.isPressed),false);
});
