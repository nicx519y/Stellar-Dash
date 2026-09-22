import { useEffect, useRef, useState } from "react";
import { Box, Text, Button } from "@chakra-ui/react";
import type { PacketEvent } from "../../../shared/monitor-types";
import { fastReasons, fastStates, percentile, type FastRequest } from "../../../shared/fast-recovery";

export function RfFastLab({packets}: {packets: PacketEvent[]}) {
  const [requested,setRequested]=useState(false),[message,setMessage]=useState("");
  const [kind,setKind]=useState(1),[target,setTarget]=useState(1),[channel,setChannel]=useState(39);
  const [since,setSince]=useState(0),[running,setRunning]=useState(false);
  const [mismatchBase,setMismatchBase]=useState(0);
  const cancel=useRef(false),id=useRef((Date.now()%65000)+1);
  const latest=useRef({rxState:-1,txState:-1,effective:false});
  const truncated=useRef(false);
  const requests=useRef<(FastRequest & {hostTimestampMs:number})[]>([]);
  const capture=useRef<PacketEvent[]>([]),seen=useRef(new Set<string>());
  const send=async (request: Omit<FastRequest,"testId">)=>{
    id.current=id.current%65535+1;
    const result=await window.connectMonitorApi.fastRecovery({...request,testId:id.current});
    if(!result.ok)throw new Error(result.message??"设备未接受控制");
    requests.current.push({...request,testId:id.current,hostTimestampMs:Date.now()});
    if(requests.current.length>1000){requests.current.shift();truncated.current=true;}
    return id.current;
  };
  useEffect(()=>()=>{cancel.current=true;},[]);
  useEffect(()=>{
    if(!since)return;
    for(const p of packets){
      if(p.timestampMs<since || (!p.rfFastEvent && !p.rfFast))continue;
      const key=`${p.timestampMs}:${p.messageType}:${p.rfFastEvent?.role}:${p.rfFastEvent?.sequence}`;
      if(!seen.current.has(key)){seen.current.add(key);capture.current.push(p);}
    }
    if(capture.current.length>50000){capture.current.splice(0,capture.current.length-50000);
      truncated.current=true;
      seen.current=new Set(capture.current.map(p=>`${p.timestampMs}:${p.messageType}:${p.rfFastEvent?.role}:${p.rfFastEvent?.sequence}`));
      setMessage("记录达到上限，较早记录已丢弃；本轮不可作完整验收");}
  },[packets,since]);
  let rx: PacketEvent["rfFast"], tx: PacketEvent["rfFast"], mismatch: number | undefined;
  const now = Date.now();
  for (let i = packets.length - 1; i >= 0; i--) {
    const p = packets[i];
    if (!rx && p.rfFast?.role === 0 && now-p.timestampMs < 3000) rx = p.rfFast;
    if (!tx && p.rfFast?.role === 1 && now-p.timestampMs < 3000) tx = p.rfFast;
    if (mismatch === undefined) mismatch = p.rfChannel?.versionMismatches;
    if (rx && tx && mismatch !== undefined) break;
  }
  const mismatches = mismatch ?? 0;
  latest.current={rxState:rx?.state??-1,txState:tx?.state??-1,effective:!!rx?.effective && !!tx?.effective};
  const begin=async()=>{requests.current=[];await send({operation:4});capture.current=[];seen.current.clear();truncated.current=false;setMismatchBase(mismatches);setSince(Date.now());};
  const act=(fn:()=>Promise<void>)=>{void fn().catch(e=>setMessage(String(e)));};
  const stop=async()=>{cancel.current=true;await send({operation:3});setRunning(false);setMessage("已发送停止；当前事务按自身期限收敛");};
  const inject=async(delayUs=0)=>{await send({operation:2,target:target as 0|1|2,kind,channel:kind===4||kind===8?channel:255,count:kind===2||kind===4||kind===8?0:1,durationMs:100,delayUs});};
  const batch=async()=>{
    await begin();cancel.current=false;setRunning(true);
    try {for(let i=0;i<100 && !cancel.current;i++){
      if(!latest.current.effective)throw new Error("双方实验模式未生效或已降级，批量测试停止；重新启用后再测");
      setMessage(`相位测试 ${i+1}/100；逻辑故障模拟`);
      await inject((i*137)%10000);await new Promise(r=>setTimeout(r,1500));
    }}finally{setRunning(false);}
  };
  const exportLog=async()=>{
    const events=capture.current;
    const measures=[2,6,7,15,16].flatMap(event=>[0,1].map(role=>{
      const values=events.flatMap(p=>p.rfFastEvent?.event===event && p.rfFastEvent.role===role?[p.rfFastEvent.value]:[]);
      return {role,event,samples:values.length,p50:percentile(values,.5),p95:percentile(values,.95),max:percentile(values,1)};
    }));
    const summary={type:"fast_test_summary",since,records:events.length,truncated:truncated.current,requests:requests.current,
      measures,
      note:"按 role / testId 分组分析；提交时间不等于首个输入时间。固件时间，不含 Windows 处理；无外部时序验收。"};
    await window.connectMonitorApi.exportFastLog({content:[summary,...events].map(v=>JSON.stringify(v)).join("\n")+"\n"});
  };
  return <Box p={3} borderTopWidth="1px" borderColor="whiteAlpha.200" fontSize="11px">
    <Text fontWeight="bold">快速恢复实验 · 4K/8K · 默认关闭</Text>
    <label><input type="checkbox" checked={requested} onChange={e=>{
      const enabled=e.target.checked;act(async()=>{await send({operation:1,enabled});setRequested(enabled);setMessage("已请求，等待双方实际状态确认");});
    }}/> 请求启用实验模式</label>
    {[rx,tx].map((s,i)=><Text key={i}>{i?"TX":"RX"}: {s?fastStates[s.state]??"未知":"状态未到/已过期"} · {s?fastReasons[s.reason]??s.reason:"—"}</Text>)}
    <Text>双方实际启用：{rx?.effective && tx?.effective?"是（实验）":"否/待确认"}</Text>
    <Button size="xs" disabled={running} onClick={()=>act(async()=>{await send({operation:1,enabled:true});setRequested(true);setMessage("已重新请求，等待双方确认");})}>重新启用实验</Button>
    <Text>配置 {rx?.profile??"—"} · 支持 4K/8K · 已验收 {rx?.acceptedRates?"见配置":"无"}</Text>
    <Text>恢复成功 RX/TX {rx?.successes??"—"}/{tx?.successes??"—"} · 失败 {rx?.failures??"—"}/{tx?.failures??"—"}</Text>
    <Text>记录溢出 RX/TX {rx?.overflow??"—"}/{tx?.overflow??"—"}</Text>
    <Text>协议不匹配累计 {mismatches} · 本区间新增 {since?(mismatches>=mismatchBase?mismatches-mismatchBase:mismatches):"未测量"}</Text>
    <label>注入位置 <select value={target} onChange={e=>setTarget(Number(e.target.value))}><option value={1}>RX</option><option value={2}>TX</option><option value={0}>双方</option></select></label>
    <label> 故障 <select value={kind} onChange={e=>setKind(Number(e.target.value))}>
      {['ACK 丢失','DATA 丢失','确认丢失','频道屏蔽','完成事件丢失','启动失败','定时迟到','仅指定频道可用'].map((v,i)=><option key={v} value={i+1}>{v}</option>)}
    </select></label>
    <label> 频道 <select value={channel} onChange={e=>setChannel(Number(e.target.value))}>{[10,16,22,24,28,34,39].map(v=><option key={v}>{v}</option>)}{kind===4&&<option value={255}>全部屏蔽</option>}</select></label>
    <Text>DATA/频道持续 100ms，其余最多 1 次/100ms。不能证明真实干扰性能。</Text>
    <Button size="xs" disabled={running} onClick={()=>act(begin)}>新统计区间</Button>{" "}
    <Button size="xs" disabled={running} onClick={()=>act(async()=>{await begin();await inject();setMessage("已发送单次注入");})}>单次故障</Button>{" "}
    <Button size="xs" disabled={running} onClick={()=>act(batch)}>100 次相位测试</Button>{" "}
    <Button size="xs" onClick={()=>act(stop)}>停止</Button>{" "}
    <Button size="xs" disabled={running} onClick={()=>act(async()=>{
      await begin();const cfg=await window.connectMonitorApi.getDebugConfig();
      await window.connectMonitorApi.setDebugConfig({...cfg,autoHopEnabled:false,manualChannel:channel});
      setMessage(`已请求手动切至 ${channel}，测试结束后按需恢复自动模式`);
    })}>单次切频</Button>{" "}
    <Button size="xs" onClick={()=>act(exportLog)}>导出 JSONL</Button>
    <Text color="orange.200">{message}</Text>
  </Box>;
}
