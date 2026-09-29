/// <reference lib="webworker" />

import {
  parsePerformanceEdge,
  parsePerformanceSample,
} from './performance-codec';

type WorkerInput =
  | { kind: 'sample'; payload: ArrayBuffer }
  | { kind: 'edge'; payload: ArrayBuffer };

self.onmessage = (event: MessageEvent<WorkerInput>) => {
  try {
    if (event.data.kind === 'sample') {
      self.postMessage({
        kind: 'sample',
        value: parsePerformanceSample(event.data.payload),
      });
    } else {
      self.postMessage({
        kind: 'edge',
        value: parsePerformanceEdge(event.data.payload),
      });
    }
  } catch (error) {
    self.postMessage({
      kind: 'error',
      message: error instanceof Error ? error.message : String(error),
    });
  }
};
