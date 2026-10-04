// USB enumeration can survive leaving WebConfig mode. Probe the existing
// read-only clock command to check the controller session, without sampling RF.
export function createDeviceConnectionWatchdog(options: {
  enabled: () => boolean; generation: () => number; probe: () => Promise<unknown>;
  unavailable: (error: unknown) => void;
  schedule?: typeof setTimeout; cancel?: typeof clearTimeout;
}) {
  const schedule = options.schedule || setTimeout, cancel = options.cancel || clearTimeout;
  let stopped = false, timer: ReturnType<typeof setTimeout> | undefined;
  const tick = async () => {
    timer = undefined;
    if (stopped) return;
    const generation = options.generation();
    if (options.enabled()) {
      try { await options.probe(); }
      catch (error) {
        if (!stopped && options.enabled() && options.generation() === generation) options.unavailable(error);
      }
    }
    if (!stopped) timer = schedule(() => { void tick(); }, 3000);
  };
  timer = schedule(() => { void tick(); }, 3000);
  return () => { stopped = true; if (timer !== undefined) cancel(timer); };
}
