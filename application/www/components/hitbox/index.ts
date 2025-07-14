// Hitbox组件导出索引
export { default as HitboxBase } from './hitbox-base';
export { default as HitboxKeys } from './hitbox-keys';
export { default as HitboxHotkey } from './hitbox-hotkey';
export { default as HitboxCalibration } from './hitbox-calibration';
export { default as HitboxPerformance } from './hitbox-performance';
export { default as HitboxLeds } from './hitbox-leds';
export { default as HitboxEnableSetting } from './hitbox-enableSetting';

// 导出类型
export type { HitboxBaseProps } from './hitbox-base';

// 保持向后兼容，导出原始Hitbox组件（现在是HitboxBase的别名）
export { default as Hitbox } from './hitbox-base'; 