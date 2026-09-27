import { useEffect, useLayoutEffect, useState } from "react";
import type { DeviceBindings } from "../../../shared/device-binding";
import type { ConnectionMode } from "../../../shared/monitor-types";

export function useDeviceBindings(sourceMode:ConnectionMode,controlSource=true) {
  const [state,setState]=useState<DeviceBindings|null>(null);
  const [error,setError]=useState("");
  useLayoutEffect(()=>{
    if(controlSource)window.connectMonitorApi?.setDeviceSource?.(sourceMode).catch(e=>setError(String(e)));
  },[sourceMode,controlSource]);
  useEffect(()=>{
    let active=true,received=false;
    const stop=window.connectMonitorApi?.onDeviceBindings?.(value=>{received=true;setState(value);});
    window.connectMonitorApi?.getDeviceBindings?.().then(value=>{if(active && !received)setState(value);}).catch(e=>{if(active)setError(String(e));});
    return ()=>{active=false;stop?.();};
  },[]);
  return {state,error};
}
