import { useEffect, useRef, useState } from "react";

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

type PreferredGamepad = {
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

function readSnapshot(preferred?: PreferredGamepad | null): GamepadButtonsSnapshot & { selected?: PreferredGamepad } {
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

function snapshotSignature(snapshot: GamepadButtonsSnapshot) {
  const pressedMask = snapshot.buttonStates.map((button) => (button.isPressed ? "1" : "0")).join("");
  return `${snapshot.connected ? "1" : "0"}:${snapshot.deviceId ?? ""}:${pressedMask}`;
}

export function useGamepadButtons(): GamepadButtonsSnapshot {
  const [snapshot, setSnapshot] = useState<GamepadButtonsSnapshot>(() => readSnapshot());
  const signatureRef = useRef(snapshotSignature(snapshot));
  const preferredRef = useRef<PreferredGamepad | null>(null);

  useEffect(() => {
    let rafId = 0;

    const update = () => {
      const nextSnapshot = readSnapshot(preferredRef.current);
      if (nextSnapshot.selected) {
        preferredRef.current = nextSnapshot.selected;
      } else if (!nextSnapshot.connected) {
        preferredRef.current = null;
      }
      const nextSignature = snapshotSignature(nextSnapshot);
      if (nextSignature !== signatureRef.current) {
        signatureRef.current = nextSignature;
        setSnapshot(nextSnapshot);
      }
      rafId = window.requestAnimationFrame(update);
    };

    const handleGamepadChange = () => {
      const nextSnapshot = readSnapshot(preferredRef.current);
      if (nextSnapshot.selected) {
        preferredRef.current = nextSnapshot.selected;
      } else if (!nextSnapshot.connected) {
        preferredRef.current = null;
      }
      const nextSignature = snapshotSignature(nextSnapshot);
      signatureRef.current = nextSignature;
      setSnapshot(nextSnapshot);
    };

    window.addEventListener("gamepadconnected", handleGamepadChange);
    window.addEventListener("gamepaddisconnected", handleGamepadChange);
    rafId = window.requestAnimationFrame(update);

    return () => {
      window.cancelAnimationFrame(rafId);
      window.removeEventListener("gamepadconnected", handleGamepadChange);
      window.removeEventListener("gamepaddisconnected", handleGamepadChange);
    };
  }, []);

  return snapshot;
}
