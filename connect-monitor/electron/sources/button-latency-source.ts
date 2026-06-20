import type { ButtonLatencyEvent, ButtonLatencyStatusEvent, MonitorEvent, PacketEvent } from "../../shared/monitor-types";

type PublishFn = (event: MonitorEvent) => void;

type PendingInput = {
  inputSeq: number;
  keyMask: number;
  standardMask: number;
  previousStandardMask: number;
  sampleTickUs: number;
  samplePcUs: number;
  syncRttUs?: number;
  createdAtUs: number;
};

type SyncSample = {
  stmMidUs: number;
  pcMidUs: number;
  rttUs: number;
};

type XinputSnapshot = {
  slot: number;
  packetNumber: number;
  standardMask: number;
  pcUs: number;
};

const MAX_SYNC_SAMPLES = 32;
const MAX_PENDING_INPUTS = 64;
const PENDING_MAX_AGE_US = 1_000_000;
const STATUS_INTERVAL_MS = 500;
const UINT32_WRAP = 0x1_0000_0000;
const UINT32_HALF = 0x8000_0000;

const hboxToStandardButton = new Map<number, number>([
  [0, 12],
  [1, 13],
  [2, 14],
  [3, 15],
  [4, 0],
  [5, 1],
  [6, 2],
  [7, 3],
  [8, 4],
  [9, 5],
  [10, 6],
  [11, 7],
  [12, 8],
  [13, 9],
  [14, 10],
  [15, 11],
  [16, 16],
]);

function nowMonoUs() {
  return Number(process.hrtime.bigint() / 1000n);
}

function hboxMaskToStandardMask(keyMask: number) {
  let standardMask = 0;
  for (const [hboxBit, standardBit] of hboxToStandardButton) {
    if ((keyMask & (1 << hboxBit)) !== 0) {
      standardMask |= 1 << standardBit;
    }
  }
  return standardMask >>> 0;
}

function describeAction(previousMask: number, nextMask: number): ButtonLatencyEvent["action"] {
  const changed = (previousMask ^ nextMask) >>> 0;
  let pressed = 0;
  let released = 0;
  for (let bit = 0; bit < 17; bit += 1) {
    if ((changed & (1 << bit)) === 0) continue;
    if ((nextMask & (1 << bit)) !== 0) pressed += 1;
    else released += 1;
  }
  if (pressed > 0 && released === 0) return "press";
  if (released > 0 && pressed === 0) return "release";
  return "change";
}

class ButtonLatencyTracker {
  private syncSamples: SyncSample[] = [];
  private syncRequests = new Map<number, number>();
  private pendingInputs: PendingInput[] = [];
  private recentXinput: XinputSnapshot[] = [];
  private lastInputStandardMask: number | null = null;
  private lastExtendedStmUs: number | null = null;
  private fitA = 1;
  private fitB = 0;
  private locked = false;
  private lastStatus = "";
  private lastStatusAt = 0;
  private latestRttUs: number | undefined;
  private selectedSlot: number | null = null;

  reset() {
    this.syncSamples = [];
    this.syncRequests.clear();
    this.pendingInputs = [];
    this.recentXinput = [];
    this.lastInputStandardMask = null;
    this.lastExtendedStmUs = null;
    this.fitA = 1;
    this.fitB = 0;
    this.locked = false;
    this.latestRttUs = undefined;
    this.selectedSlot = null;
  }

  isClockLocked() {
    return this.locked;
  }

  noteTimeSyncSent(seq: number, pcT0Us: number) {
    this.syncRequests.set(seq & 0xff, pcT0Us);
    if (this.syncRequests.size > 16) {
      const first = this.syncRequests.keys().next().value;
      if (typeof first === "number") this.syncRequests.delete(first);
    }
  }

  handleLatencyPacket(packet: PacketEvent, publish: PublishFn) {
    const latencyUs = typeof packet.latencyUs === "number" ? packet.latencyUs : packet.sampleTickUs;
    if (typeof latencyUs !== "number" || latencyUs === 0 || typeof packet.inputKeyMask !== "number") {
      this.publishStatus("Waiting edge", publish);
      return;
    }
    const standardMask = hboxMaskToStandardMask(packet.inputKeyMask);
    const previousStandardMask = this.lastInputStandardMask ?? (standardMask === 0 ? standardMask : 0);
    this.lastInputStandardMask = standardMask;
    if (((previousStandardMask ^ standardMask) >>> 0) === 0) {
      this.publishStatus("Live", publish);
      return;
    }
    publish({
      kind: "button_latency",
      timestampMs: Date.now(),
      inputSeq: packet.inputSeq ?? 0,
      keyMask: packet.inputKeyMask >>> 0,
      standardMask,
      previousStandardMask,
      action: describeAction(previousStandardMask, standardMask),
      latencyMs: latencyUs / 1000,
      sampleTickUs: latencyUs,
      samplePcUs: 0,
      xinputPcUs: 0,
      confidence: "high",
    });
    this.publishStatus("Live", publish, true);
  }

  handleXinputSnapshot(snapshot: XinputSnapshot, publish: PublishFn) {
    if (this.selectedSlot !== null && snapshot.slot !== this.selectedSlot) return;
    this.recentXinput.push(snapshot);
    const recentCutoffUs = snapshot.pcUs - PENDING_MAX_AGE_US;
    this.recentXinput = this.recentXinput.filter((item) => item.pcUs >= recentCutoffUs);
    const cutoffUs = snapshot.pcUs - PENDING_MAX_AGE_US;
    this.pendingInputs = this.pendingInputs.filter((item) => item.createdAtUs >= cutoffUs);
    if (!this.tryMatchPendingInput(publish, snapshot)) {
      if (this.locked) this.publishStatus("No match", publish);
    }
  }

  private tryMatchPendingInput(publish: PublishFn, preferredSnapshot?: XinputSnapshot) {
    for (let inputIndex = 0; inputIndex < this.pendingInputs.length; inputIndex += 1) {
      const pending = this.pendingInputs[inputIndex];
      const snapshot = preferredSnapshot && preferredSnapshot.standardMask === pending.standardMask
        ? preferredSnapshot
        : this.recentXinput.find((item) =>
            item.standardMask === pending.standardMask &&
            item.pcUs >= pending.samplePcUs - 2_000 &&
            item.pcUs <= pending.createdAtUs + PENDING_MAX_AGE_US);
      if (!snapshot) continue;
      this.selectedSlot = snapshot.slot;
      this.publishLatencyMatch(pending, snapshot, publish);
      this.pendingInputs.splice(0, inputIndex + 1);
      return true;
    }
    return false;
  }

  private publishLatencyMatch(match: PendingInput, snapshot: XinputSnapshot, publish: PublishFn) {
    const latencyMs = Math.max(0, (snapshot.pcUs - match.samplePcUs) / 1000);
    const confidence: ButtonLatencyEvent["confidence"] =
      typeof match.syncRttUs === "number" && match.syncRttUs <= 1500 ? "high" :
        typeof match.syncRttUs === "number" && match.syncRttUs <= 5000 ? "medium" : "low";
    publish({
      kind: "button_latency",
      timestampMs: Date.now(),
      inputSeq: match.inputSeq,
      keyMask: match.keyMask,
      standardMask: match.standardMask,
      previousStandardMask: match.previousStandardMask,
      action: describeAction(match.previousStandardMask, match.standardMask),
      latencyMs,
      sampleTickUs: match.sampleTickUs,
      samplePcUs: match.samplePcUs,
      xinputPcUs: snapshot.pcUs,
      syncRttUs: match.syncRttUs,
      confidence,
    });
    this.publishStatus("Locked", publish);
  }

  publishStatus(status: ButtonLatencyStatusEvent["status"], publish: PublishFn, force = false) {
    const now = Date.now();
    if (!force && status === this.lastStatus && now - this.lastStatusAt < STATUS_INTERVAL_MS) return;
    this.lastStatus = status;
    this.lastStatusAt = now;
    publish({
      kind: "button_latency_status",
      timestampMs: now,
      status,
      syncRttUs: this.latestRttUs,
      clockSamples: this.syncSamples.length,
    });
  }

  private handleSyncEcho(packet: PacketEvent, pcT3Us: number, publish: PublishFn) {
    if (
      typeof packet.syncSeq !== "number" ||
      typeof packet.syncRxTickUs !== "number" ||
      typeof packet.syncTxTickUs !== "number"
    ) {
      return;
    }
    const pcT0Us = this.syncRequests.get(packet.syncSeq & 0xff);
    if (typeof pcT0Us !== "number") return;
    this.syncRequests.delete(packet.syncSeq & 0xff);

    const stmT1 = this.unwrapStmUs(packet.syncRxTickUs);
    const stmT2 = this.unwrapStmUs(packet.syncTxTickUs);
    const pcMidUs = (pcT0Us + pcT3Us) / 2;
    const stmMidUs = (stmT1 + stmT2) / 2;
    const rttUs = Math.max(0, (pcT3Us - pcT0Us) - Math.max(0, stmT2 - stmT1));
    if (!Number.isFinite(rttUs) || rttUs > 100_000) return;

    this.latestRttUs = rttUs;
    this.syncSamples.push({ stmMidUs, pcMidUs, rttUs });
    if (this.syncSamples.length > MAX_SYNC_SAMPLES) {
      this.syncSamples = this.syncSamples.slice(-MAX_SYNC_SAMPLES);
    }
    this.recomputeFit();
    this.publishStatus(this.locked ? "Locked" : "Syncing", publish, true);
  }

  private recomputeFit() {
    const selected = this.syncSamples
      .slice()
      .sort((a, b) => a.rttUs - b.rttUs)
      .slice(0, Math.min(16, this.syncSamples.length));
    if (selected.length === 0) {
      this.locked = false;
      return;
    }
    if (selected.length === 1) {
      this.fitA = 1;
      this.fitB = selected[0].pcMidUs - selected[0].stmMidUs;
      this.locked = true;
      return;
    }

    const meanX = selected.reduce((sum, sample) => sum + sample.stmMidUs, 0) / selected.length;
    const meanY = selected.reduce((sum, sample) => sum + sample.pcMidUs, 0) / selected.length;
    let numerator = 0;
    let denominator = 0;
    for (const sample of selected) {
      const dx = sample.stmMidUs - meanX;
      numerator += dx * (sample.pcMidUs - meanY);
      denominator += dx * dx;
    }
    this.fitA = denominator > 0 ? numerator / denominator : 1;
    if (!Number.isFinite(this.fitA) || this.fitA < 0.95 || this.fitA > 1.05) {
      this.fitA = 1;
    }
    this.fitB = meanY - this.fitA * meanX;
    this.locked = true;
  }

  private unwrapStmUs(value: number) {
    const raw = value >>> 0;
    if (this.lastExtendedStmUs === null) {
      this.lastExtendedStmUs = raw;
      return raw;
    }
    const base = Math.floor(this.lastExtendedStmUs / UINT32_WRAP) * UINT32_WRAP;
    let candidate = base + raw;
    if (candidate < this.lastExtendedStmUs - UINT32_HALF) candidate += UINT32_WRAP;
    if (candidate > this.lastExtendedStmUs + UINT32_HALF) candidate -= UINT32_WRAP;
    if (candidate > this.lastExtendedStmUs) this.lastExtendedStmUs = candidate;
    return candidate;
  }
}

export const buttonLatencyTracker = new ButtonLatencyTracker();

function xinputMaskFromGamepad(buttons: number, leftTrigger: number, rightTrigger: number) {
  let mask = 0;
  if ((buttons & 0x1000) !== 0) mask |= 1 << 0;
  if ((buttons & 0x2000) !== 0) mask |= 1 << 1;
  if ((buttons & 0x4000) !== 0) mask |= 1 << 2;
  if ((buttons & 0x8000) !== 0) mask |= 1 << 3;
  if ((buttons & 0x0100) !== 0) mask |= 1 << 4;
  if ((buttons & 0x0200) !== 0) mask |= 1 << 5;
  if (leftTrigger >= 30) mask |= 1 << 6;
  if (rightTrigger >= 30) mask |= 1 << 7;
  if ((buttons & 0x0020) !== 0) mask |= 1 << 8;
  if ((buttons & 0x0010) !== 0) mask |= 1 << 9;
  if ((buttons & 0x0040) !== 0) mask |= 1 << 10;
  if ((buttons & 0x0080) !== 0) mask |= 1 << 11;
  if ((buttons & 0x0001) !== 0) mask |= 1 << 12;
  if ((buttons & 0x0002) !== 0) mask |= 1 << 13;
  if ((buttons & 0x0004) !== 0) mask |= 1 << 14;
  if ((buttons & 0x0008) !== 0) mask |= 1 << 15;
  return mask >>> 0;
}

export function startNativeXinputSource(publish: PublishFn): () => void {
  if (process.platform !== "win32") {
    buttonLatencyTracker.publishStatus("No XInput", publish, true);
    return () => {};
  }

  let koffi: any;
  try {
    koffi = require("koffi");
  } catch (_err) {
    buttonLatencyTracker.publishStatus("No XInput", publish, true);
    return () => {};
  }

  let lib: any;
  try {
    lib = koffi.load("xinput1_4.dll");
  } catch (_err) {
    try {
      lib = koffi.load("xinput9_1_0.dll");
    } catch (_fallbackErr) {
      buttonLatencyTracker.publishStatus("No XInput", publish, true);
      return () => {};
    }
  }

  let XINPUT_GAMEPAD: any;
  let XINPUT_STATE: any;
  let XInputGetState: any;
  try {
    XINPUT_GAMEPAD = koffi.struct("XINPUT_GAMEPAD", {
      wButtons: "uint16",
      bLeftTrigger: "uint8",
      bRightTrigger: "uint8",
      sThumbLX: "int16",
      sThumbLY: "int16",
      sThumbRX: "int16",
      sThumbRY: "int16",
    });
    XINPUT_STATE = koffi.struct("XINPUT_STATE", {
      dwPacketNumber: "uint32",
      Gamepad: XINPUT_GAMEPAD,
    });
    XInputGetState = lib.func("uint32 XInputGetState(uint32 dwUserIndex, _Out_ XINPUT_STATE *pState)");
  } catch (_err) {
    buttonLatencyTracker.publishStatus("No XInput", publish, true);
    return () => {};
  }

  const lastMasks = new Map<number, number>();
  const lastPackets = new Map<number, number>();
  const timer = setInterval(() => {
    let anyConnected = false;
    for (let slot = 0; slot < 4; slot += 1) {
      const state: any = {};
      let result = 1;
      try {
        result = XInputGetState(slot, state);
      } catch (_err) {
        continue;
      }
      if (result !== 0 || !state.Gamepad) continue;
      anyConnected = true;
      const packetNumber = Number(state.dwPacketNumber ?? 0);
      const previousPacket = lastPackets.get(slot);
      if (previousPacket === packetNumber) continue;
      lastPackets.set(slot, packetNumber);
      const mask = xinputMaskFromGamepad(
        Number(state.Gamepad.wButtons ?? 0),
        Number(state.Gamepad.bLeftTrigger ?? 0),
        Number(state.Gamepad.bRightTrigger ?? 0),
      );
      if (lastMasks.get(slot) === mask) continue;
      lastMasks.set(slot, mask);
      buttonLatencyTracker.handleXinputSnapshot({
        slot,
        packetNumber,
        standardMask: mask,
        pcUs: nowMonoUs(),
      }, publish);
    }
    if (!anyConnected) {
      buttonLatencyTracker.publishStatus("No XInput", publish);
    }
  }, 1);

  return () => {
    clearInterval(timer);
  };
}

export function monotonicNowUsForMonitor() {
  return nowMonoUs();
}
