import { Badge, Box, Card } from "@chakra-ui/react";
import { useMemo } from "react";

import { PanelHeader, panelSurfaceProps } from "./panelStyles";
import { HITBOX_BUTTON_MAP, StandardGamepadButton, UNMAPPED_GAMEPAD_BUTTON, type HitboxButtonConfig } from "./hitboxButtonMap";
import { useGamepadButtons, type GamepadButtonState } from "./useGamepadButtons";

const HITBOX_WIDTH = 787;
const HITBOX_HEIGHT = 489;
const HITBOX_LAYOUT_SCALE = 2.55;
const COMPACT_CONTENT_SCALE = 1.25;
const COMPACT_CONTENT_OFFSET_Y = 34;
const LABEL_FONT_SIZE = 8;
const BUTTON_FRAME_RADIUS_DISTANCE = 3;

export type RfInputSnapshot = {
  keyMask: number;
  timestampMs: number;
};

const hboxMaskByGamepadButton: Record<number, number | undefined> = {
  [StandardGamepadButton.A]: 1 << 4,
  [StandardGamepadButton.B]: 1 << 5,
  [StandardGamepadButton.X]: 1 << 6,
  [StandardGamepadButton.Y]: 1 << 7,
  [StandardGamepadButton.LeftBumper]: 1 << 8,
  [StandardGamepadButton.RightBumper]: 1 << 9,
  [StandardGamepadButton.LeftTrigger]: 1 << 10,
  [StandardGamepadButton.RightTrigger]: 1 << 11,
  [StandardGamepadButton.Back]: 1 << 12,
  [StandardGamepadButton.Start]: 1 << 13,
  [StandardGamepadButton.LeftStick]: 1 << 14,
  [StandardGamepadButton.RightStick]: 1 << 15,
  [StandardGamepadButton.DPadUp]: 1 << 0,
  [StandardGamepadButton.DPadDown]: 1 << 1,
  [StandardGamepadButton.DPadLeft]: 1 << 2,
  [StandardGamepadButton.DPadRight]: 1 << 3,
  [StandardGamepadButton.Guide]: 1 << 16,
};

const scaledHitboxLayout = HITBOX_BUTTON_MAP.map((item) => ({
  ...item,
  x: item.x * HITBOX_LAYOUT_SCALE,
  y: item.y * HITBOX_LAYOUT_SCALE,
  r: item.r * HITBOX_LAYOUT_SCALE,
}));

function displayRadius(item: HitboxButtonConfig, compact: boolean) {
  return compact ? item.r * 0.4 : item.r;
}

function transformPoint(x: number, y: number, compact: boolean) {
  const scale = compact ? COMPACT_CONTENT_SCALE : 1;
  const centerX = HITBOX_WIDTH / 2;
  const centerY = HITBOX_HEIGHT / 2;
  const offsetY = compact ? COMPACT_CONTENT_OFFSET_Y : 0;
  return {
    x: centerX + (x - centerX) * scale,
    y: centerY + offsetY + (y - centerY) * scale,
  };
}

function isButtonPressed(buttonStates: GamepadButtonState[], buttonIndex: number) {
  return buttonStates.find((state) => state.buttonIndex === buttonIndex)?.isPressed ?? false;
}

function buttonMeta(buttonStates: GamepadButtonState[]) {
  const pressed = buttonStates.filter((state) => state.isPressed).length;
  return `${pressed} pressed`;
}

function buttonStatesFromRfMask(keyMask: number): GamepadButtonState[] {
  return HITBOX_BUTTON_MAP.map((button, buttonIndex) => {
    const mask = button.gamepadButtonIndex !== UNMAPPED_GAMEPAD_BUTTON
      ? hboxMaskByGamepadButton[button.gamepadButtonIndex]
      : undefined;
    return {
      buttonIndex,
      isPressed: typeof mask === "number" ? (keyMask & mask) !== 0 : false,
    };
  });
}

function HitboxPreview({ buttonStates, compact = false }: { buttonStates: GamepadButtonState[]; compact?: boolean }) {
  const contentScale = compact ? COMPACT_CONTENT_SCALE : 1;

  return (
    <Box
      w="100%"
      maxW={compact ? "none" : "920px"}
      mx="auto"
      overflow="hidden"
      p={compact ? 0 : { base: 2, md: 4 }}
      flex={compact ? "1" : undefined}
      minH={0}
      display={compact ? "flex" : "block"}
      alignItems="center"
    >
      <svg
        xmlns="http://www.w3.org/2000/svg"
        viewBox={`0 0 ${HITBOX_WIDTH} ${HITBOX_HEIGHT}`}
        width="100%"
        height="100%"
        style={{ display: "block", aspectRatio: `${HITBOX_WIDTH} / ${HITBOX_HEIGHT}` }}
      >
        <title>Hitbox XInput buttons</title>
        <g transform={`translate(${HITBOX_WIDTH / 2} ${HITBOX_HEIGHT / 2 + (compact ? COMPACT_CONTENT_OFFSET_Y : 0)}) scale(${contentScale}) translate(${-HITBOX_WIDTH / 2} ${-HITBOX_HEIGHT / 2})`}>
          {scaledHitboxLayout.map((item, index) => {
            const pressed = isButtonPressed(buttonStates, index);
            const radius = displayRadius(item, compact);
            return (
              <circle
                key={`frame-${index}`}
                cx={item.x}
                cy={item.y}
                r={radius + BUTTON_FRAME_RADIUS_DISTANCE}
                fill="none"
                stroke={pressed ? "rgba(177,255,74,0.9)" : "rgba(180,190,186,0.62)"}
                strokeWidth={pressed ? "3" : "1"}
                filter={pressed ? "drop-shadow(0 0 10px rgba(154,205,50,0.85))" : undefined}
              />
            );
          })}
          {scaledHitboxLayout.map((item, index) => {
            const pressed = isButtonPressed(buttonStates, index);
            const radius = displayRadius(item, compact);
            return (
              <circle
                key={`button-${index}`}
                cx={item.x}
                cy={item.y}
                r={radius}
                fill={pressed ? "rgba(92,255,138,0.92)" : "rgba(6,15,18,0.96)"}
                stroke={pressed ? "rgba(221,255,229,0.98)" : "rgba(150,165,160,0.86)"}
                strokeWidth={pressed ? "2" : "1"}
                filter={pressed ? "drop-shadow(0 0 16px rgba(92,255,138,0.88))" : undefined}
              />
            );
          })}
        </g>
        {scaledHitboxLayout.map((item, index) => {
          const labelY = index < scaledHitboxLayout.length - 3 ? item.y : item.y + 30;
          const pressed = isButtonPressed(buttonStates, index);
          const point = transformPoint(item.x, labelY, compact);
          return (
            <text
              key={`label-${index}`}
              x={point.x}
              y={point.y}
              textAnchor="middle"
              dominantBaseline="middle"
              fontFamily="system-ui, sans-serif"
              pointerEvents="none"
              fontSize={LABEL_FONT_SIZE}
              fontWeight="700"
              fill={pressed ? "#02150a" : "#f1fff5"}
            >
              {item.label}
            </text>
          );
        })}
      </svg>
    </Box>
  );
}

export function ButtonsPanel({ compact = false, rfInput }: { compact?: boolean; rfInput?: RfInputSnapshot | null }) {
  const gamepad = useGamepadButtons();
  const rfButtonStates = useMemo(() => rfInput ? buttonStatesFromRfMask(rfInput.keyMask) : null, [rfInput]);
  const buttonStates = rfButtonStates ?? gamepad.buttonStates;
  const connected = Boolean(rfInput) || gamepad.connected;
  const sourceLabel = rfInput ? "RF Input" : gamepad.connected ? "XInput Connected" : "Disconnected";
  const meta = useMemo(() => buttonMeta(buttonStates), [buttonStates]);

  return (
    <Card.Root variant="outline" overflow="hidden" h="100%" {...panelSurfaceProps}>
      <PanelHeader
        title="Gamepad Buttons"
        meta={meta}
        action={
          <Badge colorPalette={connected ? "green" : "gray"}>
            {sourceLabel}
          </Badge>
        }
        borderBottom
        compact={compact}
      />
      <Card.Body
        px={compact ? 3 : { base: 3, md: 5 }}
        py={compact ? 3 : { base: 4, md: 5 }}
        display="flex"
        flexDirection="column"
        flex="1"
        minH={0}
      >
        <HitboxPreview buttonStates={buttonStates} compact={compact} />
      </Card.Body>
    </Card.Root>
  );
}
