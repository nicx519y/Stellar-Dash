import type { ButtonLatencyEvent, ButtonLatencyStatusEvent, MonitorEvent, PacketEvent } from "../../shared/monitor-types";
import { Worker } from "node:worker_threads";
import path from "node:path";
type PublishFn = (event: MonitorEvent) => void;
type ClockSample = { lo: number; hi: number; at: number };
type Observation = { slot: number; previous: number; mask: number; lo: number; hi: number; used: boolean };
type Pending = { seq: number; keyMask: number; mask: number; previous: number; sample: number; lo: number; hi: number; at: number };
export type NativePoll = { slot: number; standardMask: number; beforeUs: number; afterUs: number; previousBeforeUs: number };
const MAX_LATENCY_US = 100_000;
const MAX_METADATA_US = 50_000;
const CLOCK_TTL_US = 8_000_000;
const HALF = 0x80000000;
const WRAP = 0x100000000;
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

export function monotonicNowUsForMonitor() {
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

export class ButtonLatencyTracker {
  private requests = new Map<number, number>();
  private clocks: ClockSample[] = [];
  private deviceTime: number | null = null;
  private edges: Pending[] = [];
  private observations: Observation[] = [];
  private lastTrace: {seq: number; sample: number; mask: number} | null = null;
  private legacyMask: number | null = null;
  private nativeMasks = new Map<number, number>();
  private drops: number | undefined;
  private lastSyncRtt: number | undefined;
  private clockWidth: number | undefined;
  private session = 0;
  private lastStatus = "";
  private lastStatusAt = 0;

  reset() {
    this.session++;
    this.requests.clear(); this.clocks = []; this.deviceTime = null;
    this.edges = []; this.observations = []; this.lastTrace = null;
    this.nativeMasks.clear(); this.legacyMask = null;
    this.lastSyncRtt = undefined; this.clockWidth = undefined;
    this.drops = undefined;
  }
  syncIntervalMs(now = monotonicNowUsForMonitor()) {
    return this.offset(now) ? 137 : 17;
  }
  noteTimeSyncSent(seq: number, pcT0Us: number) {
    this.requests.set(seq & 255, pcT0Us);
    for (const [key, at] of this.requests) if (pcT0Us - at > 1_000_000) this.requests.delete(key);
  }
  private unwrap(raw: number) {
    raw >>>= 0;
    if (this.deviceTime === null) return this.deviceTime = raw;
    let candidate = Math.floor(this.deviceTime / WRAP) * WRAP + raw;
    if (candidate < this.deviceTime - HALF) candidate += WRAP;
    if (candidate > this.deviceTime + HALF) candidate -= WRAP;
    if (candidate > this.deviceTime) this.deviceTime = candidate;
    return candidate;
  }
  private offset(now: number): {lo: number; hi: number} | null {
    this.clocks = this.clocks.filter(s => now - s.at < CLOCK_TTL_US);
    if (!this.clocks.length) return null;
    // Explicit oscillator allowance (250 ppm) plus timestamp capture margin.
    const lo = Math.max(...this.clocks.map(s => s.lo - Math.abs(now-s.at)*0.00025 - 20));
    const hi = Math.min(...this.clocks.map(s => s.hi + Math.abs(now-s.at)*0.00025 + 20));
    this.clockWidth = hi-lo;
    return hi >= lo && hi-lo <= 10_000 ? {lo,hi} : null;
  }
  handleTracePacket(packet: PacketEvent, publish: PublishFn) {
    const now = packet.hostMonoUs;
    if (typeof now !== "number") return;
    if (this.drops !== undefined && this.drops !== packet.traceDrops) {
      this.edges = []; this.lastTrace = null;
    }
    this.drops = packet.traceDrops;
    if (packet.messageType === "RFH_RHC3" || packet.messageType === "RFH_RHC4") {
      if (packet.syncSeq === undefined || packet.syncRxTickUs === undefined || packet.syncTxTickUs === undefined) return;
      const start = this.requests.get(packet.syncSeq);
      if (start === undefined) return;
      this.requests.delete(packet.syncSeq);
      const receive = this.unwrap(packet.syncRxTickUs), send = this.unwrap(packet.syncTxTickUs);
      if (send < receive || now < start || now-start > 1_000_000) return;
      // No assumption that outgoing and return delays are symmetric.
      const wait = packet.messageType === "RFH_RHC4" ? packet.syncQueueWaitUs : 0;
      if (wait === undefined || !Number.isFinite(wait) || wait < 0 || wait > now-start-(send-receive)) return;
      const drift = (now-start+wait)*0.00025;
      // RX measured this residence BEFORE the first ACK transmission. Removing
      // known residence tightens the lower bound; no path symmetry is assumed.
      const sample = {lo: start+wait-receive-drift, hi: now-send+drift, at: now};
      if (sample.hi < sample.lo) return;
      const old = this.offset(now);
      if (old && (sample.lo > old.hi || sample.hi < old.lo)) {
        this.clocks = []; this.edges = []; this.observations = []; this.lastTrace = null;
      }
      this.clocks.push(sample); this.clocks = this.clocks.slice(-64);
      this.lastSyncRtt = now-start;
      const locked = this.offset(now);
      publish({kind:"button_latency_status",timestampMs:Date.now(),status:locked ? "Locked" : "Syncing",
        clockSamples:this.clocks.length,clockWidthUs:this.clockWidth,syncRttUs:now-start,
        syncRequestSeq:packet.syncSeq,syncPcSendUs:start,syncPcReceiveUs:now});
      return;
    }
    if (packet.sampleTickUs === undefined || packet.inputKeyMask === undefined || packet.inputSeq === undefined) return;
    const sample = this.unwrap(packet.sampleTickUs), seq = packet.inputSeq;
    const mask = hboxMaskToStandardMask(packet.inputKeyMask);
    const previous = this.lastTrace;
    if (previous && seq === previous.seq && sample === previous.sample) return;
    if (previous && sample < previous.sample) return; // reordered or old session
    this.lastTrace = {seq,sample,mask};
    if (!previous || packet.traceBaseline) { this.edges = []; return; }
    if (((seq-previous.seq)&255) !== 1) {
      this.edges = [];
      if (previous.mask !== mask) this.reportUnavailable(seq,packet.inputKeyMask,mask,previous.mask,sample,"Trace gap",publish);
      return;
    }
    if (previous.mask === mask) return;
    const clock = this.offset(now);
    if (!clock || this.nativeMasks.size === 0) {
      this.reportUnavailable(seq,packet.inputKeyMask,mask,previous.mask,sample,clock ? "No XInput" : "Syncing",publish);
      this.publishStatus(clock ? "No XInput" : "Syncing", publish); return;
    }
    // Cover oscillator drift between the edge and metadata arrival too.
    const margin = MAX_METADATA_US*0.00025;
    const lo = sample+clock.lo-margin, hi = sample+clock.hi+margin;
    if (lo > now+20 || now-lo > MAX_METADATA_US) {
      this.reportUnavailable(seq,packet.inputKeyMask,mask,previous.mask,sample,"Trace late",publish); return;
    }
    this.reportUnavailable(seq,packet.inputKeyMask,mask,previous.mask,sample,"Matching",publish);
    this.edges.push({seq,keyMask:packet.inputKeyMask,mask,previous:previous.mask,sample,lo,hi,at:now});
    this.edges = this.edges.slice(-64);
    this.flush(now, publish);
  }
  handleNativePoll(samples: NativePoll[], now: number, publish: PublishFn) {
    // Track every slot independently. A second idle controller must not disable
    // measurement; indistinguishable transitions across slots remain ambiguous.
    const connected = new Set(samples.map(s => s.slot));
    for (const slot of this.nativeMasks.keys()) {
      if (!connected.has(slot)) this.nativeMasks.delete(slot);
    }
    this.observations = this.observations.filter(o => connected.has(o.slot));
    for (const s of samples) {
      const previous = this.nativeMasks.get(s.slot);
      if (previous !== undefined && previous !== s.standardMask) {
        this.observations.push({slot:s.slot,previous,mask:s.standardMask,
          lo:s.previousBeforeUs,hi:s.afterUs,used:false});
      }
      this.nativeMasks.set(s.slot,s.standardMask);
    }
    if (!samples.length) this.publishStatus("No XInput", publish);
    this.observations = this.observations.filter(o => now-o.hi < 2_000_000).slice(-256);
    this.flush(now,publish);
  }
  private reportUnavailable(seq:number,keyMask:number,mask:number,previous:number,sample:number,reason:string,publish:PublishFn) {
    publish({kind:"button_latency",timestampMs:Date.now(),inputSeq:seq,keyMask,standardMask:mask,
      previousStandardMask:previous,action:describeAction(previous,mask),measurement:"trace",measurementReason:reason,
      traceId:`${this.session}:${sample}:${seq}`,latencyMs:null,sampleTickUs:sample,samplePcUs:0,xinputPcUs:0,
      confidence:"low",latencyFrame:"RFH_RHE3"});
  }
  private flush(now: number, publish: PublishFn) {
    const ready = this.edges.filter(e => now > Math.max(e.at+MAX_LATENCY_US,e.hi+MAX_LATENCY_US+MAX_METADATA_US));
    this.edges = this.edges.filter(e => !ready.includes(e));
    for (const e of ready) {
      const candidates = this.observations.filter(o => !o.used && o.previous===e.previous && o.mask===e.mask &&
        o.hi >= e.lo && o.lo <= e.hi+MAX_LATENCY_US);
      if (candidates.length !== 1) { this.reportUnavailable(e.seq,e.keyMask,e.mask,e.previous,e.sample,"No match",publish); this.publishStatus("No match",publish); continue; }
      const o = candidates[0];
      // If another origin can explain this same Windows transition, fail closed.
      const competing = [...ready,...this.edges].filter(other => other !== e && other.previous===e.previous && other.mask===e.mask &&
        o.hi >= other.lo && o.lo <= other.hi+MAX_LATENCY_US);
      if (competing.length) { this.reportUnavailable(e.seq,e.keyMask,e.mask,e.previous,e.sample,"No match",publish); this.publishStatus("No match",publish); continue; }
      o.used = true;
      const low = Math.max(0,o.lo-e.hi), high = o.hi-e.lo;
      if (high < 0 || o.hi-o.lo > 20_000) { this.reportUnavailable(e.seq,e.keyMask,e.mask,e.previous,e.sample,"No match",publish); this.publishStatus("No match",publish); continue; }
      publish({kind:"button_latency",timestampMs:Date.now(),inputSeq:e.seq,keyMask:e.keyMask,
        standardMask:e.mask,previousStandardMask:e.previous,action:describeAction(e.previous,e.mask),
        traceId:`${this.session}:${e.sample}:${e.seq}`,measurement:"windows",latencyMs:(low+high)/2000,latencyMinMs:low/1000,latencyMaxMs:high/1000,
        sampleTickUs:e.sample,samplePcUs:(e.lo+e.hi)/2,xinputPcUs:o.hi,
        syncRttUs:e.hi-e.lo,pollIntervalUs:o.hi-o.lo,
        confidence:high-low<=1000 ? "high" : high-low<=4000 ? "medium" : "low",latencyFrame:"RFH_RHE3"});
      this.publishStatus("Locked",publish);
    }
  }
  publishStatus(status: ButtonLatencyStatusEvent["status"], publish: PublishFn, force=false) {
    const now=Date.now();
    if (!force && status===this.lastStatus && now-this.lastStatusAt<500) return;
    this.lastStatus=status; this.lastStatusAt=now;
    publish({kind:"button_latency_status",timestampMs:now,status,clockSamples:this.clocks.length,syncRttUs:this.lastSyncRtt,clockWidthUs:this.clockWidth});
  }
  handleLatencyPacket(packet: PacketEvent, publish: PublishFn) {
    const latencyUs = typeof packet.latencyUs === "number" ? packet.latencyUs : packet.sampleTickUs;
    if (typeof latencyUs !== "number" || latencyUs === 0 || typeof packet.inputKeyMask !== "number") {
      if (!this.clocks.length) this.publishStatus("Waiting edge", publish);
      return;
    }
    const standardMask = hboxMaskToStandardMask(packet.inputKeyMask);
    const previousStandardMask = this.legacyMask ?? (standardMask === 0 ? standardMask : 0);
    this.legacyMask = standardMask;
    if (((previousStandardMask ^ standardMask) >>> 0) === 0) {
      if (!this.clocks.length) this.publishStatus("Live", publish);
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
      stm32Ms: typeof packet.latencyStm32Us === "number" ? packet.latencyStm32Us / 1000 : undefined,
      txMs: typeof packet.latencyTxUs === "number" ? packet.latencyTxUs / 1000 : undefined,
      rxMs: typeof packet.latencyRxUs === "number" ? packet.latencyRxUs / 1000 : undefined,
      rxIrqMs: typeof packet.latencyRxIrqUs === "number" ? packet.latencyRxIrqUs / 1000 : undefined,
      rxDecodeMs: typeof packet.latencyRxDecodeUs === "number" ? packet.latencyRxDecodeUs / 1000 : undefined,
      rxEpWaitMs: typeof packet.latencyRxEpWaitUs === "number" ? packet.latencyRxEpWaitUs / 1000 : undefined,
      rxSubmitMs: typeof packet.latencyRxSubmitUs === "number" ? packet.latencyRxSubmitUs / 1000 : undefined,
      latencyFrame: packet.messageType,
      sampleTickUs: latencyUs,
      samplePcUs: 0,
      xinputPcUs: 0,
      confidence: "low",
      measurement: "stages",
      latencyStageFlags: packet.latencyStageFlags,
    });
    if (!this.clocks.length) this.publishStatus("Waiting edge", publish);
  }

}
export const buttonLatencyTracker = new ButtonLatencyTracker();
export function startNativeXinputSource(publish: PublishFn): () => void {
  if (process.platform !== "win32") return () => {};
  const worker = new Worker(path.join(__dirname,"xinput-latency-worker.js"));
  worker.on("message", (data: {samples: NativePoll[]; at: number}) => {
    buttonLatencyTracker.handleNativePoll(data.samples,data.at,publish);
  });
  worker.on("error", () => buttonLatencyTracker.publishStatus("No XInput",publish,true));
  return () => {
    worker.postMessage("stop");
    const deadline = setTimeout(() => { void worker.terminate(); }, 1000);
    deadline.unref();
    worker.once("exit", () => clearTimeout(deadline));
  };
}
