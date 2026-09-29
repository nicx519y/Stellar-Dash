import { Badge, Box, Card, Text } from "@chakra-ui/react";
import { useCallback, useEffect, useLayoutEffect, useRef, useState } from "react";

import type { ConnectionMode, HitboxBounds, HitboxSummary } from "../../../shared/monitor-types";
import type { DeviceBindings } from "../../../shared/device-binding";
import { PanelHeader, panelSurfaceProps } from "./panelStyles";

const disconnectedSummary: HitboxSummary = {
  connected: false,
  deviceId: null,
  pressedCount: 0,
  timestampMs: 0,
};

function hiddenBounds(compact: boolean): HitboxBounds {
  return {
    x: 0,
    y: 0,
    width: 1,
    height: 1,
    visible: false,
    compact,
  };
}

function HitboxViewSlot({ compact }: { compact: boolean }) {
  const slotRef = useRef<HTMLDivElement | null>(null);
  const frameRef = useRef<number | null>(null);

  const syncBounds = useCallback(() => {
    if (frameRef.current !== null) return;
    frameRef.current = window.requestAnimationFrame(() => {
      frameRef.current = null;
      const slot = slotRef.current;
      if (!slot) {
        window.connectMonitorApi?.setHitboxBounds?.(hiddenBounds(compact));
        return;
      }

      const rect = slot.getBoundingClientRect();
      const visible =
        rect.width >= 2 &&
        rect.height >= 2 &&
        rect.right > 0 &&
        rect.bottom > 0 &&
        rect.left < window.innerWidth &&
        rect.top < window.innerHeight;

      window.connectMonitorApi?.setHitboxBounds?.({
        x: rect.left,
        y: rect.top,
        width: rect.width,
        height: rect.height,
        visible,
        compact,
      });
    });
  }, [compact]);

  useLayoutEffect(() => {
    syncBounds();

    const resizeObserver = new ResizeObserver(syncBounds);
    if (slotRef.current) {
      resizeObserver.observe(slotRef.current);
    }
    resizeObserver.observe(document.body);
    window.addEventListener("resize", syncBounds);
    window.addEventListener("scroll", syncBounds, true);

    return () => {
      resizeObserver.disconnect();
      window.removeEventListener("resize", syncBounds);
      window.removeEventListener("scroll", syncBounds, true);
      if (frameRef.current !== null) {
        window.cancelAnimationFrame(frameRef.current);
        frameRef.current = null;
      }
      window.connectMonitorApi?.setHitboxBounds?.(hiddenBounds(compact));
    };
  }, [compact, syncBounds]);

  return (
    <Box
      ref={slotRef}
      w="100%"
      h="100%"
      minH={0}
      flex="1"
      position="relative"
      overflow="hidden"
      borderRadius="6px"
    />
  );
}

export function ButtonsPanel({ compact = false, sourceMode, devices }: { compact?: boolean;sourceMode:ConnectionMode;devices:DeviceBindings|null }) {
  const [summary, setSummary] = useState<HitboxSummary>(disconnectedSummary);
  const [selecting,setSelecting]=useState(false);
  const [pad,setPad]=useState("");
  const [peer,setPeer]=useState("");
  const [error,setError]=useState("");
  const [saving,setSaving]=useState(false);
  const binding=devices?.bindings[sourceMode];
  const current=summary.sourceMode===sourceMode && summary.bindingGeneration===binding?.generation?summary:disconnectedSummary;
  const sourceLabel = `${sourceMode==="USB"?"USB · 有线 TX":"RF · RX"} · ${current.connected?"已连接":"未连接"}`;
  const meta = `${current.pressedCount} pressed`;
  useEffect(()=>{setSelecting(false);setError("");},[sourceMode]);
  const openSelection=()=>{setPad(binding?.gamepadId??"");setPeer(binding?.telemetryId??"");setError("");setSelecting(v=>!v);};
  const save=async(automatic=false)=>{
    setSaving(true);setError("");
    try {await window.connectMonitorApi.selectDevices(sourceMode,automatic?null:{gamepadId:pad||null,telemetryId:peer||null});setSelecting(false);}
    catch(e){setError(String(e));}finally{setSaving(false);}
  };
  const candidates=devices?.gamepads.filter(d=>!d.sourceMode||d.sourceMode===sourceMode)??[];
  const selectStyle={background:"#101f28",color:"#e6f3f4",border:"1px solid #456",borderRadius:4,padding:4,width:"100%"};
  const hex=(n:number)=>n.toString(16).padStart(4,"0").toUpperCase();

  useEffect(() => {
    return window.connectMonitorApi?.onHitboxSummary?.((nextSummary) => {
      setSummary(nextSummary);
    });
  }, []);

  return (
    <Card.Root variant="outline" overflow="hidden" h="100%" {...panelSurfaceProps}>
      <PanelHeader
        title="Gamepad Buttons"
        meta={meta}
        action={
          <Badge as="button" cursor="pointer" onClick={openSelection} title="选择设备" colorPalette={current.connected ? "green" : "gray"}>
            {sourceLabel}
          </Badge>
        }
        borderBottom
        compact={compact}
      />
      <Card.Body
        px={compact ? 3 : { base: 3, md: 5 }}
        py={compact ? 3 : { base: 4, md: 5 }}
        display="flex"
        flexDirection="column"
        flex="1"
        minH={0}
      >
        {selecting?<Box display="flex" flexDirection="column" gap={2} fontSize="sm">
          <label>游戏手柄<select aria-label="游戏手柄" style={selectStyle} value={pad} onChange={e=>setPad(e.target.value)}>
            <option value="">未绑定</option>
            {candidates.map(d=><option key={d.id} value={d.id}>{d.name} · {d.backend==="xinput"?"临时槽位":`${hex(d.vendorId)}:${hex(d.productId)}`} · {d.id}</option>)}
          </select></label>
          <label>遥测设备<select aria-label="遥测设备" style={selectStyle} value={peer} onChange={e=>setPeer(e.target.value)}>
            <option value="">不绑定遥测</option>
            {devices?.telemetry.filter(d=>d.sourceMode===sourceMode).map(d=><option key={d.id} value={d.id}>{hex(d.vendorId)}:{hex(d.productId)} · {d.serialNumber||d.path} {d.capable?"":"（能力不可用）"}</option>)}
          </select></label>
          <Text color="gray.400">XInput 临时槽位需手动核对，断开后重新选择。</Text>
          <Box display="flex" gap={4}><button disabled={saving} onClick={()=>void save()}>保存绑定</button><button disabled={saving} onClick={()=>void save(true)}>自动识别</button><button onClick={()=>setSelecting(false)}>取消</button></Box>
          {error&&<Text color="red.300">{error}</Text>}
        </Box>:<>
          {(binding?.reason||current.reason)&&<Text fontSize="xs" color="gray.400">{binding?.reason||current.reason}</Text>}
          <HitboxViewSlot compact={compact} />
        </>}
      </Card.Body>
    </Card.Root>
  );
}
