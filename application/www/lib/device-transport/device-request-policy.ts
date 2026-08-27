type CommandParams = Record<string, unknown>;

export type DeviceCommandSchedule = {
  coalescingKey?: string;
  mergeParams?: (previous: CommandParams, next: CommandParams) => CommandParams;
  debounceMs: number;
  maxWaitMs: number;
  timeoutMs?: number;
};

const DURABLE_WRITE_DEBOUNCE_MS = 300;
const DURABLE_WRITE_MAX_WAIT_MS = 1_200;
const DURABLE_WRITE_TIMEOUT_MS = 30_000;
const PREVIEW_DEBOUNCE_MS = 40;
const PREVIEW_MAX_WAIT_MS = 160;

const durableCommands = new Set([
  'update_global_config',
  'update_hotkeys_config',
  'update_screen_control_config',
  'update_profile',
  'update_macro',
  'update_profile_macros',
]);

function profileId(params: CommandParams): string {
  const details = params.profileDetails;
  const detailId = details && typeof details === 'object' && !Array.isArray(details)
    ? (details as Record<string, unknown>).id
    : undefined;
  return typeof params.profileId === 'string'
    ? params.profileId
    : (typeof params.pid === 'string'
      ? params.pid
      : (typeof detailId === 'string' ? detailId : ''));
}

function durableKey(command: string, params: CommandParams): string {
  if (
    command === 'update_profile' ||
    command === 'update_profile_macros'
  ) {
    return `${command}:${profileId(params)}`;
  }
  if (command === 'update_macro') {
    const macro = params.macro;
    const index = macro && typeof macro === 'object' && !Array.isArray(macro)
      ? (macro as Record<string, unknown>).index
      : undefined;
    return `${command}:${profileId(params)}:${String(index ?? '')}`;
  }
  return command;
}

function mergeObjectField(
  field: string,
  previous: CommandParams,
  next: CommandParams,
): CommandParams {
  const previousValue = previous[field];
  const nextValue = next[field];
  if (
    !previousValue || typeof previousValue !== 'object' || Array.isArray(previousValue) ||
    !nextValue || typeof nextValue !== 'object' || Array.isArray(nextValue)
  ) {
    return next;
  }
  return {
    ...previous,
    ...next,
    [field]: {
      ...(previousValue as Record<string, unknown>),
      ...(nextValue as Record<string, unknown>),
    },
  };
}

function durableMerge(
  command: string,
): ((previous: CommandParams, next: CommandParams) => CommandParams) | undefined {
  if (command === 'update_global_config') {
    return (previous, next) => mergeObjectField('globalConfig', previous, next);
  }
  if (command === 'update_profile') {
    return (previous, next) => mergeObjectField('profileDetails', previous, next);
  }
  return undefined;
}

/**
 * Single source of truth for device-command coalescing and debounce.
 * Unlisted commands are still serialized, but are never delayed or merged.
 */
export function deviceCommandSchedule(
  command: string,
  params: CommandParams,
  immediate: boolean,
): DeviceCommandSchedule {
  if (durableCommands.has(command)) {
    return {
      coalescingKey: durableKey(command, params),
      mergeParams: durableMerge(command),
      debounceMs: immediate ? 0 : DURABLE_WRITE_DEBOUNCE_MS,
      maxWaitMs: immediate ? 0 : DURABLE_WRITE_MAX_WAIT_MS,
      timeoutMs: DURABLE_WRITE_TIMEOUT_MS,
    };
  }

  if (command === 'push_leds_config' || command === 'clear_leds_preview') {
    return {
      // Preview and clear mutate one ephemeral device resource. A clear can
      // therefore delete a queued stale preview instead of sending both.
      coalescingKey: 'led-preview',
      debounceMs: immediate ? 0 : PREVIEW_DEBOUNCE_MS,
      maxWaitMs: immediate ? 0 : PREVIEW_MAX_WAIT_MS,
    };
  }

  return { debounceMs: 0, maxWaitMs: 0 };
}
