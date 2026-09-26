import type { NativeGamepadSnapshot } from "../../shared/monitor-types";

type State = { Gamepad?: { wButtons: number; bLeftTrigger: number; bRightTrigger: number } };
type ReadState = (slot: number, state: State) => number;

export function createNativeGamepadReader(read: ReadState) {
  let preferred = 0;
  return (): NativeGamepadSnapshot => {
    const slots = [preferred, ...[0, 1, 2, 3].filter((slot) => slot !== preferred)];
    for (const slot of slots) {
      const state: State = {};
      let result: number;
      try { result = read(slot, state); } catch { continue; }
      if (result !== 0 || !state.Gamepad) continue;
      preferred = slot;
      const g = state.Gamepad;
      const bits = [0x1000, 0x2000, 0x4000, 0x8000, 0x0100, 0x0200,
        0, 0, 0x0020, 0x0010, 0x0040, 0x0080, 0x0001, 0x0002, 0x0004, 0x0008];
      let standardMask = bits.reduce((mask, bit, index) => mask | ((g.wButtons & bit) ? 1 << index : 0), 0);
      if (g.bLeftTrigger >= 128) standardMask |= 1 << 6;
      if (g.bRightTrigger >= 128) standardMask |= 1 << 7;
      return { connected: true, deviceId: `Windows XInput ${slot + 1}`, standardMask, timestampMs: Date.now() };
    }
    return { connected: false, deviceId: null, standardMask: 0, timestampMs: Date.now() };
  };
}

let reader: (() => NativeGamepadSnapshot) | null | undefined;

export function readNativeGamepad(): NativeGamepadSnapshot | null {
  if (reader === undefined) {
    reader = null;
    if (process.platform === "win32") {
      try {
        const koffi = require("koffi");
        let lib;
        try { lib = koffi.load("xinput1_4.dll"); }
        catch { lib = koffi.load("xinput9_1_0.dll"); }
        const gamepad = koffi.struct({ wButtons: "uint16", bLeftTrigger: "uint8", bRightTrigger: "uint8",
          sThumbLX: "int16", sThumbLY: "int16", sThumbRX: "int16", sThumbRY: "int16" });
        const state = koffi.struct({ dwPacketNumber: "uint32", Gamepad: gamepad });
        const read = lib.func("__stdcall", "XInputGetState", "uint32", ["uint32", koffi.out(koffi.pointer(state))]);
        reader = createNativeGamepadReader(read);
      } catch { /* Browser Gamepad remains available on unsupported systems. */ }
    }
  }
  return reader ? reader() : null;
}
