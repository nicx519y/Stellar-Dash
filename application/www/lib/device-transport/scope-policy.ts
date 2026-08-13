import { DeviceScope, DeviceTransportError } from './types';

const DEVICE_CONTROL_COMMANDS = new Set([
  'reboot',
  'start_manual_calibration',
  'stop_manual_calibration',
  'get_calibration_status',
  'check_is_manual_calibration_completed',
  'ms_mark_mapping_start',
  'ms_mark_mapping_stop',
  'ms_mark_mapping_step',
]);

const FIRMWARE_UPDATE_COMMANDS = new Set([
  'create_firmware_upgrade_session',
  'upload_firmware_chunk',
  'complete_firmware_upgrade_session',
  'abort_firmware_upgrade_session',
  'get_firmware_upgrade_status',
  'cleanup_firmware_upgrade_session',
  'ch585_update_status',
  'ch585_update_begin',
  'ch585_update_chunk',
  'ch585_update_complete',
]);

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
