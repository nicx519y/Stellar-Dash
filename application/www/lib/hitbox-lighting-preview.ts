import { LightEngine } from '../../../common/xora-light-engine.cjs';
import { lighting, topology } from '../../../common/xora-factory-catalog.cjs';
import type { ResourceSource } from './resources';

export const keyEffectIds = ['key-static', 'key-breath', 'key-star', 'key-flow', 'key-ripple', 'key-transform'];
export const ambientEffectIds = ['ambient-static', 'ambient-breath', 'ambient-quake', 'ambient-meteor'];
export type RGB = number[];
export interface PreviewOptions {
  keyEnabled: boolean; ambientEnabled: boolean; sync: boolean; oneShot: boolean;
  keyColors: number[]; ambientColors: number[];
  keySpeed: number; ambientSpeed: number;
  keyBrightness: number; ambientBrightness: number;
  disabledKeys: readonly number[];
}

export function factoryLightSource(ambient: boolean, effect = 0) {
  const ids = ambient ? ambientEffectIds : keyEffectIds;
  return lighting.find(source => source.resourceId === (ids[effect] ?? ids[0]))!;
}

/** The preview uses the device topology; drawing the border does not change effect geometry. */
export class HitboxLightingPreview {
  private keys: LightEngine;
  private ambient: LightEngine;
  constructor(keys: ResourceSource, ambient: ResourceSource, now = 0) {
    this.keys = new LightEngine(keys, now);
    this.ambient = new LightEngine(ambient, now);
  }
  render(now: number, pointerMask: number, hardwareMask: number, options: PreviewOptions) {
    const mask = options.disabledKeys.reduce((value, key) => value & ~(1 << key), pointerMask | hardwareMask);
    const sync = options.keyEnabled && options.ambientEnabled && options.sync;
    const keys = this.keys.render(now, mask, topology, 22, {
      colors: options.keyColors, speed: options.keySpeed, sync,
    });
    const ambient = sync ? keys : this.ambient.render(now, mask, topology, 22, {
      colors: options.ambientColors, speed: options.ambientSpeed, oneShot: options.oneShot,
    });
    const scale = (color: RGB, brightness: number) => color.map(channel => Math.round(channel * Math.max(0, Math.min(100, brightness)) / 100));
    return {
      keys: keys.slice(0, 22).map((color, i) => scale(color, options.keyEnabled && !options.disabledKeys.includes(i) ? options.keyBrightness : 0)),
      ambient: ambient.slice(22).map(color => scale(color, options.ambientEnabled ? options.ambientBrightness : 0)),
    };
  }
}

// Physical strip runs down the left side, across the bottom, and up the right.
// Project those samples onto the frame; the top smoothly joins the strip ends.
const anchors = topology.slice(22).map(point => {
  const x = (point.x - 35.1) / 240;
  const y = (point.y - 35.1) / 120;
  return point.x < 36 ? y : point.x > 274 ? 2 + (1 - y) : 1 + x;
});
export function ambientBorderColor(colors: RGB[], position: number): RGB {
  if (!colors.length) return [0, 0, 0];
  const p = ((position % 4) + 4) % 4;
  let index = anchors.findIndex((anchor, i) => i + 1 < colors.length && p >= anchor && p < anchors[i + 1]);
  if (index < 0) index = colors.length - 1;
  const next = (index + 1) % colors.length;
  const start = anchors[index] ?? 0, end = next === 0 ? 4 : anchors[next];
  const amount = Math.max(0, Math.min(1, (p - start) / Math.max(0.0001, end - start)));
  return colors[index].map((channel, c) => Math.round(channel + (colors[next][c] - channel) * amount));
}
