import { useEffect, useRef } from "react";

import type { HitboxSummary } from "../../../shared/monitor-types";
import {
  createHitboxSummary,
  gamepadSnapshotSignature,
  readGamepadButtonsSnapshot,
  type GamepadButtonsSnapshot,
  type PreferredGamepad,
} from "./gamepadButtons";
import { HITBOX_BUTTON_MAP, type HitboxButtonConfig } from "./hitboxButtonMap";

const HITBOX_WIDTH = 787;
const HITBOX_HEIGHT = 489;
const HITBOX_LAYOUT_SCALE = 2.55;
const COMPACT_CONTENT_SCALE = 1.25;
const COMPACT_CONTENT_OFFSET_Y = 34;
const COMPACT_RENDER_SCALE = 0.72;
const LABEL_FONT_SIZE = 16;
const BUTTON_FRAME_RADIUS_DISTANCE = 3;
const SUMMARY_INTERVAL_MS = 100;

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

function isButtonPressed(snapshot: GamepadButtonsSnapshot, buttonIndex: number) {
  return snapshot.buttonStates[buttonIndex]?.isPressed ?? false;
}

function drawCircle(
  ctx: CanvasRenderingContext2D,
  x: number,
  y: number,
  radius: number,
  fillStyle: string | null,
  strokeStyle: string,
  lineWidth: number,
  shadowColor?: string,
  shadowBlur = 0,
) {
  ctx.save();
  ctx.beginPath();
  ctx.arc(x, y, radius, 0, Math.PI * 2);
  ctx.shadowColor = shadowColor ?? "transparent";
  ctx.shadowBlur = shadowBlur;
  if (fillStyle) {
    ctx.fillStyle = fillStyle;
    ctx.fill();
  }
  ctx.strokeStyle = strokeStyle;
  ctx.lineWidth = lineWidth;
  ctx.stroke();
  ctx.restore();
}

function drawHitbox(ctx: CanvasRenderingContext2D, cssWidth: number, cssHeight: number, snapshot: GamepadButtonsSnapshot, compact: boolean) {
  ctx.clearRect(0, 0, cssWidth, cssHeight);

  const renderScale = compact ? COMPACT_RENDER_SCALE : 1;
  const offsetX = (cssWidth - HITBOX_WIDTH * renderScale) / 2;
  const offsetY = (cssHeight - HITBOX_HEIGHT * renderScale) / 2;
  const contentScale = compact ? COMPACT_CONTENT_SCALE : 1;

  ctx.save();
  ctx.translate(offsetX, offsetY);
  ctx.scale(renderScale, renderScale);

  ctx.save();
  ctx.translate(HITBOX_WIDTH / 2, HITBOX_HEIGHT / 2 + (compact ? COMPACT_CONTENT_OFFSET_Y : 0));
  ctx.scale(contentScale, contentScale);
  ctx.translate(-HITBOX_WIDTH / 2, -HITBOX_HEIGHT / 2);

  for (let index = 0; index < scaledHitboxLayout.length; index += 1) {
    const item = scaledHitboxLayout[index];
    const pressed = isButtonPressed(snapshot, index);
    const radius = displayRadius(item, compact);
    drawCircle(
      ctx,
      item.x,
      item.y,
      radius + BUTTON_FRAME_RADIUS_DISTANCE,
      null,
      pressed ? "rgba(177,255,74,0.9)" : "rgba(180,190,186,0.62)",
      pressed ? 3 : 1,
      pressed ? "rgba(154,205,50,0.85)" : undefined,
      pressed ? 10 : 0,
    );
  }

  for (let index = 0; index < scaledHitboxLayout.length; index += 1) {
    const item = scaledHitboxLayout[index];
    const pressed = isButtonPressed(snapshot, index);
    const radius = displayRadius(item, compact);
    drawCircle(
      ctx,
      item.x,
      item.y,
      radius,
      pressed ? "rgba(92,255,138,0.92)" : "rgba(6,15,18,0.96)",
      pressed ? "rgba(221,255,229,0.98)" : "rgba(150,165,160,0.86)",
      pressed ? 2 : 1,
      pressed ? "rgba(92,255,138,0.88)" : undefined,
      pressed ? 16 : 0,
    );
  }

  ctx.restore();

  ctx.font = `700 ${LABEL_FONT_SIZE}px system-ui, sans-serif`;
  ctx.textAlign = "center";
  ctx.textBaseline = "middle";
  for (let index = 0; index < scaledHitboxLayout.length; index += 1) {
    const item = scaledHitboxLayout[index];
    const labelY = index < scaledHitboxLayout.length - 3 ? item.y : item.y + 30;
    const pressed = isButtonPressed(snapshot, index);
    const point = transformPoint(item.x, labelY, compact);
    ctx.fillStyle = pressed ? "#02150a" : "#f1fff5";
    ctx.fillText(item.label, point.x, point.y);
  }

  ctx.restore();
}

function resizeCanvas(canvas: HTMLCanvasElement, ctx: CanvasRenderingContext2D) {
  const rect = canvas.getBoundingClientRect();
  const dpr = Math.max(1, window.devicePixelRatio || 1);
  const pixelWidth = Math.max(1, Math.round(rect.width * dpr));
  const pixelHeight = Math.max(1, Math.round(rect.height * dpr));

  if (canvas.width !== pixelWidth || canvas.height !== pixelHeight) {
    canvas.width = pixelWidth;
    canvas.height = pixelHeight;
  }

  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  return {
    width: Math.max(1, rect.width),
    height: Math.max(1, rect.height),
  };
}

export function HitboxCanvas({
  compact,
  onSummary,
}: {
  compact: boolean;
  onSummary?: (summary: HitboxSummary) => void;
}) {
  const canvasRef = useRef<HTMLCanvasElement | null>(null);
  const onSummaryRef = useRef(onSummary);
  onSummaryRef.current = onSummary;

  useEffect(() => {
    const canvas = canvasRef.current;
    const ctx = canvas?.getContext("2d");
    if (!canvas || !ctx) return;

    let rafId = 0;
    let preferred: PreferredGamepad | null = null;
    let lastDrawSignature = "";
    let lastSummarySignature = "";
    let lastSummaryAt = 0;
    let lastCssWidth = 0;
    let lastCssHeight = 0;

    const publishSummary = (snapshot: GamepadButtonsSnapshot, force = false) => {
      const summary = createHitboxSummary(snapshot);
      const summarySignature = `${summary.connected ? "1" : "0"}:${summary.deviceId ?? ""}:${summary.pressedCount}`;
      const now = performance.now();
      if (!force && summarySignature === lastSummarySignature && now - lastSummaryAt < SUMMARY_INTERVAL_MS) {
        return;
      }
      lastSummarySignature = summarySignature;
      lastSummaryAt = now;
      onSummaryRef.current?.(summary);
    };

    const tick = () => {
      const snapshot = readGamepadButtonsSnapshot(preferred);
      if (snapshot.selected) {
        preferred = snapshot.selected;
      } else if (!snapshot.connected) {
        preferred = null;
      }

      const size = resizeCanvas(canvas, ctx);
      const drawSignature = `${gamepadSnapshotSignature(snapshot)}:${compact ? "1" : "0"}`;
      const sizeChanged = size.width !== lastCssWidth || size.height !== lastCssHeight;
      if (drawSignature !== lastDrawSignature || sizeChanged) {
        lastDrawSignature = drawSignature;
        lastCssWidth = size.width;
        lastCssHeight = size.height;
        drawHitbox(ctx, size.width, size.height, snapshot, compact);
      }
      publishSummary(snapshot);
      rafId = window.requestAnimationFrame(tick);
    };

    const forceRefresh = () => {
      lastDrawSignature = "";
      lastSummarySignature = "";
    };

    window.addEventListener("resize", forceRefresh);
    window.addEventListener("gamepadconnected", forceRefresh);
    window.addEventListener("gamepaddisconnected", forceRefresh);
    rafId = window.requestAnimationFrame(tick);

    return () => {
      window.cancelAnimationFrame(rafId);
      window.removeEventListener("resize", forceRefresh);
      window.removeEventListener("gamepadconnected", forceRefresh);
      window.removeEventListener("gamepaddisconnected", forceRefresh);
    };
  }, [compact]);

  return (
    <canvas
      ref={canvasRef}
      style={{
        display: "block",
        width: "100%",
        height: "100%",
        minWidth: 0,
        minHeight: 0,
        pointerEvents: "none",
      }}
    />
  );
}
