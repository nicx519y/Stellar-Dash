import { DEFAULT_SCREEN_CONTROL_CONFIG, normalizeScreenStandbyTimeout, type ScreenControlConfig, type ScreenControlFeatureKey } from '../types/gamepad-config';

export const SCREEN_FEATURE_IDS: Record<ScreenControlFeatureKey, number> = {
  inputModeSwitch: 0, profilesSwitch: 1, socdModeSwitch: 2, connectionModeSwitch: 3,
  buttonsPerformanceQuickSet: 11, ledSetting: 13, screenBrightnessAdjust: 8, power: 14,
  webConfigEntry: 9, calibrationModeSwitch: 10,
};
const legacyLedKeys = ['ledBrightnessAdjust', 'ledEffectSwitch', 'ambientBrightnessAdjust', 'ambientEffectSwitch'];
const record = (value: unknown): Record<string, unknown> => value && typeof value === 'object' && !Array.isArray(value) ? value as Record<string, unknown> : {};

/** Normalize storage/backups; live reads additionally remember firmware capability. */
export function normalizeScreenControl(value: unknown, live = false): ScreenControlConfig {
  const raw = record(value), oldFeatures = record(raw.features);
  const defaults = DEFAULT_SCREEN_CONTROL_CONFIG;
  const legacy = legacyLedKeys.some(key => key in oldFeatures) && !('ledSetting' in oldFeatures);
  const features = { ...defaults.features };
  for (const key of Object.keys(features) as ScreenControlFeatureKey[]) {
    if (typeof oldFeatures[key] === 'boolean') features[key] = oldFeatures[key] as boolean;
  }
  if (legacy) features.ledSetting = legacyLedKeys.some(key => oldFeatures[key] === true);
  features.webConfigEntry = true;
  const order: ScreenControlFeatureKey[] = [];
  const add = (key: unknown) => {
    if (typeof key === 'string' && Object.hasOwn(features, key) && !order.includes(key as ScreenControlFeatureKey)) order.push(key as ScreenControlFeatureKey);
  };
  for (let key of Array.isArray(raw.featuresOrder) ? raw.featuresOrder : []) {
    if (legacyLedKeys.includes(key)) key = 'ledSetting';
    add(key);
    if (legacy && key === 'screenBrightnessAdjust') add('power');
  }
  defaults.featuresOrder.forEach(add);
  let page = typeof raw.currentPageId === 'number' ? raw.currentPageId : defaults.currentPageId;
  if (page >= 4 && page <= 7) page = 13;
  if (!order.some(key => SCREEN_FEATURE_IDS[key] === page && features[key])) page = SCREEN_FEATURE_IDS[order.find(key => features[key])!];
  return {
    ...defaults, ...raw,
    standbyEnabled: raw.standbyDisplay === 'none' ? false : typeof raw.standbyEnabled === 'boolean' ? raw.standbyEnabled : raw.standbyDisplay === 'backgroundImage' || raw.standbyDisplay === 'buttonLayout',
    standbyDisplay: raw.standbyDisplay === 'backgroundImage' || raw.standbyDisplay === 'buttonLayout' ? raw.standbyDisplay : 'screenOff',
    standbyTimeoutSeconds: normalizeScreenStandbyTimeout(raw.standbyTimeoutSeconds),
    standbySupported: live ? typeof raw.standbyEnabled === 'boolean' && raw.standbySupported !== false : raw.standbySupported !== false,
    features, featuresOrder: order, currentPageId: page,
  } as ScreenControlConfig;
}

export function applyScreenControlPatch(current: ScreenControlConfig, patch: Record<string, unknown>, importing = false): ScreenControlConfig {
  if (patch.standbyEnabled !== undefined && typeof patch.standbyEnabled !== 'boolean') throw new Error('Invalid standbyEnabled');
  if (patch.standbyDisplay !== undefined && !['none', 'screenOff', 'backgroundImage', 'buttonLayout'].includes(patch.standbyDisplay as string)) throw new Error('Invalid standby display');
  if (patch.standbyTimeoutSeconds !== undefined && normalizeScreenStandbyTimeout(patch.standbyTimeoutSeconds) !== patch.standbyTimeoutSeconds) throw new Error('Invalid standby timeout');
  if (patch.featuresOrder !== undefined && !Array.isArray(patch.featuresOrder)) throw new Error('Invalid featuresOrder');
  const features = record(patch.features);
  for (const key of Object.keys(SCREEN_FEATURE_IDS)) if (key in features && typeof features[key] !== 'boolean') throw new Error('Invalid screen feature');
  const legacyFeatures = legacyLedKeys.some(key => key in features) && !('ledSetting' in features);
  const merged = { ...current, ...patch, features: { ...current.features, ...features } } as Record<string, unknown>;
  if (legacyFeatures) delete (merged.features as Record<string, unknown>).ledSetting;
  if (importing && patch.standbyEnabled === undefined && patch.standbyDisplay !== undefined) delete merged.standbyEnabled;
  return normalizeScreenControl(merged);
}
