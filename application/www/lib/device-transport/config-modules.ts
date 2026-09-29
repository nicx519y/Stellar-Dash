import { DEFAULT_SCREEN_CONTROL_CONFIG, type GameProfile, type GameProfileList } from '../../types/gamepad-config';
import { profileSlots } from '../profile-slots';
import { compactMacrosToLegacy } from './webhid-config-export';

export const CONFIG_SCHEMA_VERSION = 1;
export const CONFIG_CACHE_FORMAT = 1;
export const VERSIONED_CONFIG_COMMANDS = new Set([
  'get_global_config', 'update_global_config', 'get_screen_control_config', 'update_screen_control_config',
  'get_hotkeys_config', 'update_hotkeys_config', 'get_profile_list', 'get_default_profile',
  'get_profile_details', 'update_profile', 'get_profile_macros', 'update_profile_macros', 'switch_default_profile',
]);
export const isRecord = (value: unknown): value is Record<string, unknown> =>
  value !== null && typeof value === 'object' && !Array.isArray(value);
export const isDigest = (value: unknown): value is string => typeof value === 'string' && /^[a-f0-9]{64}$/.test(value);
export interface ConfigManifest {
  deviceCacheKey: string;
  hardwareVersion: string;
  schemaVersion: number;
  modules: Record<string, string>;
}
export function parseConfigManifest(value: unknown): ConfigManifest {
  if (!isRecord(value) || !isDigest(value.deviceCacheKey) ||
      typeof value.hardwareVersion !== 'string' || !value.hardwareVersion ||
      value.schemaVersion !== CONFIG_SCHEMA_VERSION || !isRecord(value.modules)) throw new Error('Invalid configuration manifest');
  const keys = Object.keys(value.modules);
  if (keys.length < 4 || keys.length > 132 || keys.some(key =>
    !/^(global|screen-control|hotkeys|profile-list|(?:profile|macros):[A-Za-z0-9_-]{1,64})$/.test(key) || !isDigest((value.modules as Record<string, unknown>)[key]))) {
    throw new Error('Invalid configuration module versions');
  }
  for (const key of ['global', 'screen-control', 'hotkeys', 'profile-list']) {
    if (!isDigest(value.modules[key])) throw new Error(`Missing configuration version: ${key}`);
  }
  return value as unknown as ConfigManifest;
}
export function resourceRequest(key: string): { command: string; params: Record<string, unknown>; field: string } {
  const common: Record<string, [string, string]> = {
    global: ['get_global_config', 'globalConfig'], 'screen-control': ['get_screen_control_config', 'screenControl'],
    hotkeys: ['get_hotkeys_config', 'hotkeysConfig'], 'profile-list': ['get_profile_list', 'profileList'],
  };
  if (Object.hasOwn(common, key)) return { command: common[key][0], field: common[key][1], params: key === 'profile-list' ? { listOnly: true } : {} };
  if (key.startsWith('profile:')) return { command: 'get_profile_details', field: 'profileDetails', params: { profileId: key.slice(8) } };
  if (key.startsWith('macros:')) return { command: 'get_profile_macros', field: 'm', params: { pid: key.slice(7) } };
  throw new Error(`Unknown configuration module: ${key}`);
}
export function decodeConfigResource(key: string, value: unknown, convert: (profile: GameProfile) => GameProfile | null | undefined): unknown {
  if (key === 'profile-list') {
    const list = value as GameProfileList;
    if (!isRecord(value) || !Array.isArray(list.items) || !profileSlots(list).compatible || !list.items.some(p => p.id === list.defaultId)) throw new Error('Invalid device profile slots');
    return list;
  }
  if (key === 'global') {
    if (!isRecord(value) || !value.inputMode) throw new Error('Invalid global configuration');
    return value;
  }
  if (key === 'screen-control') {
    if (!isRecord(value) || !['light', 'dark'].includes(value.screenStyle as string)) throw new Error('Invalid screen configuration');
    const features = isRecord(value.features) ? value.features : {};
    return { ...DEFAULT_SCREEN_CONTROL_CONFIG, ...value,
      features: { ...DEFAULT_SCREEN_CONTROL_CONFIG.features, ...features },
      featuresOrder: [...new Set([...(Array.isArray(value.featuresOrder) ? value.featuresOrder : []), ...DEFAULT_SCREEN_CONTROL_CONFIG.featuresOrder])]
        .filter(key => key in DEFAULT_SCREEN_CONTROL_CONFIG.features) };
  }
  if (key === 'hotkeys') {
    if (!Array.isArray(value)) throw new Error('Invalid hotkeys configuration');
    return value;
  }
  if (key.startsWith('macros:')) return compactMacrosToLegacy(value);
  if (!isRecord(value) || value.id !== key.slice(8) || !value.keysConfig || !value.ledsConfigs || !value.triggerConfigs) throw new Error(`Incomplete profile: ${key}`);
  const profile = convert(value as unknown as GameProfile);
  if (!profile) throw new Error(`Invalid profile: ${key}`);
  return { ...profile, keysConfig: { ...profile.keysConfig, macros: undefined } };
}
export function resourceBodies(data: Record<string, unknown>, params: Record<string, unknown>): Record<string, unknown> {
  const result: Record<string, unknown> = {};
  for (const [field, key] of Object.entries({ globalConfig: 'global', screenControl: 'screen-control', hotkeysConfig: 'hotkeys', profileList: 'profile-list' })) {
    if (data[field] !== undefined) result[key] = data[field];
  }
  for (const field of ['defaultProfileDetails', 'profileDetails']) {
    const profile = data[field];
    if (isRecord(profile) && typeof profile.id === 'string') result[`profile:${profile.id}`] = profile;
  }
  if (data.m !== undefined && typeof params.pid === 'string') result[`macros:${params.pid}`] = data.m;
  return result;
}
export async function contentChecksum(value: unknown): Promise<string> {
  const bytes = new TextEncoder().encode(JSON.stringify(value));
  const digest = await globalThis.crypto.subtle.digest('SHA-256', bytes);
  return Array.from(new Uint8Array(digest), byte => byte.toString(16).padStart(2, '0')).join('');
}
