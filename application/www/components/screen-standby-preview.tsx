'use client';

import React, { useEffect, useState } from 'react';
import { Box } from '@chakra-ui/react';
import type { ScreenStyle } from '@/types/gamepad-config';

const DEVICE_SCREEN_WIDTH = 320;
const DEVICE_SCREEN_HEIGHT = 172;

// Matches BOARD_WIDTH and HITBOX_BUTTON_POS_LIST in Core/Inc/board_cfg.h.
// The third value is the button diameter, despite the firmware field name `r`.
const BOARD_WIDTH = 310.2;
const BUTTON_POSITIONS = [
  [125.10, 103.10, 26], [147.34, 120.10, 34],
  [175.10, 119.10, 26], [192.80, 101.44, 26],
  [73.49, 63.76, 26], [99.05, 59.67, 26],
  [122.19, 63.76, 26], [141.50, 77.34, 26],
  [131.19, 42.04, 26], [165.45, 87.10, 26],
  [163.37, 62.80, 26], [161.29, 38.50, 26],
  [185.51, 73.05, 26], [183.43, 48.75, 26],
  [209.01, 66.10, 26], [206.93, 41.80, 26],
  [233.44, 67.98, 26], [231.36, 43.69, 26],
  [84.49, 15.49, 11.5], [62.49, 15.49, 11.5],
  [40.49, 15.49, 11.5], [18.48, 15.49, 11.5],
] as const;

type ScreenPreviewFrameProps = {
  selected: boolean;
  children: React.ReactNode;
  screenStyle?: ScreenStyle;
  onClick?: () => void;
  label?: string;
};

export function ScreenPreviewFrame({ selected, children, screenStyle, onClick, label }: ScreenPreviewFrameProps) {
  const [displayPixelRatio, setDisplayPixelRatio] = useState(1);

  useEffect(() => {
    const updateDisplayPixelRatio = () => setDisplayPixelRatio(
      Math.max(1, Number(window.devicePixelRatio) || 1),
    );
    updateDisplayPixelRatio();
    window.addEventListener('resize', updateDisplayPixelRatio);
    return () => window.removeEventListener('resize', updateDisplayPixelRatio);
  }, []);

  return <Box
    as={onClick ? 'button' : 'div'}
    aria-label={label}
    aria-hidden={onClick ? undefined : true}
    width={`${DEVICE_SCREEN_WIDTH * (2 / 3) / displayPixelRatio}px`}
    height={`${DEVICE_SCREEN_HEIGHT * (2 / 3) / displayPixelRatio}px`}
    flexShrink="0"
    boxSizing="content-box"
    borderWidth="2px"
    borderColor="gray.600"
    padding="2px"
    transition="border-color 150ms ease, opacity 150ms ease, filter 150ms ease"
    _hover={onClick ? { borderColor: 'green.400' } : undefined}
    _focusVisible={{ outline: '2px solid', outlineColor: 'green.400', outlineOffset: '2px' }}
    position="relative"
    borderRadius="lg"
    overflow="hidden"
    bg={screenStyle === 'light' ? 'white' : screenStyle === 'dark' ? 'black' : 'gray.900'}
    color={screenStyle === 'light' ? 'black' : 'white'}
    opacity={selected ? 1 : 0.35}
    filter={selected ? 'none' : 'grayscale(1)'}
    cursor={onClick ? 'pointer' : 'default'}
    onClick={onClick}
  >
    {children}
  </Box>;
}

export function ScreenStandbyPreview({ mode, selected, screenStyle }: {
  mode: 'none' | 'buttonLayout';
  selected: boolean;
  screenStyle: ScreenStyle;
}) {
  const scale = DEVICE_SCREEN_WIDTH / BOARD_WIDTH;

  return <ScreenPreviewFrame selected={selected} screenStyle={screenStyle}>
    <svg viewBox={`0 0 ${DEVICE_SCREEN_WIDTH} ${DEVICE_SCREEN_HEIGHT}`} width="100%" height="100%" display="block" aria-hidden="true">
      {mode === 'none' ? <path d="M 180 60 L 140 112" stroke="currentColor" strokeWidth="4" strokeLinecap="round" /> : (
        BUTTON_POSITIONS.map(([x, y, diameter], index) => {
          // Same rounding and 90% radius as draw_button_layout in spi_screen_standby.cpp.
          const radius = Math.max(2, Math.trunc((diameter * scale * 0.5 + 0.5) * 9 / 10));
          return <circle key={index} cx={Math.round(x * scale)} cy={Math.round(y * scale)}
            r={radius - 0.5} fill="none" stroke="currentColor" strokeWidth="2" />;
        })
      )}
    </svg>
  </ScreenPreviewFrame>;
}
