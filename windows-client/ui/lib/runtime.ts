export type RuntimeMode =
  | "native"
  | "acquiring"
  | "turbo"
  | "recovering"
  | "fault";

export interface RuntimeSnapshot {
  schemaVersion: 1;
  mode: RuntimeMode;
  highPerformanceEnabled: boolean;
  device: {
    connected: boolean;
    usbSpeed: "NONE" | "FULL" | "HIGH";
    firmwareVersion: string | null;
  };
  stream: {
    configuredHz: number;
    measuredHz: number;
    received: number;
    dropped: number;
    invalid: number;
    coalesced: number;
    lastSequence: number | null;
    intervalUs: { p50: number; p95: number; p99: number; max: number };
  };
  virtualPad: {
    backend: string;
    connected: boolean;
    slot: number | null;
  };
  input: {
    actionMask: number;
    buttons: number;
    lt: number;
    rt: number;
    lx: number;
    ly: number;
    rx: number;
    ry: number;
  };
  lastError: { code: string; message: string } | null;
}

export type RuntimeEvent = {
  event: "runtime.snapshot" | "runtime.stateChanged";
  payload: RuntimeSnapshot;
};

export type WebViewRuntimeMessage = {
  readonly data: RuntimeEvent;
};

declare global {
  interface Window {
    chrome?: {
      webview?: {
        postMessage(message: string): void;
        addEventListener(
          name: "message",
          callback: (event: WebViewRuntimeMessage) => void,
        ): void;
        removeEventListener(
          name: "message",
          callback: (event: WebViewRuntimeMessage) => void,
        ): void;
      };
    };
  }
}

export const emptySnapshot: RuntimeSnapshot = {
  schemaVersion: 1,
  mode: "native",
  highPerformanceEnabled: true,
  device: { connected: false, usbSpeed: "NONE", firmwareVersion: null },
  stream: {
    configuredHz: 1000,
    measuredHz: 0,
    received: 0,
    dropped: 0,
    invalid: 0,
    coalesced: 0,
    lastSequence: null,
    intervalUs: { p50: 0, p95: 0, p99: 0, max: 0 },
  },
  virtualPad: { backend: "vigem-internal", connected: false, slot: null },
  input: {
    actionMask: 0,
    buttons: 0,
    lt: 0,
    rt: 0,
    lx: 0,
    ly: 0,
    rx: 0,
    ry: 0,
  },
  lastError: null,
};

export function sendRuntimeCommand(command: Record<string, unknown>) {
  window.chrome?.webview?.postMessage(JSON.stringify(command));
}
