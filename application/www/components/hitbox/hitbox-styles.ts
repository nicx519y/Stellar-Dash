import styled, { css } from "styled-components";

import {
  HITBOX_HEIGHT,
  HITBOX_PADDING,
  HITBOX_WIDTH,
} from "./hitbox-constants";

/**
 * Shared visual primitives for every Hitbox variant.
 *
 * The classic values intentionally mirror the original per-component styles.
 * Cyber overrides are scoped through the root data-skin attribute, so switching
 * skins never changes component state or the fill supplied by LED/device data.
 */
export const StyledSvg = styled.svg<{
  $scale?: number;
}>`
  --hitbox-button-stroke: gray;
  --hitbox-outer-stroke: gray;
  --hitbox-highlight-stroke: yellowgreen;
  --hitbox-hover-stroke: #ccc;
  --hitbox-frame-stroke: gray;
  --hitbox-button-stroke-width: 1px;
  --hitbox-outer-stroke-width: 1px;
  --hitbox-frame-stroke-width: 1px;
  --hitbox-idle-filter: none;
  --hitbox-outer-filter: none;
  --hitbox-highlight-filter: drop-shadow(0 0 2px rgba(154, 205, 50, 0.8));
  --hitbox-hover-filter: drop-shadow(0 0 10px rgba(204, 204, 204, 0.8));
  --hitbox-active-filter: drop-shadow(0 0 15px rgba(154, 205, 50, 0.9));
  --hitbox-frame-filter: drop-shadow(0 0 5px rgba(204, 204, 204, 0.8));

  width: ${HITBOX_WIDTH + HITBOX_PADDING * 2 + 2}px;
  height: ${HITBOX_HEIGHT + HITBOX_PADDING * 2 + 2}px;
  padding: ${HITBOX_PADDING}px;
  position: relative;
  transform: scale(${props => props.$scale || 1});
  transform-origin: center;

  :root[data-skin="cyber"] &,
  [data-skin="cyber"] & {
    --hitbox-button-stroke: var(--chakra-colors-hud-cyan, #58eaf4);
    --hitbox-outer-stroke: var(--chakra-colors-hud-purple, #9e8cff);
    --hitbox-highlight-stroke: var(--chakra-colors-hud-success, #7cff42);
    --hitbox-hover-stroke: var(--chakra-colors-app-text-primary, #eaf7fb);
    --hitbox-frame-stroke: var(--chakra-colors-hud-cyan, #58eaf4);
    --hitbox-button-stroke-width: 1.35px;
    --hitbox-outer-stroke-width: 1.65px;
    --hitbox-frame-stroke-width: 1.25px;
    --hitbox-idle-filter:
      drop-shadow(0 0 2px rgba(88, 234, 244, 0.85))
      drop-shadow(0 0 7px rgba(88, 234, 244, 0.32));
    --hitbox-outer-filter:
      drop-shadow(0 0 2px rgba(158, 140, 255, 0.95))
      drop-shadow(0 0 8px rgba(158, 140, 255, 0.5));
    --hitbox-highlight-filter:
      drop-shadow(0 0 3px rgba(124, 255, 66, 0.95))
      drop-shadow(0 0 11px rgba(88, 234, 244, 0.55));
    --hitbox-hover-filter:
      drop-shadow(0 0 3px rgba(234, 247, 251, 0.95))
      drop-shadow(0 0 12px rgba(88, 234, 244, 0.72));
    --hitbox-active-filter:
      drop-shadow(0 0 4px rgba(124, 255, 66, 0.95))
      drop-shadow(0 0 15px rgba(158, 140, 255, 0.72));
    --hitbox-frame-filter:
      drop-shadow(0 0 3px rgba(88, 234, 244, 0.8))
      drop-shadow(0 0 10px rgba(158, 140, 255, 0.42));
  }
`;

export interface StyledCircleProps {
  $opacity?: number;
  $interactive?: boolean;
  $highlight?: boolean;
  $fillNone?: boolean;
  $pressed?: boolean;
}

export const StyledCircle = styled.circle<StyledCircleProps>`
  cursor: ${props => props.$interactive ? "pointer" : "default"};
  pointer-events: ${props => props.$interactive ? "auto" : "none"};
  opacity: ${props => props.$opacity};
  stroke: ${props => props.$highlight
    ? "var(--hitbox-highlight-stroke)"
    : props.$fillNone
      ? "var(--hitbox-outer-stroke)"
      : "var(--hitbox-button-stroke)"};
  stroke-width: ${props => props.$highlight
    ? "2px"
    : props.$fillNone
      ? "var(--hitbox-outer-stroke-width)"
      : "var(--hitbox-button-stroke-width)"};
  filter: ${props => props.$highlight
    ? "var(--hitbox-highlight-filter)"
    : props.$fillNone
      ? "var(--hitbox-outer-filter)"
      : "var(--hitbox-idle-filter)"};
  fill: ${props => props.$fillNone ? "none" : ""};

  &:hover {
    stroke-width: ${props => props.$interactive ? "2px" : "var(--hitbox-button-stroke-width)"};
    stroke: ${props => props.$interactive ? "var(--hitbox-hover-stroke)" : "var(--hitbox-button-stroke)"};
    filter: ${props => props.$interactive ? "var(--hitbox-hover-filter)" : "var(--hitbox-idle-filter)"};
  }

  ${props => props.$pressed && css`
    stroke: var(--hitbox-highlight-stroke);
    stroke-width: 2px;
    filter: var(--hitbox-active-filter);
  `}

  &:active {
    stroke-width: ${props => props.$interactive ? "2px" : "var(--hitbox-button-stroke-width)"};
    stroke: ${props => props.$interactive ? "var(--hitbox-highlight-stroke)" : "var(--hitbox-button-stroke)"};
    filter: ${props => props.$interactive ? "var(--hitbox-active-filter)" : "var(--hitbox-idle-filter)"};
  }
`;

export const StyledFrame = styled.rect`
  fill: none;
  stroke: var(--hitbox-frame-stroke);
  stroke-width: var(--hitbox-frame-stroke-width);
  filter: var(--hitbox-frame-filter);
`;

export const StyledText = styled.text`
  text-align: center;
  font-family: 'custom_en', system-ui, sans-serif;
  cursor: default;
  pointer-events: none;
`;

export const StyledCompactText = styled.text`
  text-align: center;
  font-family: "Helvetica", cursive;
  font-size: 0.9rem;
  cursor: default;
  pointer-events: none;
`;

export const BUTTON_FRAME_RADIUS_DISTANCE = 3;
