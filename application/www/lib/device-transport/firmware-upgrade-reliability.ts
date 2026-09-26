import type { DeviceFirmwareChunkResult } from './device-feature-types';

export type FirmwareChunkFailureKind = 'rejected' | 'ambiguous';

/**
 * The current firmware status RPC reports only aggregate progress, so it
 * cannot prove whether one particular chunk reached the device. Never resend
 * a chunk after the transport has started the attempt.
 */
export class FirmwareUpgradeRestartRequiredError extends Error {
  constructor(
    public readonly failureKind: FirmwareChunkFailureKind,
    message: string,
    public readonly cause?: unknown,
  ) {
    super(message);
    this.name = 'FirmwareUpgradeRestartRequiredError';
  }
}

export class FirmwareFinalizationUncertainError extends Error {
  constructor(public readonly cause?: unknown) {
    super(
      'Firmware finalization result is uncertain. Reconnect and verify firmware metadata before starting another upgrade.',
    );
    this.name = 'FirmwareFinalizationUncertainError';
  }
}

export async function sendFirmwareChunkWithoutAmbiguousRetry(
  send: () => Promise<DeviceFirmwareChunkResult>,
): Promise<DeviceFirmwareChunkResult> {
  try {
    const result = await send();
    if (!result.success) {
      throw new FirmwareUpgradeRestartRequiredError(
        'rejected',
        `Device rejected the firmware chunk: ${result.error || 'Unknown error'}. Restart the upgrade session.`,
      );
    }
    return result;
  } catch (error) {
    if (error instanceof FirmwareUpgradeRestartRequiredError) {
      throw error;
    }
    throw new FirmwareUpgradeRestartRequiredError(
      'ambiguous',
      'Firmware chunk delivery result is uncertain. Do not resend this chunk; restart the upgrade session.',
      error,
    );
  }
}

export async function abortFirmwareSessionIfSafe(options: {
  sessionId: string | null;
  completionIssued: boolean;
  completionSettled: boolean;
  abortSession: (sessionId: string) => Promise<void>;
}): Promise<boolean> {
  if (
    !options.sessionId ||
    (options.completionIssued && !options.completionSettled)
  ) {
    return false;
  }
  await options.abortSession(options.sessionId);
  return true;
}
