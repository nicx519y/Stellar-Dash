import type { BindingChoice, DeviceBindings, GamepadDevice, SourceBinding, TelemetryDevice } from "../../shared/device-binding";
import type { ConnectionMode, NativeGamepadSnapshot } from "../../shared/monitor-types";
import { deviceRole, isWebConfigInterface } from "./hid-device-selection";

const modes: ConnectionMode[] = ["USB", "RF24G"];
export class DeviceBindingRegistry {
  activeSource: ConnectionMode = "RF24G";
  gamepads: GamepadDevice[] = [];
  telemetry: TelemetryDevice[] = [];
  choices: Partial<Record<ConnectionMode, BindingChoice>> = {};
  private serial = 0;
  private signatures = new Map<ConnectionMode, string>();
  private resolved = new Map<ConnectionMode, SourceBinding>();
  backendReason = "正在识别手柄";

  updateGamepads(devices: GamepadDevice[]) {
    const previous=this.gamepads;
    this.gamepads = devices.filter(d => !isWebConfigInterface(d)).map(d=>({...d,sourceMode:deviceRole(d)}));
    // Slot assignments have no persistent identity. Drop them on disconnection.
    for (const mode of modes) {
      const choice = this.choices[mode];
      if (choice?.gamepadId?.startsWith("xinput:") && (!this.gamepads.some(d => d.id === choice.gamepadId) ||
          this.gamepads.find(d=>d.id===choice.gamepadId)?.generation!==previous.find(d=>d.id===choice.gamepadId)?.generation))
        delete this.choices[mode];
    }
  }

  choose(mode: ConnectionMode, choice: BindingChoice | null) {
    if (!choice) { delete this.choices[mode]; this.signatures.delete(mode); return; }
    const gamepad = this.gamepads.find(d => d.id === choice.gamepadId);
    const telemetry = this.telemetry.find(d => d.id === choice.telemetryId && d.sourceMode === mode);
    if (choice.gamepadId && (!gamepad || (deviceRole(gamepad) && deviceRole(gamepad) !== mode)))
      throw new Error("手柄不存在或属于另一个来源");
    if (choice.telemetryId && !telemetry) throw new Error("遥测设备不存在或来源不匹配");
    const other = this.choices[mode === "USB" ? "RF24G" : "USB"];
    if (choice.gamepadId && other?.gamepadId === choice.gamepadId) throw new Error("同一手柄不能同时绑定 USB 与 RF");
    this.choices[mode] = { ...choice };
    this.signatures.delete(mode);
  }

  binding(mode: ConnectionMode): SourceBinding {
    const choice = this.choices[mode];
    const pads = this.gamepads.filter(d => deviceRole(d) === mode);
    const peers = this.telemetry.filter(d => d.sourceMode === mode);
    const ambiguous = !choice && (pads.length > 1 || peers.length > 1);
    const pad = choice ? this.gamepads.find(d => d.id === choice.gamepadId && (!deviceRole(d) || deviceRole(d) === mode))
      : !ambiguous && pads.length === 1 ? pads[0] : undefined;
    const peer = choice ? peers.find(d => d.id === choice.telemetryId)
      : !ambiguous && peers.length === 1 ? peers[0] : undefined;
    // For a manual pair, losing either member suspends control, not button display.
    const paired = !ambiguous && (!choice || !choice.gamepadId || !!pad) &&
      ((pads.length<=1 && peers.length<=1) || (!!pad && !!peer));
    const capable = !!peer?.capable && paired;
    const unknown=this.gamepads.some(d=>deviceRole(d)===null);
    const state = ambiguous ? "ambiguous" : pad ? "connected" : choice ? "disconnected"
      : unknown ? "ambiguous" : this.gamepads.length ? "disconnected" : "unsupported";
    const reason = ambiguous ? "同来源存在多个设备，请选择遥测设备与手柄"
      : pad ? (capable ? "" : "按键已连接；遥测不可用")
      : choice ? "所选手柄已断开，等待原设备"
      : unknown ? "无法自动确认设备身份，请选择手柄" : this.gamepads.length ? "未发现该来源的手柄" : this.backendReason;
    const signature = JSON.stringify([pad?.id, pad?.generation, peer?.id, peer?.generation, capable, state, reason]);
    if (this.signatures.get(mode) !== signature) {
      this.signatures.set(mode, signature);
      this.resolved.set(mode, { sourceMode: mode, gamepadId: pad?.id ?? null, telemetryId: peer?.id ?? null,
        state, reason, generation: ++this.serial, telemetryAvailable: capable });
    }
    return this.resolved.get(mode)!;
  }

  snapshot(): DeviceBindings {
    return { activeSource: this.activeSource, gamepads: this.gamepads, telemetry: this.telemetry,
      bindings: { USB: this.binding("USB"), RF24G: this.binding("RF24G") } };
  }

  read(readings: Map<string, { standardMask: number; timestampMs: number; generation: number }>, now = Date.now()): NativeGamepadSnapshot {
    const b = this.binding(this.activeSource);
    const pad = this.gamepads.find(d => d.id === b.gamepadId);
    const reading = b.gamepadId ? readings.get(b.gamepadId) : undefined;
    const connected = !!pad && !!reading && reading.generation === pad.generation && now - reading.timestampMs < 1000;
    return { sourceMode: b.sourceMode, bindingGeneration: b.generation, connected,
      deviceId: b.gamepadId, standardMask: connected ? reading!.standardMask : 0,
      timestampMs: reading?.timestampMs ?? now, reason: connected ? b.reason : b.reason || "等待新按键状态" };
  }
}
