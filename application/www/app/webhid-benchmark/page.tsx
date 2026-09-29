'use client';
import { useEffect, useRef, useState } from 'react';
import { WebHidTransport } from '@/lib/device-transport/webhid-transport';
import { DirectDeviceSessionClient } from '@/lib/device-transport/device-session-client';
import { runWebHidBenchmark, type BenchmarkResult } from '@/lib/device-transport/webhid-benchmark';

export default function WebHidBenchmarkPage() {
  const transport = useRef<WebHidTransport | null>(null);
  const cancel = useRef<AbortController | null>(null);
  const [state, setState] = useState('未连接');
  const [busy, setBusy] = useState(false);
  const [results, setResults] = useState<BenchmarkResult[]>([]);
  const [local, setLocal] = useState(false);
  useEffect(() => {
    setLocal(['localhost', '127.0.0.1', '[::1]'].includes(location.hostname));
    return () => { cancel.current?.abort(); void transport.current?.close(); };
  }, []);
  async function connect() {
    setBusy(true);
    try {
      const next = new WebHidTransport();
      transport.current = next;
      await next.requestPermissionAndConnect();
      await new DirectDeviceSessionClient().authenticate(next);
      setState('已连接 XORA 高速加密通道');
    } catch (error) { setState(String(error)); await transport.current?.close(); transport.current = null; }
    finally { setBusy(false); }
  }
  async function run(direction: 'upload' | 'download', rpc = false, soak = false) {
    if (!transport.current) return;
    setBusy(true); cancel.current = new AbortController();
    try {
      setState('预热 2 秒');
      await runWebHidBenchmark(transport.current, direction, 2000, cancel.current.signal, rpc);
      for (let round = 1; round <= (soak ? 1 : 3); round++) {
        setState(soak ? '连续稳定性测试：10 分钟' : `测量第 ${round}/3 轮：10 秒`);
        const result = await runWebHidBenchmark(transport.current, direction, soak ? 600000 : 10000, cancel.current.signal, rpc);
        setResults(previous => [...previous, result]);
      }
      setState('测试完成，接收端已确认字节数与 CRC');
    } catch (error) { setState(String(error)); }
    finally {
      if (transport.current && !transport.current.session?.authenticated) { await transport.current.close(); transport.current = null; }
      setBusy(false); cancel.current = null;
    }
  }
  function download() {
    const url = URL.createObjectURL(new Blob([JSON.stringify(results, null, 2)], { type: 'application/json' }));
    const link = document.createElement('a'); link.href = url; link.download = 'xora-webhid-benchmark.json'; link.click(); URL.revokeObjectURL(url);
  }
  if (!local) return <main><h1>XORA 带宽诊断</h1><p>此开发诊断页仅允许本机访问。</p></main>;
  return <main style={{ padding: 32, maxWidth: 1100, margin: 'auto' }}>
    <h1>XORA WebHID 带宽诊断</h1>
    <p>RAM 通信测试，1024 字节报告、窗口 8。请关闭其他设备配置页面后连接。</p>
    <p role="status">{state}</p>
    <div style={{ display: 'flex', gap: 16, flexWrap: 'wrap', marginBlock: 24 }}>
      <button disabled={busy || !!transport.current} onClick={connect}>连接设备</button>
      <button disabled={busy || !transport.current} onClick={() => run('upload')}>上传三轮</button>
      <button disabled={busy || !transport.current} onClick={() => run('download')}>下载三轮</button>
      <button disabled={busy || !transport.current} onClick={() => run('upload', true)}>满载 RPC 三轮</button>
      <button disabled={busy || !transport.current} onClick={() => run('upload', true, true)}>稳定性 10 分钟</button>
      <button disabled={!busy} onClick={() => cancel.current?.abort()}>取消</button>
      <button disabled={!results.length} onClick={download}>导出 JSON</button>
    </div>
    <table style={{ width: '100%', textAlign: 'left' }}><thead><tr><th>Run ID</th><th>方向</th><th>有效 MB/s</th><th>报告/s</th><th>RPC P95</th><th>校验</th></tr></thead>
      <tbody>{results.map(r => <tr key={r.runId}><td>{r.runId}</td><td>{r.direction}</td><td>{(r.bytesPerSecond / 1e6).toFixed(3)}</td><td>{r.reportsPerSecond.toFixed(0)}</td><td>{r.rpcP95Ms?.toFixed(1) ?? '—'} ms</td><td>已确认</td></tr>)}</tbody></table>
  </main>;
}
