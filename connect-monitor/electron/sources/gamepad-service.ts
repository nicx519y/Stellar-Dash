import { spawn, type ChildProcessWithoutNullStreams } from "node:child_process";
import type { GamepadDevice } from "../../shared/device-binding";
import { DeviceBindingRegistry } from "./device-bindings";

/** One OS input owner. The main process only reads this cache; never polls native input. */
export class GamepadService {
  readonly registry = new DeviceBindingRegistry();
  readonly readings = new Map<string, { standardMask: number; timestampMs: number; generation: number }>();
  private child: ChildProcessWithoutNullStreams | null = null;
  private revision = 0;
  private selection = "";
  private lastFrame = 0;
  private watchdog: NodeJS.Timeout | null = null;
  private stopped = false;
  constructor(private changed: () => void) {}

  start(executable: string) {
    if (this.child || this.stopped) return;
    if (process.platform !== "win32") { this.registry.backendReason = "当前系统不支持 Windows 设备身份读取"; this.changed(); return; }
    const child = spawn(executable, [], { windowsHide: true, stdio: ["pipe", "pipe", "pipe"] });
    this.child = child;
    let buffer = "";
    child.stdout.setEncoding("utf8");
    child.stderr.on("data", () => {});
    child.stdin.on("error", () => {});
    child.stdout.on("data", (chunk: string) => {
      buffer += chunk;
      if (buffer.length > 1024 * 1024) { child.kill(); return; }
      for (let end; (end = buffer.indexOf("\n")) >= 0;) {
        const line = buffer.slice(0, end); buffer = buffer.slice(end + 1);
        try {
          const frame = JSON.parse(line);
          if (!Array.isArray(frame.devices) || !Array.isArray(frame.readings)) continue;
          const devices = frame.devices.filter((d: GamepadDevice) => typeof d.id === "string" && typeof d.name === "string" &&
            Number.isInteger(d.vendorId) && Number.isInteger(d.productId) && Number.isInteger(d.generation) &&
            (d.backend === "wgi" || d.backend === "xinput")) as GamepadDevice[];
          this.lastFrame = Date.now();
          const previousReason=this.registry.backendReason;
          this.registry.backendReason = frame.wgiAvailable ? "未发现可读取的手柄" : "Windows 身份读取不可用，请手动选择 XInput";
          const previous = JSON.stringify(this.registry.gamepads);
          this.registry.updateGamepads(devices);
          // The selection revision also fences data queued before a source/rebind change.
          if (frame.revision === this.revision) {
            this.readings.clear();
            for (const r of frame.readings) if (typeof r.id === "string" && Number.isInteger(r.standardMask) && Number.isInteger(r.generation))
              this.readings.set(r.id, { standardMask: r.standardMask, generation: r.generation, timestampMs: this.lastFrame });
          }
          if (previous !== JSON.stringify(this.registry.gamepads) || previousReason!==this.registry.backendReason) this.changed();
        } catch { /* No state from partial or malformed frames. */ }
      }
    });
    const failed = (reason: string) => {
      this.registry.backendReason = reason;
      this.registry.updateGamepads([]); this.readings.clear(); this.changed();
    };
    child.on("error", e => failed(`手柄辅助程序不可用：${e.message}`));
    child.on("exit", () => { this.child = null; failed("手柄辅助程序已退出，请重启监视器"); });
    this.watchdog = setInterval(() => {
      if (this.lastFrame && Date.now() - this.lastFrame > 1000 && this.registry.gamepads.length)
        failed("手柄数据已过期");
    }, 250);
    this.select(true);
  }

  select(force = false) {
    const bindings = this.registry.snapshot().bindings;
    const signature = JSON.stringify([this.registry.activeSource, bindings.USB.generation, bindings.RF24G.generation]);
    if (!force && signature === this.selection) return;
    this.selection = signature; this.revision++; this.readings.clear();
    const ids = [...new Set([bindings.USB.gamepadId, bindings.RF24G.gamepadId].filter((id): id is string => !!id))];
    if (this.child?.stdin.writable) this.child.stdin.write(JSON.stringify({ ids, revision: this.revision }) + "\n");
  }

  stop() {
    this.stopped = true;
    if (this.watchdog) clearInterval(this.watchdog);
    const child = this.child;
    if (child) {
      child.stdin.end();
      const timer = setTimeout(() => child.kill(), 1500);
      timer.unref(); child.once("exit", () => clearTimeout(timer));
    }
    this.readings.clear();
  }
}
