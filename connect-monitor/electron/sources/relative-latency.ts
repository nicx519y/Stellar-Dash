import type { ButtonLatencyEvent, MonitorEvent } from "../../shared/monitor-types";

const U32 = (b: Uint8Array, n: number) => (b[n] | b[n+1]<<8 | b[n+2]<<16 | b[n+3]<<24) >>> 0;
const U16 = (b: Uint8Array, n: number) => b[n] | b[n+1]<<8;
const map = [12,13,14,15,0,1,2,3,4,5,6,7,8,9,10,11,16];
const standard = (mask: number) => map.reduce((s,b,i) => s | ((mask & (1<<i)) ? 1<<b : 0), 0) >>> 0;
type Entry = { revision: number; pages: number; event: ButtonLatencyEvent; stages: Array<number | null>; flags: number };

/** Firmware-local durations only. HID delivery timestamps never enter a sum. */
export class RelativeLatencyDecoder {
  private rows = new Map<string, Entry>();
  private generation = 0;
  reset() { this.rows.clear(); this.generation++; }
  parse(raw: Uint8Array, now=Date.now()): MonitorEvent[] | null {
    const b = raw.length===33 && U32(raw,1)===0x32544c52 ? raw.subarray(1) : raw;
    if(b.length!==32 || U32(b,0)!==0x32544c52)return null;
    const session=U16(b,4), row=U16(b,6), mask=b[8]|b[9]<<8|b[10]<<16;
    const current=standard(mask), previous=standard(b[13]|b[14]<<8|b[15]<<16);
    // Compare the states carried by this event, not the last arriving HID page:
    // revisions/pages may arrive late. A new trace tag alone is not a button edge.
    // Missing timing stages must not hide an actual press or release.
    if(current===previous)return [];
    const revision=b[11]>>>1, page=b[11]&1, flags=b[12];
    const key=`usb:${this.generation}:${session}:${row}`;
    let e=this.rows.get(key);
    if(!e) {
      e={revision,pages:0,flags,stages:Array(8).fill(null),event:{
        kind:"button_latency", timestampMs:now,inputSeq:row,keyMask:mask,standardMask:current,
        previousStandardMask:previous,action:(current&~previous) ? ((previous&~current) ? "change" : "press") : "release",
        traceId:key,measurement:"usb",latencyMs:null,sampleTickUs:0,samplePcUs:0,xinputPcUs:0,confidence:"low",
      }};
      this.rows.set(key,e);
      if(this.rows.size>300)this.rows.delete(this.rows.keys().next().value!);
    }
    if(revision!==e.revision) {
      if(((revision-e.revision)&127)>64)return [];
      e.revision=revision;e.pages=0;e.flags=flags;e.stages=Array(8).fill(null);
    }
    if(flags!==e.flags || mask!==e.event.keyMask)return [];
    for(let i=0;i<4;i++) {
      const valid=page===0 ? !!(flags&1) : i===0 ? !!(flags&2) : i===1 ? true : !!(flags&4);
      const value=U32(b,16+4*i);
      e.stages[page*4+i]=valid && value<=1_000_000 ? value : null;
    }
    e.pages|=1<<page;
    const complete=e.pages===3 && (flags&15)===7 && e.stages.every(v=>v!==null);
    const total=complete ? e.stages.reduce<number>((sum,v)=>sum+(v??0),0)/1000 : null;
    const event: ButtonLatencyEvent={...e.event,relativeStagesUs:[...e.stages],latencyMs:total,
      measurementReason:complete ? "RF/IRQ boundary estimated" : flags&8 ? "Invalid/expired record" : !(flags&4) ? (flags&64 ? "USB completion timeout" : "Waiting USB completion") : !(flags&2) ? ((flags&1) ? "TX timing unavailable" : (flags&32) ? "TX attempt mismatch" : flags&64 ? "Source record timeout" : "Source record missing") : "Waiting metadata",
      confidence:complete ? "medium" : "low"};
    e.event=event;
    return [event,{kind:"button_latency_status",timestampMs:now,status:complete ? "Live" : "Waiting edge"}];
  }
}
