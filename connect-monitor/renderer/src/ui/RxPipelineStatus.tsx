import type {PacketEvent} from "../../../shared/monitor-types";
import {rxProfileDelta,rxQuantile,rxTimings,type RxTiming} from "../../../shared/rx-profile";
const labels:Record<typeof rxTimings[number],string>={rearm:"接收重启",parse:"收到→解析",ready:"收到→报告准备",
  rf:"RF 回调经过时间",usb:"USB 中断经过时间",background:"后台经过时间",critical:"已插桩关中断区",
  serviceGap:"主循环服务间隔",queueWait:"报告准备→提交",inFlight:"提交→完成",rfCpu:"RF 扣除已记录抢占",
  usbCpu:"USB 扣除已记录抢占",backgroundCpu:"后台扣除已记录抢占",timer:"定时器中断经过时间"};
const quant=(t:RxTiming,p:number)=>{const n=rxQuantile(t,p);return n===null?"—":Number.isFinite(n)?`≤${n}`:">1000";};
export function RxPipelineStatus({packets,now}:{packets:PacketEvent[];now:number}){
  const list=packets.filter(p=>p.rxProfile).slice(-2),last=list.at(-1);
  const p=last?.rxProfile;
  if(!p || now-last!.timestampMs>3500)return <div style={{padding:"4px 12px",fontSize:12}}>RX 流水线：不可用或统计过期（需配套诊断固件的完整快照）</div>;
  const delta=list.length===2?rxProfileDelta(list[0].rxProfile!,p):null;
  const rate=(key:"received"|"accepted"|"ready"|"submitted"|"completed")=>delta?(delta.delta[key]/delta.seconds).toFixed(0):"—";
  return <details style={{padding:"4px 12px",fontSize:12}}>
    <summary>RX {p.pipeline?"直通流水线":"计时基线"} · USB {p.speed===2?"HS":p.speed===1?"FS（不满足 8K）":p.speed===3?"挂起":"未配置"} · 接收/准备/完成 {rate("received")}/{rate("ready")}/{rate("completed")} Hz · 队列 {p.depth}/4，峰值 {p.highwater}</summary>
    <div>接纳 {rate("accepted")} Hz · 提交 {rate("submitted")} Hz · 以下耗时与计数为诊断累计；分位数显示直方图桶上界，不能作为实机验收自动结论。</div>
    <div>拥塞丢弃 {p.counters.congestionDropped} · 不同状态覆盖 {p.counters.changedOverwritten} · 同状态合并 {p.counters.sameMerged} · 版本拒绝 {p.counters.rejected} · 复位取消 {p.counters.cancelled} · 提交失败 {p.counters.submitFailed} · 辅助队列丢弃 {p.counters.auxDropped}</div>
    <div>CRC {p.counters.crc} · 控制包 {p.counters.control} · 最近丢弃 {p.dropGeneration}:{p.lastDrop} · 读计数器最小间隔 {p.readCycles} cycles（{p.cyclesPerUs} cycles/µs）</div>
    <table style={{width:"100%",textAlign:"right"}}><thead><tr><th style={{textAlign:"left"}}>阶段 / µs</th><th>P50</th><th>P95</th><th>P99</th><th>最大</th><th>&gt;125 次数</th></tr></thead>
      <tbody>{rxTimings.map(k=><tr key={k}><td style={{textAlign:"left"}}>{labels[k]}</td><td>{quant(p.timings[k],.5)}</td><td>{quant(p.timings[k],.95)}</td><td>{quant(p.timings[k],.99)}</td><td>{p.timings[k].count?p.timings[k].maxUs:"—"}</td><td>{p.timings[k].over125}</td></tr>)}</tbody></table>
    <div>RF 回调之前的 SDK 时间与未插桩中断不包含在 CPU 估计内。读计数器间隔不是整套诊断开销；没有从延迟中减去常数。</div>
    <div>最近超时/丢弃：{p.spikes.filter(s=>s.us).map(s=>`${s.generation}:${s.id} ${s.us===0xffffffff?"拥塞丢弃":s.us===0xfffffffe?"复位取消":`${s.us}µs`}`).join(" · ")||"无"}</div>
  </details>;
}
