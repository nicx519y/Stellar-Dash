import { DEFAULT_SCREEN_CONTROL_CONFIG, normalizeScreenStandbyTimeout, type GameProfile, type GameProfileList, type MacroConfig, type GlobalConfig, type ScreenControlConfig, type Hotkey } from '../../types/gamepad-config';
import { compactMacrosToLegacy } from './webhid-config-export';
import { profileSlots } from '../profile-slots';
import type { ConfigResources } from '../session-config-store';

interface ConfigResponse extends Record<string, unknown> {
  profileList?: GameProfileList;
  profileDetails?: GameProfile;
  defaultProfileDetails?: GameProfile;
  globalConfig?: GlobalConfig;
  screenControl?: ScreenControlConfig;
  hotkeysConfig?: Hotkey[];
  m?: unknown;
}
export type ConfigRequester = (command: string, params?: Record<string, unknown>) => Promise<ConfigResponse | undefined>;
export async function readConfigSnapshot(
  request: ConfigRequester,
  convert: (value: GameProfile) => GameProfile | null | undefined,
  progress: (completed: number, total: number) => void = () => {},
): Promise<ConfigResources> {
  const resources: ConfigResources = {};
  let completed = 0;
  progress(0, 0);
  // The profile list determines the denominator. Do not publish a temporary
  // 1/4 value that makes the visible percentage fall when the slots arrive.
  const list = (await request('get_profile_list'))?.profileList as GameProfileList;
  if (!list || !profileSlots(list).compatible || !list.items.some(p => p.id === list.defaultId)) throw new Error('Invalid device profile slots');
  const total = 4 + list.items.length * 2;
  progress(++completed, total);
  const read: ConfigRequester = async (command, params) => {
    const value = await request(command, params);
    progress(++completed, total);
    return value;
  };
  resources['profile-list'] = list;
  resources['selected-profile'] = list.defaultId;
  const global = (await read('get_global_config'))?.globalConfig;
  if (!global || typeof global !== 'object' || !global.inputMode) throw new Error('Invalid global configuration');
  resources.global = global;
  const screen = (await read('get_screen_control_config'))?.screenControl;
  if (!screen || typeof screen !== 'object' || !['light', 'dark'].includes(screen.screenStyle)) throw new Error('Invalid screen configuration');
  resources['screen-control'] = {
    ...DEFAULT_SCREEN_CONTROL_CONFIG, ...screen,
    standbyTimeoutSeconds: normalizeScreenStandbyTimeout(screen.standbyTimeoutSeconds),
    features: { ...DEFAULT_SCREEN_CONTROL_CONFIG.features, ...screen.features },
    featuresOrder: [...new Set([...(Array.isArray(screen.featuresOrder) ? screen.featuresOrder : []), ...DEFAULT_SCREEN_CONTROL_CONFIG.featuresOrder])]
      .filter(key => key in DEFAULT_SCREEN_CONTROL_CONFIG.features),
  };
  const hotkeys = (await read('get_hotkeys_config'))?.hotkeysConfig;
  if (!Array.isArray(hotkeys)) throw new Error('Invalid hotkeys configuration');
  resources.hotkeys = hotkeys;
  for (const item of list.items) {
    const details = (await read('get_profile_details', { profileId: item.id }))?.profileDetails;
    if (!details || details.id !== item.id || !details.keysConfig || !details.ledsConfigs || !details.triggerConfigs) throw new Error(`Incomplete profile: ${item.id}`);
    const profile = convert(details);
    if (!profile) throw new Error(`Invalid profile: ${item.id}`);
    resources[`profile:${item.id}`] = { ...profile, keysConfig: { ...profile.keysConfig, macros: undefined } };
    resources[`macros:${item.id}`] = compactMacrosToLegacy((await read('get_profile_macros', { pid: item.id }))?.m);
  }
  return resources;
}
export function compactMacros(macros: MacroConfig[]): unknown[] {
  const byIndex = new Map(macros.map(m => [m.index, m]));
  return Array.from({ length: 5 }, (_, i) => {
    const macro = byIndex.get(i);
    if (!macro || (!macro.triggerKeys.length && !macro.steps.length)) return null;
    return { k: macro.triggerKeys, s: macro.steps.map(s => [s.timeMs, s.buttonMask >>> 0, (s.dynamicMask ?? 0) >>> 0]) };
  });
}

export async function writeConfigResource(request: ConfigRequester, key: string, value: unknown, convert: (value: GameProfile) => GameProfile): Promise<unknown> {
  let remote: unknown;
  if (key === 'global') remote = (await request('update_global_config', { globalConfig: value }))?.globalConfig;
  else if (key === 'screen-control') remote = (await request('update_screen_control_config', { screenControl: value }))?.screenControl;
  else if (key === 'hotkeys') remote = (await request('update_hotkeys_config', { hotkeysConfig: value }))?.hotkeysConfig;
  else if (key.startsWith('profile:')) {
    const id = key.slice(8);
    const response = await request('update_profile', { profileId: id, profileDetails: value });
    let details = response?.profileDetails ?? response?.defaultProfileDetails;
    if (details?.id !== id) details = (await request('get_profile_details', { profileId: id }))?.profileDetails;
    if (details?.id !== id) throw new Error('Device confirmed the wrong profile');
    const profile = convert(details);
    remote = { ...profile, keysConfig: { ...profile.keysConfig, macros: undefined } };
  } else if (key.startsWith('macros:')) {
    remote = compactMacrosToLegacy((await request('update_profile_macros', { pid: key.slice(7), m: compactMacros(value as MacroConfig[]) }))?.m);
  } else if (key === 'selected-profile') {
    remote = (await request('switch_default_profile', { profileId: value }))?.profileList?.defaultId;
    if (remote !== value) throw new Error('Device did not confirm the selected profile');
  }
  if (remote === undefined || remote === null) throw new Error('Device returned an incomplete configuration acknowledgement');
  return remote;
}
