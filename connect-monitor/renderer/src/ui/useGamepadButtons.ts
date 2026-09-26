import { useEffect, useRef, useState } from "react";

import {
  gamepadSnapshotSignature,
  readGamepadButtonsSnapshot,
  type GamepadButtonsSnapshot,
  type PreferredGamepad,
} from "./gamepadButtons";

export type { GamepadButtonsSnapshot, GamepadButtonState } from "./gamepadButtons";

export function useGamepadButtons(): GamepadButtonsSnapshot {
  const [snapshot, setSnapshot] = useState<GamepadButtonsSnapshot>(() => readGamepadButtonsSnapshot());
  const signatureRef = useRef(gamepadSnapshotSignature(snapshot));
  const preferredRef = useRef<PreferredGamepad | null>(null);

  useEffect(() => {
    let rafId = 0;

    const update = () => {
      const nextSnapshot = readGamepadButtonsSnapshot(preferredRef.current);
      if (nextSnapshot.selected) {
        preferredRef.current = nextSnapshot.selected;
      } else if (!nextSnapshot.connected) {
        preferredRef.current = null;
      }
      const nextSignature = gamepadSnapshotSignature(nextSnapshot);
      if (nextSignature !== signatureRef.current) {
        signatureRef.current = nextSignature;
        setSnapshot(nextSnapshot);
      }
      rafId = window.requestAnimationFrame(update);
    };

    const handleGamepadChange = () => {
      const nextSnapshot = readGamepadButtonsSnapshot(preferredRef.current);
      if (nextSnapshot.selected) {
        preferredRef.current = nextSnapshot.selected;
      } else if (!nextSnapshot.connected) {
        preferredRef.current = null;
      }
      const nextSignature = gamepadSnapshotSignature(nextSnapshot);
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
