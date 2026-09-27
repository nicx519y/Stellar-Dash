import { WebHidTransport } from './webhid-transport';

export interface BenchmarkStatus {
  runId: number; state: number; bytes: number; reports: number; crc32: number;
  errors: number; elapsedMs: number; spiHz: number; buildId: string;
}
export interface BenchmarkResult {
  runId: number; direction: 'upload' | 'download'; elapsedMs: number;
  bytes: number; crc32: number; bytesPerSecond: number; reportsPerSecond: number;
  rpcP95Ms: number | null; rpcSamplesMs: number[]; device: BenchmarkStatus;
  timing: ReturnType<WebHidTransport['getTimingSnapshot']>;
  browserVersion: string; webVersion: string; reportBytes: 1024; window: 8;
}
const DATA_BYTES = 988;
const table = new Uint32Array(256).map((_, i) => {
  let c = i;
  for (let bit = 0; bit < 8; bit++) c = (c >>> 1) ^ ((c & 1) ? 0xedb88320 : 0);
  return c >>> 0;
});
function crcUpdate(crc: number, data: Uint8Array): number {
  for (const value of data) crc = (crc >>> 8) ^ table[(crc ^ value) & 255];
  return crc >>> 0;
}
function nextRandom(seed: number): number { return (Math.imul(seed, 1664525) + 1013904223) >>> 0; }
const delay = (ms: number) => new Promise<void>(resolve => setTimeout(resolve, ms));

/** Uses the production authenticated codec and native WebHID writer. No Flash writes. */
export async function runWebHidBenchmark(
  transport: WebHidTransport,
  direction: 'upload' | 'download',
  durationMs: number,
  signal: AbortSignal,
  interleaveRpc = false,
): Promise<BenchmarkResult> {
  const runId = crypto.getRandomValues(new Uint32Array(1))[0] || 1;
  const seed = 0x584f5241;
  let random = seed, bytes = 0, crc = 0xffffffff, receiveError: Error | null = null;
  const rpcSamplesMs: number[] = [];
  const unsubscribe = transport.subscribe<Uint8Array>('benchmark.data', ({ data }) => {
    try {
      const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
      if (data.length <= 8 || view.getUint32(0, true) !== runId || view.getUint32(4, true) !== bytes)
        throw new Error('测速数据代次或顺序错误');
      for (const value of data.subarray(8)) {
        random = nextRandom(random);
        if (value !== random >>> 24) throw new Error('测速数据校验失败');
      }
      bytes += data.length - 8; crc = crcUpdate(crc, data.subarray(8));
    } catch (error) { receiveError = error as Error; }
  });
  let started = 0;
  let pendingRpc: Promise<void> | null = null;
  let timingStart = transport.getTimingSnapshot();
  try {
    await transport.request('benchmark.start', { runId, seed, direction: direction === 'upload' ? 0 : 1, durationMs }, { signal });
    started = performance.now();
    timingStart = transport.getTimingSnapshot();
    let nextRpc = started + 100;
    if (direction === 'upload') {
      while (performance.now() - started < durationMs) {
        signal.throwIfAborted();
        if (receiveError) throw receiveError;
        const payloads: Uint8Array[] = [];
        for (let i = 0; i < 4; i++) {
          const payload = new Uint8Array(DATA_BYTES + 8);
          const view = new DataView(payload.buffer);
          view.setUint32(0, runId, true); view.setUint32(4, bytes, true);
          for (let p = 8; p < payload.length; p++) { random = nextRandom(random); payload[p] = random >>> 24; }
          bytes += DATA_BYTES; crc = crcUpdate(crc, payload.subarray(8)); payloads.push(payload);
        }
        await transport.sendBenchmarkReports(payloads, signal);
        if (interleaveRpc && !pendingRpc && performance.now() >= nextRpc) {
          const before = performance.now();
          nextRpc = before + 100;
          pendingRpc = transport.request('benchmark.status', { runId }, { signal })
            .then(() => { rpcSamplesMs.push(performance.now() - before); })
            .catch(error => { receiveError = error as Error; })
            .finally(() => { pendingRpc = null; });
        }
      }
    } else {
      for (;;) {
        signal.throwIfAborted();
        if (receiveError) throw receiveError;
        await delay(100);
        const status = await transport.request<BenchmarkStatus>('benchmark.status', { runId }, { signal });
        if (status.data?.state === 2) break;
        if (performance.now() - started > durationMs + 10000) throw new Error('下载测速超时');
      }
    }
    await pendingRpc;
    if (receiveError) throw receiveError;
    const status = await transport.request<BenchmarkStatus>('benchmark.stop', { runId, bytes, crc32: (~crc) >>> 0 }, { signal });
    const elapsedMs = performance.now() - started;
    const device = status.data;
    if (receiveError) throw receiveError;
    if (!device || device.state !== 3 || device.bytes !== bytes || device.crc32 !== ((~crc) >>> 0) || device.errors)
      throw new Error('接收端未确认测速完成');
    const timing = transport.getTimingSnapshot();
    for (const key of Object.keys(timing) as (keyof typeof timing)[]) timing[key] -= timingStart[key];
    const sorted = [...rpcSamplesMs].sort((a, b) => a - b);
    return { runId, direction, elapsedMs, bytes, crc32: (~crc) >>> 0,
      bytesPerSecond: bytes * 1000 / elapsedMs, reportsPerSecond: device.reports * 1000 / elapsedMs,
      rpcP95Ms: sorted.length ? sorted[Math.ceil(sorted.length * .95) - 1] : null,
      rpcSamplesMs, timing, device, browserVersion: navigator.userAgent,
      webVersion: process.env.NEXT_PUBLIC_BUILD_VERSION ?? 'local-development', reportBytes: 1024, window: 8 };
  } catch (error) {
    await transport.request('benchmark.stop', { runId }, { timeoutMs: 2000 }).catch(() => undefined);
    throw error;
  } finally { unsubscribe(); }
}
