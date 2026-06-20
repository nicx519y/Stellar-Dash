import * as React from "react";

import type { MonitorEvent } from "../../../shared/monitor-types";
import {
  createEmptyMonitorStreamSnapshot,
  type MonitorStreamSnapshot,
  type MonitorStreamWorkerRequest,
  type MonitorStreamWorkerResponse,
} from "./monitorStreamTypes";

export type {
  ChannelScoreRow,
  ChannelSwitchRow,
  ErrorRow,
  LossPoint,
  MonitorStreamSnapshot,
  PacketRow,
  RatePoint,
} from "./monitorStreamTypes";

function createMonitorWorker(): Worker {
  return new Worker(new URL("./monitorStream.worker.ts", import.meta.url), {
    type: "module",
    name: "monitor-stream",
  });
}

function postWorkerMessage(worker: Worker | null, message: MonitorStreamWorkerRequest): void {
  worker?.postMessage(message);
}

export function useMonitorStream() {
  const [snapshot, setSnapshot] = React.useState<MonitorStreamSnapshot>(createEmptyMonitorStreamSnapshot);
  const [paused, setPausedState] = React.useState(false);
  const pausedRef = React.useRef(false);
  const workerRef = React.useRef<Worker | null>(null);
  pausedRef.current = paused;

  React.useEffect(() => {
    const worker = createMonitorWorker();
    workerRef.current = worker;
    let unsub: (() => void) | null = null;

    worker.onmessage = (event: MessageEvent<MonitorStreamWorkerResponse>) => {
      if (event.data.type === "snapshot") {
        setSnapshot(event.data.snapshot);
      }
    };

    const handler = (batch: MonitorEvent[]) => {
      if (pausedRef.current || batch.length === 0) return;
      postWorkerMessage(worker, { type: "batch", events: batch });
    };

    if (window.connectMonitorApi?.onEvents) {
      unsub = window.connectMonitorApi.onEvents(handler);
      window.connectMonitorApi.getSnapshot(200).then((snap) => handler(snap)).catch(() => {});
    }

    return () => {
      if (unsub) unsub();
      worker.terminate();
      if (workerRef.current === worker) {
        workerRef.current = null;
      }
    };
  }, []);

  const clear = React.useCallback(() => {
    setSnapshot(createEmptyMonitorStreamSnapshot());
    postWorkerMessage(workerRef.current, { type: "reset" });
    if (window.connectMonitorApi?.clear) {
      window.connectMonitorApi.clear().catch(() => {});
    }
  }, []);

  const setPaused = React.useCallback(async (nextPaused: boolean) => {
    pausedRef.current = nextPaused;
    setPausedState(nextPaused);
    if (window.connectMonitorApi?.setPaused) {
      try {
        await window.connectMonitorApi.setPaused(nextPaused);
      } catch {
      }
    }
  }, []);

  const loadOlderEvents = React.useCallback(async () => {
    if (!window.connectMonitorApi?.queryEvents || snapshot.events.length === 0) return;
    const before = snapshot.events[0].timestampMs;
    const older = await window.connectMonitorApi.queryEvents(before, 500);
    if (older.length === 0) return;
    postWorkerMessage(workerRef.current, { type: "prependEvents", events: older });
  }, [snapshot.events]);

  React.useEffect(() => {
    if (window.connectMonitorApi?.getPaused) {
      window.connectMonitorApi
        .getPaused()
        .then((p) => setPausedState(Boolean(p)))
        .catch(() => {});
    }
  }, []);

  return {
    ...snapshot,
    paused,
    setPaused,
    clear,
    loadOlderEvents,
  };
}
