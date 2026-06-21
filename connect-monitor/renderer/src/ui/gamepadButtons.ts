import type { HitboxSummary } from "../../../shared/monitor-types";
import { HITBOX_BUTTON_MAP, UNMAPPED_GAMEPAD_BUTTON } from "./hitboxButtonMap";

export type GamepadButtonState = {
  buttonIndex: number;
  isPressed: boolean;
};

export type GamepadButtonsSnapshot = {
  connected: boolean;
  deviceId: string | null;
  timestampMs: number;
  buttonStates: GamepadButtonState[];
};

export type PreferredGamepad = {
  index: number;
  id: string;
};

const PRESS_THRESHOLD = 0.5;
const DEVICE_ID_HINT = /(xbox|xinput|controller|hbox)/i;

const releasedButtonStates = HITBOX_BUTTON_MAP.map((_button, buttonIndex) => ({
  buttonIndex,
  isPressed: false,
}));

function getGamepads(): Gamepad[] {
  if (typeof navigator === "undefined" || !navigator.getGamepads) {
    return [];
  }
  return Array.from(navigator.getGamepads()).filter((gamepad): gamepad is Gamepad => Boolean(gamepad));
}

function chooseGamepad(gamepads: Gamepad[], preferred?: PreferredGamepad | null) {
  if (preferred) {
    const exact = gamepads.find((gamepad) => gamepad.index === preferred.index && gamepad.id === preferred.id);
    if (exact) return exact;
  }

  const standardGamepads = gamepads.filter((gamepad) => gamepad.mapping === "standard");
  return (
    standardGamepads.find((gamepad) => DEVICE_ID_HINT.test(gamepad.id)) ??
    standardGamepads[0] ??
    gamepads.find((gamepad) => DEVICE_ID_HINT.test(gamepad.id)) ??
    gamepads[0] ??
    null
  );
}

function isPressed(button: GamepadButton | undefined) {
  return Boolean(button && (button.pressed || button.value >= PRESS_THRESHOLD));
}

export function readGamepadButtonsSnapshot(
  preferred?: PreferredGamepad | null,
): GamepadButtonsSnapshot & { selected?: PreferredGamepad } {
  const gamepad = chooseGamepad(getGamepads(), preferred);
  if (!gamepad) {
    return {
      connected: false,
      deviceId: null,
      timestampMs: Date.now(),
      buttonStates: releasedButtonStates,
    };
  }

  return {
    connected: true,
    deviceId: gamepad.id,
    timestampMs: Date.now(),
    selected: {
      index: gamepad.index,
      id: gamepad.id,
    },
    buttonStates: HITBOX_BUTTON_MAP.map((button, buttonIndex) => ({
      buttonIndex,
      isPressed: button.gamepadButtonIndex !== UNMAPPED_GAMEPAD_BUTTON ? isPressed(gamepad.buttons[button.gamepadButtonIndex]) : false,
    })),
  };
}

export function gamepadSnapshotSignature(snapshot: GamepadButtonsSnapshot) {
  const pressedMask = snapshot.buttonStates.map((button) => (button.isPressed ? "1" : "0")).join("");
  return `${snapshot.connected ? "1" : "0"}:${snapshot.deviceId ?? ""}:${pressedMask}`;
}

export function createHitboxSummary(snapshot: GamepadButtonsSnapshot): HitboxSummary {
  return {
    connected: snapshot.connected,
    deviceId: snapshot.deviceId,
    pressedCount: snapshot.buttonStates.filter((state) => state.isPressed).length,
    timestampMs: snapshot.timestampMs,
  };
}
