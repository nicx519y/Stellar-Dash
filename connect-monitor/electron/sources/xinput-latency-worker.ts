import { parentPort } from "node:worker_threads";

// Dedicated event loop: renderer work and chart rendering cannot delay polling.
// The actual call interval is carried with each transition, never assumed 1 ms.
const koffi = require("koffi");
let lib: any;
try { lib = koffi.load("xinput1_4.dll"); } catch { lib = koffi.load("xinput9_1_0.dll"); }
const gamepad = koffi.struct({wButtons:"uint16",bLeftTrigger:"uint8",bRightTrigger:"uint8",
  sThumbLX:"int16",sThumbLY:"int16",sThumbRX:"int16",sThumbRY:"int16"});
const stateType = koffi.struct({dwPacketNumber:"uint32",Gamepad:gamepad});
const read = lib.func("__stdcall","XInputGetState","uint32",["uint32",koffi.out(koffi.pointer(stateType))]);
const bits = [0x1000,0x2000,0x4000,0x8000,0x100,0x200,0,0,0x20,0x10,0x40,0x80,1,2,4,8];
const previous = new Map<number,{mask:number; before:number}>();
const nowUs = () => Number(process.hrtime.bigint()/1000n);
// Windows otherwise commonly rounds setInterval(1) to ~15.6 ms.
// Balance the per-process timer request when this measurement worker stops.
let endPeriod: (() => void) | undefined;
try {
  const mm = koffi.load("winmm.dll");
  const begin = mm.func("__stdcall","timeBeginPeriod","uint32",["uint32"]);
  const end = mm.func("__stdcall","timeEndPeriod","uint32",["uint32"]);
  if (begin(1) === 0) endPeriod = () => { end(1); };
} catch { /* Actual polling bounds still expose coarse scheduling. */ }
let lastPost = 0;
const timer = setInterval(() => {
  const samples = [];
  let changed = false;
  for (let slot=0;slot<4;slot++) {
    const state:any = {};
    const beforeUs=nowUs();
    const result=read(slot,state);
    const afterUs=nowUs();
    if (result !== 0 || !state.Gamepad) {
      if (previous.delete(slot)) changed=true;
      continue;
    }
    const g=state.Gamepad;
    let standardMask=0;
    bits.forEach((bit,index) => { if (bit && (g.wButtons & bit)) standardMask |= 1<<index; });
    if (g.bLeftTrigger>=128) standardMask |= 1<<6;
    if (g.bRightTrigger>=128) standardMask |= 1<<7;
    const old=previous.get(slot);
    if (!old || old.mask!==standardMask) changed=true;
    samples.push({slot,standardMask,beforeUs,afterUs,previousBeforeUs:old?.before ?? beforeUs});
    previous.set(slot,{mask:standardMask,before:beforeUs});
  }
  const at=nowUs();
  if (changed || at-lastPost>=20_000) {
    parentPort?.postMessage({samples,at}); lastPost=at;
  }
},1);
parentPort?.on("message", (message) => {
  if (message !== "stop") return;
  clearInterval(timer); endPeriod?.(); parentPort?.close();
});
