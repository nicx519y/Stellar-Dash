export const UNMAPPED_GAMEPAD_BUTTON = -1;

export enum StandardGamepadButton {
  A = 0,
  B = 1,
  X = 2,
  Y = 3,
  LeftBumper = 4,
  RightBumper = 5,
  LeftTrigger = 6,
  RightTrigger = 7,
  Back = 8,
  Start = 9,
  LeftStick = 10,
  RightStick = 11,
  DPadUp = 12,
  DPadDown = 13,
  DPadLeft = 14,
  DPadRight = 15,
  Guide = 16,
}

export type HitboxButtonConfig = {
  label: string;
  gamepadButtonIndex: number;
  x: number;
  y: number;
  r: number;
};

export const HITBOX_BUTTON_MAP: HitboxButtonConfig[] = [
  { label: "L3", gamepadButtonIndex: StandardGamepadButton.LeftStick, x: 125.1, y: 103.1, r: 26 },
  { label: "UP", gamepadButtonIndex: StandardGamepadButton.DPadUp, x: 147.34, y: 120.1, r: 34 },
  { label: "R3", gamepadButtonIndex: StandardGamepadButton.RightStick, x: 175.1, y: 119.1, r: 26 },
  { label: "", gamepadButtonIndex: UNMAPPED_GAMEPAD_BUTTON, x: 192.8, y: 101.44, r: 26 },
  { label: "", gamepadButtonIndex: UNMAPPED_GAMEPAD_BUTTON, x: 73.49, y: 63.76, r: 26 },
  { label: "LEFT", gamepadButtonIndex: StandardGamepadButton.DPadLeft, x: 99.05, y: 59.67, r: 26 },
  { label: "DOWN", gamepadButtonIndex: StandardGamepadButton.DPadDown, x: 122.19, y: 63.76, r: 26 },
  { label: "RIGHT", gamepadButtonIndex: StandardGamepadButton.DPadRight, x: 141.5, y: 77.34, r: 26 },
  { label: "UP", gamepadButtonIndex: StandardGamepadButton.DPadUp, x: 131.19, y: 42.04, r: 26 },
  { label: "A", gamepadButtonIndex: StandardGamepadButton.A, x: 165.45, y: 87.1, r: 26 },
  { label: "X", gamepadButtonIndex: StandardGamepadButton.X, x: 163.37, y: 62.8, r: 26 },
  { label: "", gamepadButtonIndex: UNMAPPED_GAMEPAD_BUTTON, x: 161.29, y: 38.5, r: 26 },
  { label: "B", gamepadButtonIndex: StandardGamepadButton.B, x: 185.51, y: 73.05, r: 26 },
  { label: "Y", gamepadButtonIndex: StandardGamepadButton.Y, x: 183.43, y: 48.75, r: 26 },
  { label: "LT", gamepadButtonIndex: StandardGamepadButton.LeftTrigger, x: 209.01, y: 66.1, r: 26 },
  { label: "LB", gamepadButtonIndex: StandardGamepadButton.LeftBumper, x: 206.93, y: 41.8, r: 26 },
  { label: "RT", gamepadButtonIndex: StandardGamepadButton.RightTrigger, x: 233.44, y: 67.98, r: 26 },
  { label: "RB", gamepadButtonIndex: StandardGamepadButton.RightBumper, x: 231.36, y: 43.69, r: 26 },
  { label: "Start", gamepadButtonIndex: StandardGamepadButton.Start, x: 78.49, y: 15.49, r: 11.5 },
  { label: "Back", gamepadButtonIndex: StandardGamepadButton.Back, x: 58.49, y: 15.49, r: 11.5 },
  { label: "Guide", gamepadButtonIndex: StandardGamepadButton.Guide, x: 38.49, y: 15.49, r: 11.5 },
] as const;
