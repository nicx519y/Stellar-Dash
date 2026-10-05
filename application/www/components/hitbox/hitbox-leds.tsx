'use client';

import { useEffect, useMemo } from 'react';
import { useGamepadConfig } from '@/contexts/gamepad-config-context';
import { useHitboxButtonMonitor } from '@/hooks/use-hitbox-button-monitor';
import { shouldStartButtonMonitoring } from '@/lib/button-monitor-lifecycle';
import { factoryLightSource, type PreviewOptions } from '@/lib/hitbox-lighting-preview';
import type { ResourceSource } from '@/lib/resources';
import type { LedsEffectStyleConfig } from '@/types/gamepad-config';
import { HITBOX_WIDTH, HITBOX_LAYOUT_SCALE } from './hitbox-constants';
import { calculateHitboxScale } from '../setting-content-layout';
import { HitboxLightingCanvas } from './hitbox-lighting-canvas';

interface HitboxLedsProps {
  resource?: ResourceSource | null;
  ambientResource?: ResourceSource | null;
  onClick?: (id: number) => void;
  hasText?: boolean;
  ledsConfig?: LedsEffectStyleConfig;
  interactiveIds?: number[];
  highlightIds?: number[];
  disabledKeys?: number[];
  isButtonMonitoringEnabled?: boolean;
  className?: string;
  containerWidth?: number;
}

export default function HitboxLeds(props: HitboxLedsProps) {
  const { contextJsReady, setContextJsReady, deviceConnected, dataIsReady, hitboxLayout } = useGamepadConfig();
  // Match the fixed drawing box, centered transform and margin used by HitboxKeys/Base.
  const scale = calculateHitboxScale(props.containerWidth ?? 0, HITBOX_WIDTH);
  const layout = useMemo(() => (hitboxLayout ?? []).map(item => ({...item, x:item.x * HITBOX_LAYOUT_SCALE, y:item.y * HITBOX_LAYOUT_SCALE})), [hitboxLayout]);
  const hardware = useHitboxButtonMonitor({
    buttonCount: layout.length, interactiveIds: props.interactiveIds ?? [], disabledIds: props.disabledKeys ?? [],
    enabled: shouldStartButtonMonitoring({ enabled: props.isButtonMonitoringEnabled ?? false, deviceConnected, dataIsReady, contextJsReady, layoutLength: layout.length }),
    onButtonChange: props.onClick, logPrefix: 'hitbox-leds',
  });
  const hardwareMask = hardware.reduce((mask, value, i) => mask | (value === 1 ? 1 << i : 0), 0);
  const config = props.ledsConfig;
  const keys = props.resource ?? factoryLightSource(false, config?.ledsEffectStyle);
  const ambient = props.ambientResource ?? factoryLightSource(true, config?.aroundLedEffectStyle);
  const toColors = (colors: LedsEffectStyleConfig['ledColors'], fallback: number[]) => colors?.map(color => parseInt(color.toString('hex').slice(1),16)) ?? fallback;
  const options: PreviewOptions = {
    keyEnabled: config?.ledEnabled ?? false, ambientEnabled: config?.aroundLedEnabled ?? false,
    sync: config?.aroundLedSyncToMainLed ?? false, oneShot: config?.aroundLedTriggerByButton ?? false,
    keyColors: toColors(config?.ledColors, [0xffffff,0,0]), ambientColors: toColors(config?.aroundLedColors,[0xffffff,0,0]),
    keySpeed: config?.animationSpeed ?? 1, ambientSpeed: config?.aroundLedAnimationSpeed ?? 1,
    keyBrightness: config?.brightness ?? 100, ambientBrightness: config?.aroundLedBrightness ?? 100,
    disabledKeys: props.disabledKeys ?? [],
  };
  useEffect(() => { setContextJsReady(true); }, [setContextJsReady]);
  return <HitboxLightingCanvas keys={keys} ambient={ambient} options={options} layout={layout}
    hardwareMask={hardwareMask} interactiveIds={props.interactiveIds} highlightIds={props.highlightIds}
    onPress={props.onClick} hasText={props.hasText ?? true} className={props.className}
    scale={scale} visible={contextJsReady} resetToken={deviceConnected}/>;
}
