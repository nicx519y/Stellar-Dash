import * as React from "react";
import type { ConnectionMode, DeviceStatusEvent } from "../../../shared/monitor-types";
const KEY="xora.monitor.source";
function savedMode():ConnectionMode {
  try{return localStorage.getItem(KEY)==="USB"?"USB":"RF24G";}catch{return "RF24G";}
}
export function useMonitorSource(devices:DeviceStatusEvent[],automatic=true) {
  const [mode,setMode]=React.useState<ConnectionMode>(savedMode);
  const manualChoice=React.useRef(false);
  const usb=devices.some(d=>d.mode==="USB"&&d.state==="Connected");
  const rf=devices.some(d=>d.mode==="RF24G"&&d.state==="Connected");
  const commit=React.useCallback((next:ConnectionMode)=>{
    try{localStorage.setItem(KEY,next);}catch{}
    setMode(next);window.dispatchEvent(new Event(KEY));
    try{const channel=new BroadcastChannel(KEY);channel.postMessage(next);channel.close();}catch{}
  },[]);
  const choose=React.useCallback((next:ConnectionMode)=>{manualChoice.current=true;commit(next);},[commit]);
  React.useEffect(()=>{
    const read=()=>setMode(savedMode());
    let channel:BroadcastChannel|null=null;
    try{channel=new BroadcastChannel(KEY);channel.onmessage=e=>{if(e.data==="USB"||e.data==="RF24G")setMode(e.data);};}catch{}
    window.addEventListener("storage",read);window.addEventListener(KEY,read);
    return ()=>{channel?.close();window.removeEventListener("storage",read);window.removeEventListener(KEY,read);};
  },[]);
  React.useEffect(()=>{
    if(automatic && !manualChoice.current && usb!==rf) {
      const next=usb?"USB":"RF24G";
      if(mode!==next) {const timer=window.setTimeout(()=>commit(next),500);return ()=>window.clearTimeout(timer);}
    }
  },[usb,rf,mode,commit,automatic]);
  return [mode,choose] as const;
}
