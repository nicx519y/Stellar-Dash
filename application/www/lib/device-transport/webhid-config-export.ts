import { DeviceTransportError } from './types';

const MAX_PROFILE_COUNT = 16;
const MAX_MACRO_COUNT = 5;
const MAX_MACRO_TRIGGER_KEYS = 4;
const MAX_MACRO_STEPS = 32;

export interface WebHidExportSection {
  section: 'global' | 'hotkeys' | 'screenControl' | 'profile' | 'end';
  data?: unknown;
}

type Requester = (
  command: string,
  params?: Record<string, unknown>,
) => Promise<Record<string, unknown> | undefined>;

/**
 * Replaces the legacy multi-response export_all_config handler with bounded,
 * sequential RPCs. Each result is emitted with the legacy section semantics so
 * the existing download UI and import format stay compatible.
 */
export async function exportWebHidConfigSections(
  request: Requester,
  emit: (section: WebHidExportSection) => void,
): Promise<void> {
  const global = await request('get_global_config');
  const globalConfig = asRecord(global?.globalConfig);
  if (!globalConfig) {
    throw new DeviceTransportError('protocol', 'get_global_config response is malformed');
  }
  emit({ section: 'global', data: globalConfig });

  const hotkeys = await request('get_hotkeys_config');
  if (!Array.isArray(hotkeys?.hotkeysConfig)) {
    throw new DeviceTransportError('protocol', 'get_hotkeys_config response is malformed');
  }
  emit({ section: 'hotkeys', data: hotkeys.hotkeysConfig });

  const screen = await request('get_screen_control_config');
  const screenControl = asRecord(screen?.screenControl);
  if (!screenControl) {
    throw new DeviceTransportError(
      'protocol',
      'get_screen_control_config response is malformed',
    );
  }
  emit({ section: 'screenControl', data: screenControl });

  const listResponse = await request('get_profile_list');
  const profileList = asRecord(listResponse?.profileList);
  const items = profileList?.items;
  if (!Array.isArray(items) || items.length > MAX_PROFILE_COUNT) {
    throw new DeviceTransportError(
      'protocol',
      'get_profile_list did not return profile IDs',
    );
  }

  const seenProfileIds = new Set<string>();
  for (const item of items) {
    const profileId = asRecord(item)?.id;
    if (typeof profileId !== 'string' || profileId.length === 0) {
      throw new DeviceTransportError(
        'protocol',
        'get_profile_list contains an invalid profile ID',
      );
    }
    if (seenProfileIds.has(profileId)) {
      throw new DeviceTransportError(
        'protocol',
        'get_profile_list contains duplicate profile IDs',
      );
    }
    seenProfileIds.add(profileId);
    const detailsResponse = await request('get_profile_details', { profileId });
    const profileDetails = asRecord(detailsResponse?.profileDetails);
    if (!profileDetails || profileDetails.id !== profileId) {
      throw new DeviceTransportError(
        'protocol',
        'get_profile_details returned the wrong profile',
      );
    }
    const macrosResponse = await request('get_profile_macros', { pid: profileId });
    const macros = compactMacrosToLegacy(macrosResponse?.m);
    const keysConfig = asRecord(profileDetails.keysConfig) ?? {};
    emit({
      section: 'profile',
      data: {
        ...profileDetails,
        keysConfig: {
          ...keysConfig,
          macros,
        },
      },
    });
  }
  emit({ section: 'end' });
}

export function compactMacrosToLegacy(value: unknown): Array<{
  index: number;
  triggerKeys: number[];
  steps: Array<{
    timeMs: number;
    buttonMask: number;
    dynamicMask: number;
  }>;
}> {
  if (!Array.isArray(value) || value.length !== MAX_MACRO_COUNT) {
    throw new DeviceTransportError(
      'protocol',
      'get_profile_macros did not return a compact macro array',
    );
  }
  const result = [];
  for (let index = 0; index < value.length; index += 1) {
    if (value[index] === null) continue;
    const macro = asRecord(value[index]);
    if (
      !macro ||
      !Array.isArray(macro.k) ||
      macro.k.length > MAX_MACRO_TRIGGER_KEYS ||
      !Array.isArray(macro.s) ||
      macro.s.length > MAX_MACRO_STEPS
    ) {
      throw new DeviceTransportError(
        'protocol',
        `Profile macro ${index} is malformed`,
      );
    }
    const triggerKeys = macro.k.map(toU8);
    const steps = macro.s.map((rawStep, stepIndex) => {
      if (!Array.isArray(rawStep) || rawStep.length < 2) {
        throw new DeviceTransportError(
          'protocol',
          `Profile macro ${index} step ${stepIndex} is malformed`,
        );
      }
      return {
        timeMs: toU16(rawStep[0]),
        buttonMask: toU32(rawStep[1]),
        dynamicMask: toU32(rawStep[2] ?? 0),
      };
    });
    result.push({ index, triggerKeys, steps });
  }
  return result;
}

function asRecord(value: unknown): Record<string, unknown> | null {
  return value !== null && typeof value === 'object' && !Array.isArray(value)
    ? value as Record<string, unknown>
    : null;
}

function toU8(value: unknown): number {
  const number = finiteInteger(value);
  if (number < 0 || number > 0xff) {
    throw new DeviceTransportError('protocol', 'Macro trigger key is not a u8');
  }
  return number;
}

function toU16(value: unknown): number {
  const number = finiteInteger(value);
  if (number < 0 || number > 0xffff) {
    throw new DeviceTransportError('protocol', 'Macro time is not a u16');
  }
  return number;
}

function toU32(value: unknown): number {
  const number = finiteInteger(value);
  if (number < 0 || number > 0xffffffff) {
    throw new DeviceTransportError('protocol', 'Macro mask is not a u32');
  }
  return number >>> 0;
}

function finiteInteger(value: unknown): number {
  if (typeof value !== 'number' || !Number.isInteger(value)) {
    throw new DeviceTransportError('protocol', 'Macro field is not an integer');
  }
  return value;
}
