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
  const snapshotPending = React.useRef<Worker | null>(null);
  pausedRef.current = paused;

  React.useEffect(() => {
    // Return credit after React committed this update, not before rendering it.
    if (snapshotPending.current === workerRef.current && snapshotPending.current) {
      postWorkerMessage(snapshotPending.current, { type: "snapshotConsumed" });
      snapshotPending.current = null;
    }
  }, [snapshot]);

  React.useEffect(() => {
    const worker = createMonitorWorker();
    workerRef.current = worker;
    let unsub: (() => void) | null = null;
    let unsubCleared: (() => void) | null = null;
    let nextRequestId = 0;
    let active = true;
    const pending = new Map<number, () => void>();

    worker.onmessage = (event: MessageEvent<MonitorStreamWorkerResponse>) => {
      if (event.data.type === "snapshot") {
        const patch = event.data.snapshot;
        snapshotPending.current = worker;
        setSnapshot(previous => ({ ...previous, ...patch }));
      } else if (event.data.type === "processed") {
        pending.get(event.data.requestId)?.();
        pending.delete(event.data.requestId);
      }
    };

    const handler = (batch: MonitorEvent[]) => {
      if (!active || pausedRef.current || batch.length === 0) return;
      const requestId = ++nextRequestId;
      return new Promise<void>((resolve) => {
        pending.set(requestId, resolve);
        postWorkerMessage(worker, { type: "batch", events: batch, requestId });
      });
    };

    if (window.connectMonitorApi?.onEvents) {
      unsub = window.connectMonitorApi.onEvents(handler);
      window.connectMonitorApi.getSnapshot(200).then((snap) => handler(snap)).catch(() => {});
    }
    if (window.connectMonitorApi?.onMonitorCleared) {
      unsubCleared = window.connectMonitorApi.onMonitorCleared(() => {
        setSnapshot(createEmptyMonitorStreamSnapshot());
        postWorkerMessage(worker, { type: "reset" });
      });
    }

    return () => {
      active = false;
      if (unsub) unsub();
      if (unsubCleared) unsubCleared();
      worker.terminate();
      for (const resolve of pending.values()) resolve();
      pending.clear();
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
