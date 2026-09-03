import { Box, Button, Flex, Grid, Heading, Text } from "@chakra-ui/react";
import Head from "next/head";
import { useCallback, useEffect, useMemo, useState } from "react";
import {
  emptySnapshot,
  RuntimeSnapshot,
  sendRuntimeCommand,
  WebViewRuntimeMessage,
} from "../lib/runtime";

const buttonBits = [
  ["↑", 0x0001], ["↓", 0x0002], ["←", 0x0004], ["→", 0x0008],
  ["START", 0x0010], ["BACK", 0x0020], ["LS", 0x0040], ["RS", 0x0080],
  ["LB", 0x0100], ["RB", 0x0200], ["A", 0x1000], ["B", 0x2000],
  ["X", 0x4000], ["Y", 0x8000],
] as const;

const modeLabels: Record<RuntimeSnapshot["mode"], string> = {
  native: "原生 XInput",
  acquiring: "正在接管",
  turbo: "高性能模式",
  recovering: "正在恢复",
  fault: "需要处理",
};

function Metric({ label, value, suffix = "" }: {
  label: string; value: string | number; suffix?: string;
}) {
  return (
    <Box className="metric">
      <Text className="eyebrow">{label}</Text>
      <Text className="metricValue">{value}<small>{suffix}</small></Text>
    </Box>
  );
}

function Card({ title, children, className = "" }: {
  title: string; children: React.ReactNode; className?: string;
}) {
  return (
    <Box className={`card ${className}`}>
      <Text className="cardTitle">{title}</Text>
      {children}
    </Box>
  );
}

function Trigger({ label, value }: { label: string; value: number }) {
  return (
    <Box className="trigger">
      <Flex justify="space-between"><Text>{label}</Text><Text>{value}</Text></Flex>
      <Box className="track"><Box className="fill" style={{ width: `${value / 2.55}%` }} /></Box>
    </Box>
  );
}

function Stick({ label, x, y }: { label: string; x: number; y: number }) {
  const left = 50 + (x / 32768) * 40;
  const top = 50 - (y / 32768) * 40;
  return (
    <Box>
      <Text className="stickLabel">{label} <span>{x}, {y}</span></Text>
      <Box className="stick"><Box className="stickDot" style={{ left: `${left}%`, top: `${top}%` }} /></Box>
    </Box>
  );
}

export default function Dashboard() {
  const [snapshot, setSnapshot] = useState(emptySnapshot);
  const [bridgeReady, setBridgeReady] = useState(false);

  useEffect(() => {
    const bridge = window.chrome?.webview;
    if (!bridge) return;
    setBridgeReady(true);
    const receive = (event: WebViewRuntimeMessage) => {
      const message = event.data;
      if (!message || message.payload?.schemaVersion !== 1) return;
      if (message.event === "runtime.snapshot" ||
          message.event === "runtime.stateChanged") {
        setSnapshot(message.payload);
      }
    };
    bridge.addEventListener("message", receive);
    sendRuntimeCommand({ command: "runtime.getSnapshot" });
    return () => bridge.removeEventListener("message", receive);
  }, []);

  const setHighPerformance = useCallback((enabled: boolean) => {
    setSnapshot((current) => ({ ...current, highPerformanceEnabled: enabled }));
    sendRuntimeCommand({
      command: "runtime.setHighPerformanceEnabled",
      enabled,
    });
  }, []);

  const activeButtons = useMemo(() => new Set(
    buttonBits.filter(([, bit]) => (snapshot.input.buttons & bit) !== 0).map(([name]) => name),
  ), [snapshot.input.buttons]);
  const lossRate = snapshot.stream.received === 0 ? 0 :
    (snapshot.stream.dropped / (snapshot.stream.received + snapshot.stream.dropped)) * 100;

  return (
    <>
      <Head>
        <title>HBox Input Engine</title>
        <link rel="icon" href="/favicon.svg" type="image/svg+xml" />
      </Head>
    <Box className="shell">
      <Flex className="topbar" align="center" justify="space-between">
        <Flex align="center" gap="14px">
          <Box className="logo">H</Box>
          <Box>
            <Heading className="title">HBox Input Engine</Heading>
            <Text className="subtitle">Windows high-rate XInput bridge</Text>
          </Box>
        </Flex>
        <Flex gap="10px">
          <Button className="ghostButton" onClick={() => sendRuntimeCommand({ command: "window.hide" })}>隐藏</Button>
          <Button className="dangerButton" onClick={() => sendRuntimeCommand({ command: "app.exit" })}>退出</Button>
        </Flex>
      </Flex>

      <Grid className="hero" templateColumns={{ base: "1fr", lg: "1.55fr 1fr" }} gap="18px">
        <Box className="statusPanel">
          <Flex align="center" gap="12px">
            <Box className={`statusDot ${snapshot.mode}`} />
            <Box>
              <Text className="eyebrow">运行状态</Text>
              <Heading className="statusTitle">{modeLabels[snapshot.mode]}</Heading>
            </Box>
          </Flex>
          <Text className="statusCopy">
            {snapshot.mode === "turbo"
              ? "物理 XInput 已隐藏，输入正由虚拟 Xbox 360 控制器提供。"
              : snapshot.device.connected
                ? "设备处于兼容模式；满足 2K/4K/8K 条件后客户端将自动接管。"
                : "等待 HBox USB 设备连接。"}
          </Text>
          {snapshot.lastError && (
            <Box className="errorBox"><b>{snapshot.lastError.code}</b> · {snapshot.lastError.message}</Box>
          )}
        </Box>

        <Box className="switchPanel">
          <Flex justify="space-between" align="center" gap="18px">
            <Box>
              <Text className="eyebrow">CLIENT TAKEOVER</Text>
              <Text className="switchTitle">高性能模式</Text>
              <Text className="switchCopy">仅在设备配置高于 1K 时接管</Text>
            </Box>
            <label className="switch">
              <input
                aria-label="高性能模式"
                type="checkbox"
                checked={snapshot.highPerformanceEnabled}
                onChange={(event) => setHighPerformance(event.target.checked)}
              />
              <span />
            </label>
          </Flex>
          {!bridgeReady && <Text className="previewNote">浏览器预览：未连接原生运行时</Text>}
        </Box>
      </Grid>

      <Grid className="metricGrid" templateColumns={{ base: "repeat(2, 1fr)", lg: "repeat(4, 1fr)" }} gap="12px">
        <Metric label="配置频率" value={(snapshot.stream.configuredHz / 1000).toFixed(0)} suffix="K Hz" />
        <Metric label="实测接收" value={snapshot.stream.measuredHz.toFixed(0)} suffix=" Hz" />
        <Metric label="丢包率" value={lossRate.toFixed(3)} suffix="%" />
        <Metric label="虚拟槽位" value={snapshot.virtualPad.slot === null ? "—" : snapshot.virtualPad.slot} />
      </Grid>

      <Grid className="contentGrid" templateColumns={{ base: "1fr", xl: "1.2fr 1fr" }} gap="18px">
        <Card title="输入状态" className="inputCard">
          <Box className="buttonGrid">
            {buttonBits.map(([name]) => <Box key={name} className={`padButton ${activeButtons.has(name) ? "active" : ""}`}>{name}</Box>)}
          </Box>
          <Grid templateColumns="1fr 1fr" gap="20px" mt="22px">
            <Trigger label="LT" value={snapshot.input.lt} />
            <Trigger label="RT" value={snapshot.input.rt} />
          </Grid>
          <Grid templateColumns="1fr 1fr" gap="24px" mt="24px">
            <Stick label="LEFT STICK" x={snapshot.input.lx} y={snapshot.input.ly} />
            <Stick label="RIGHT STICK" x={snapshot.input.rx} y={snapshot.input.ry} />
          </Grid>
        </Card>

        <Box>
          <Card title="USB 与虚拟设备">
            <Box className="detailRows">
              <Flex><span>物理设备</span><b>{snapshot.device.connected ? "已连接" : "未连接"}</b></Flex>
              <Flex><span>USB 速率</span><b>{snapshot.device.usbSpeed}</b></Flex>
              <Flex><span>虚拟后端</span><b>{snapshot.virtualPad.backend}</b></Flex>
              <Flex><span>虚拟 Xbox 360</span><b>{snapshot.virtualPad.connected ? "在线" : "离线"}</b></Flex>
              <Flex><span>流序号</span><b>{snapshot.stream.lastSequence ?? "—"}</b></Flex>
            </Box>
          </Card>
          <Card title="完成间隔" className="intervalCard">
            <Grid templateColumns="repeat(4, 1fr)" gap="8px">
              {(["p50", "p95", "p99", "max"] as const).map((key) => (
                <Box className="interval" key={key}><span>{key.toUpperCase()}</span><b>{snapshot.stream.intervalUs[key].toFixed(1)}</b><small>µs</small></Box>
              ))}
            </Grid>
          </Card>
          <Card title="流量计数">
            <Grid templateColumns="repeat(4, 1fr)" gap="8px">
              <Metric label="接收" value={snapshot.stream.received} />
              <Metric label="丢失" value={snapshot.stream.dropped} />
              <Metric label="非法" value={snapshot.stream.invalid} />
              <Metric label="合并" value={snapshot.stream.coalesced} />
            </Grid>
          </Card>
        </Box>
      </Grid>
      <Text className="footnote">8K 表示设备到客户端的更新频率；游戏读取频率由游戏自身决定。</Text>
    </Box>
    </>
  );
}
