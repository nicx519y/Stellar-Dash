import type { ButtonLatencyEvent, DebugConfig, DebugConfigStatus, MonitorEvent, UsbStatisticsEvent } from "../../shared/monitor-types";

const CONTROL=0x31434d55, STATS=0x31534d55, EDGE=0x31454d55;
const u32=(b:Uint8Array,n:number)=>(b[n]|b[n+1]<<8|b[n+2]<<16|b[n+3]<<24)>>>0;
const u16=(b:Uint8Array,n:number)=>b[n]|b[n+1]<<8;
function crc(b:Uint8Array,n:number) { let c=65535;for(let i=0;i<n;i++){c^=b[i]<<8;for(let k=0;k<8;k++)c=c&32768?((c<<1)^0x1021)&65535:(c<<1)&65535;}return c; }
function report(raw:Uint8Array,magic:number) {
  const b=raw.length===33 && raw[0]===0 ? raw.subarray(1):raw;
  return b.length===32 && u32(b,0)===magic && b[4]===1 ? b:null;
}
const mapping=[12,13,14,15,0,1,2,3,4,5,6,7,8,9,10,11,16];
const standard=(mask:number)=>mapping.reduce((sum,bit,i)=>sum|((mask&(1<<i))?1<<bit:0),0)>>>0;
type Edge={revision:number;flags:number;pages:number;at:number;event:ButtonLatencyEvent;values:Array<number|null>};
type Control={session:number;flags:number;period:number;transaction:number;status:number};
function parseControl(raw:Uint8Array):Control|null {
  const b=report(raw,CONTROL);if(!b || crc(b,30)!==u16(b,30) || (b[19]&3)!==3)return null;
  return {session:u32(b,12),flags:b[7],period:u16(b,16),transaction:u32(b,8),status:b[6]};
}
let generation=0;
/** One device, one decoder, one lease. No HID arrival frequency enters the rate. */
export class UsbMonitorPeer {
  readonly id: string;
  private sequence=0;
  private commandTail:Promise<void>=Promise.resolve();
  private control:Control|null=null;
  private previous:UsbStatisticsEvent|null=null;
  private rows=new Map<string,Edge>();
  private session:number|null=null;
  private lastAt=0;
  private stale=false;
  private closed=false;
  private desired:DebugConfig|null=null;
  private status:DebugConfigStatus={state:"Idle",rxStatus:"Idle",txStatus:"Idle",lastSeq:0};
  constructor(private handle:any,private publish:(e:MonitorEvent)=>void,id?:string,private selected:()=>boolean=()=>true) {this.id=id??`usb-${++generation}`;}
  suspend() {this.desired=null;}
  private command(op:number,config?:DebugConfig):Promise<Control|null> {
    // SET + GET form one transaction. Initial discovery/configuration can run
    // beside the source maintenance timer, even with its outer control queue.
    const work=this.commandTail.then(()=>this.executeCommand(op,config));
    this.commandTail=work.then(()=>undefined,()=>undefined);
    return work;
  }
  private async executeCommand(op:number,config?:DebugConfig) {
    if(this.closed)return null;
    if(op!==0 && !this.selected())return null;
    const b=Buffer.alloc(32);b.writeUInt32LE(CONTROL);b[4]=1;b[5]=op;
    b[7]=config?.hidTelemetryEnabled ? 1|(config.latencyMeasurementEnabled?2:0):0;
    const transaction=++this.sequence>>>0;
    b.writeUInt32LE(transaction,8);b.writeUInt32LE(this.control?.session??0,12);
    b.writeUInt16LE(config?.hidPeriodMs??250,16);b.writeUInt16LE(crc(b,30),30);
    await this.handle.sendFeatureReport([0,...b]);
    const c=parseControl(Uint8Array.from(await this.handle.getFeatureReport(0,33)));
    if(this.closed || !c || c.transaction!==transaction)return null;
    this.control=c;return c;
  }
  async identify() { try{const ok=!!await this.command(0);if(ok)this.connection("USB 监测就绪");return ok;}catch{return false;} }
  getStatus() { return this.status; }
  private connection(label:string,state:"Connected"|"Disconnected"="Connected") {
    this.publish({kind:"device_status",timestampMs:Date.now(),mode:"USB",sourceMode:"USB",deviceId:this.id,
      state,statusLabel:label,targetRateHz:this.previous?.effectiveRateHz??0,actualRateHz:0,rateValid:false});
  }
  async configure(config:DebugConfig) {
    const desired={...config};this.desired=desired;
    try {
      const c=await this.command(1,desired);
      if(this.closed || this.desired!==desired)return this.status;
      const expected=config.hidTelemetryEnabled?1|(config.latencyMeasurementEnabled?2:0):0;
      const ok=c?.status===0 && c.flags===expected && c.period===config.hidPeriodMs;
      this.status={state:ok?"Applied":"Failed",rxStatus:"Idle",txStatus:ok?"Applied":"Failed",lastSeq:this.sequence,
        message:ok?undefined:"USB 监测配置未确认"};
      if(ok && !config.hidTelemetryEnabled) {this.previous=null;this.connection("USB 统计已关闭");}
    } catch(error) {
      if(!this.closed && this.desired===desired)this.failed("USB 控制通信失败",error);
    }
    return this.status;
  }
  private failed(message:string,error:unknown) {
    const detail=error instanceof Error?error.message:String(error);
    this.status={state:"Failed",rxStatus:"Idle",txStatus:"Failed",lastSeq:this.sequence,
      message:`${message}：${detail.slice(0,160)}`};
  }
  async maintain() {
    if(this.closed)return;
    this.expire();
    const desired=this.desired;
    if(!desired?.hidTelemetryEnabled)return;
    try {
      const c=await this.command(2);
      if(this.closed || this.desired!==desired)return;
      const expected=1|(desired.latencyMeasurementEnabled?2:0);
      if(!c || c.status!==0 || c.flags!==expected || c.period!==desired.hidPeriodMs) {
        await this.configure(desired);
      } else {
        // A confirmed renewal recovers a transient failure in the UI too.
        this.status={state:"Applied",rxStatus:"Idle",txStatus:"Applied",lastSeq:c.transaction};
      }
    } catch(error) {
      if(!this.closed && this.desired===desired)this.failed("USB 监测续期失败",error);
    }
  }
  expire(now=Date.now()) {
    if(this.lastAt && now-this.lastAt>2000 && !this.stale) {
      this.stale=true;this.previous=null;this.connection("USB 统计停更");
    }
  }
  parse(raw:Uint8Array,now=Date.now()):boolean {
    const s=report(raw,STATS),b=s??report(raw,EDGE);if(!b)return false;
    const session=u32(b,8);
    if(this.session!==session) {this.session=session;this.previous=null;this.rows.clear();}
    if(s) {
      const current:UsbStatisticsEvent={kind:"usb_statistics",sourceMode:"USB",deviceId:this.id,timestampMs:now,session,
        deviceUs:u32(s,12),received:u32(s,16),completed:u32(s,20),overwritten:u32(s,24),
        effectiveRateHz:u16(s,28),speed:s[6],diagnosticDrops:u16(s,30),rateHz:null,receivedHz:null};
      const prev=this.previous;
      if(prev && now-prev.timestampMs<=2000) {
        const dt=(current.deviceUs-prev.deviceUs)>>>0,dc=(current.completed-prev.completed)>>>0,dr=(current.received-prev.received)>>>0;
        if(dt>0 && dt<=2000000 && dc<=dt/1000000*10000+32 && dr<=dt/1000000*10000+32) {
          current.rateHz=dc*1000000/dt;current.receivedHz=dr*1000000/dt;
        }
      }
      // Repeated snapshots do not erase the baseline or add a point.
      if(prev && current.deviceUs===prev.deviceUs)return true;
      this.previous=current;this.lastAt=now;this.stale=false;
      this.publish(current);
      this.publish({kind:"device_status",sourceMode:"USB",deviceId:this.id,timestampMs:now,mode:"USB",
        state:"Connected",targetRateHz:current.effectiveRateHz,actualRateHz:current.rateHz??0,rateValid:current.rateHz!==null,
        statusLabel:s[6]===2?"USB HS":s[6]===1?"USB FS":s[6]===3?"USB 挂起":"USB 未配置"});
      return true;
    }
    const page=b[5],revision=b[6],flags=b[7],event=u32(b,12),previous=u32(b,16),mask=u32(b,20);
    if(page>3 || mask===previous)return true;
    const key=`${this.id}:${session}:${event}`;
    let row=this.rows.get(key);
    if(row && (row.event.keyMask!==mask || row.event.previousStandardMask!==standard(previous)))return true;
    if(row && row.revision!==revision && ((revision-row.revision)&255)>=128)return true;
    if(!row) {
      row={revision,flags,pages:0,at:now,values:Array(8).fill(null),event:{kind:"button_latency",timestampMs:now,
        sourceMode:"USB",deviceId:this.id,session,inputSeq:event,keyMask:mask,standardMask:standard(mask),previousStandardMask:standard(previous),
        action:mask&~previous ? (previous&~mask ? "change":"press"):"release",measurement:"usb",traceId:key,
        latencyMs:null,sampleTickUs:0,samplePcUs:0,xinputPcUs:0,confidence:"low",latencyFrame:"UME1"}};
      this.rows.set(key,row);if(this.rows.size>300)this.rows.delete(this.rows.keys().next().value!);
    }
    if(row.revision!==revision) {row.revision=revision;row.flags=flags;row.pages=0;row.values=Array(8).fill(null);row.at=now;}
    if(row.flags!==flags)return true;
    if(now-row.at>2000) {row.pages=0;row.values=Array(8).fill(null);row.at=now;}
    for(let i=0;i<2;i++){const v=u32(b,24+4*i);row.values[page*2+i]=v<=1000000?v:null;}
    row.pages|=1<<page;
    const complete=row.pages===15 && (flags&7)===7 && !(flags&0xf8) && row.values.every(v=>v!==null) && row.values[7]!>=row.values[6]!;
    const reason=flags&8?"待发送状态被覆盖":flags&32?"输入发送失败":flags&64?"来源无法关联":flags&16?"USB 完成超时":!(flags&1)?"等待 STM32 来源":!(flags&2)?"等待 USB 完成":!(flags&4)?"时钟校准不可用":"等待完整分页";
    row.event={...row.event,relativeStagesUs:[...row.values.slice(0,6),null,null],latencyMs:complete?(row.values[6]!+row.values[7]!)/2000:null,
      latencyMinMs:complete?row.values[6]!/1000:undefined,latencyMaxMs:complete?row.values[7]!/1000:undefined,
      measurementReason:complete?"采样 → USB IN 完成；包含时钟误差":reason,confidence:complete?"medium":"low"};
    this.publish(row.event);
    this.publish({kind:"button_latency_status",sourceMode:"USB",deviceId:this.id,timestampMs:now,status:complete?"Live":"Waiting edge"});
    return true;
  }
  async close(release=true) {
    if(release && this.desired) {try{await this.configure({...this.desired,hidTelemetryEnabled:false});}catch{}}
    this.closed=true;this.previous=null;this.rows.clear();this.connection("USB 已断开","Disconnected");
  }
}
