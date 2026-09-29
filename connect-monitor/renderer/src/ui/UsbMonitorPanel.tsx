import { ClearDataIconButton } from "./panelActions";
import { Box, Card } from "@chakra-ui/react";
import type { UsbStreamSnapshot } from "./monitorStreamTypes";
import { TelemetryTrendChart } from "./TelemetryTrendChart";
import { PanelHeader, panelSurfaceProps } from "./panelStyles";

export function UsbMonitorPanel({usb,now,clearAfter=0,onClearData}:{usb:UsbStreamSnapshot|undefined;now:number;clearAfter?:number;onClearData?:()=>void}) {
  const s=usb?.statistics;
  const fresh=!!s && now-s.timestampMs<=2000 && usb?.status?.state==="Connected" && usb.status.rateValid!==false;
  return <Card.Root variant="outline" h="100%" {...panelSurfaceProps}>
    <PanelHeader title="USB XInput · EP1 IN 完成率" meta={fresh&&s?.rateHz!==null?`${s?.rateHz.toFixed(1)} Hz`:"实测频率不可用"} action={onClearData?<ClearDataIconButton label="Clear USB chart data" onClick={onClearData}/>:undefined} borderBottom compact />
    <Card.Body p={3} minH={0} display="flex" flexDirection="column">
      <Box flex="1" minH={0}><TelemetryTrendChart showRfMetrics={false} rateSeries={(usb?.rates??[]).filter(p=>p.tMs>=clearAfter)} lossSeries={[]} channelSwitches={[]} height="100%" /></Box>
    </Card.Body>
  </Card.Root>;
}
