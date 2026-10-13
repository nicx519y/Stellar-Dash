import { DeviceScope, DeviceTransportError } from './types';

import { DEVICE_CONTROL_COMMANDS as CONTROL_COMMANDS, FIRMWARE_UPDATE_COMMANDS as UPDATE_COMMANDS } from './generated-command-policy';

const DEVICE_CONTROL_COMMANDS = new Set<string>(CONTROL_COMMANDS);
const FIRMWARE_UPDATE_COMMANDS = new Set<string>(UPDATE_COMMANDS);

export function elevatedScopesForCommand(command: string): readonly DeviceScope[] {
  if (DEVICE_CONTROL_COMMANDS.has(command)) {
    return ['device.control'];
  }
  if (FIRMWARE_UPDATE_COMMANDS.has(command)) {
    return ['firmware.update'];
  }
  return [];
}

export function binaryOpcodeScope(opcode: number): DeviceScope {
  if (opcode === 0x01) {
    return 'firmware.update';
  }
  if (opcode >= 0x30 && opcode <= 0x33) {
    return 'asset.write';
  }
  if (opcode === 0x34 || opcode === 0x35) {
    return 'config.read';
  }
  throw new DeviceTransportError(
    'protocol',
    `Unsupported WebHID binary opcode 0x${opcode.toString(16).padStart(2, '0')}`,
  );
}
