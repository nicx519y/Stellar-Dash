/** 单帧 WebHID BUTTON_STATE 负载，与 common/webhid_protocol.h 对齐。 */

export const BUTTON_STATE_PAYLOAD_SIZE = 12;
export const BUTTON_STATE_ACTIVE_FLAG = 1 << 0;

// 按键状态二进制数据结构（与C++端保持一致）
export interface ButtonStateBinaryData {
  command: number;        // 命令号：1 表示按键状态变化
  isActive: boolean;      // 布尔值：true=活跃，false=非活跃
  triggerMask: number;    // 32位按键触发掩码
  totalButtons: number;   // 总按键数量
  eventSequence: number;  // 固件按键事件序号（独立于物理HID序号）
  droppedSnapshots: number; // 本会话累计丢弃/合并的状态快照
}

// 命令号定义
export const BUTTON_STATE_CHANGED_CMD = 1;

/**
 * 解析按键状态二进制数据
 * @param buffer ArrayBuffer 或 Uint8Array
 * @returns 解析后的按键状态数据，如果解析失败返回null
 */
export function parseButtonStateBinaryData(buffer: ArrayBuffer | Uint8Array): ButtonStateBinaryData | null {
  try {
    // 确保是ArrayBuffer
    const arrayBuffer = buffer instanceof ArrayBuffer ? buffer : buffer.buffer.slice(buffer.byteOffset, buffer.byteOffset + buffer.byteLength);
    
    if (arrayBuffer.byteLength !== BUTTON_STATE_PAYLOAD_SIZE) {
      return null;
    }
    
    const dataView = new DataView(arrayBuffer);
    
    const eventSequence = dataView.getUint32(0, true);
    const triggerMask = dataView.getUint32(4, true);
    const droppedSnapshots = dataView.getUint16(8, true);
    const totalButtons = dataView.getUint8(10);
    const flags = dataView.getUint8(11);
    if (
      eventSequence === 0
      || totalButtons > 32
      || (flags & ~BUTTON_STATE_ACTIVE_FLAG) !== 0
    ) {
      return null;
    }
    
    const result: ButtonStateBinaryData = {
      command: BUTTON_STATE_CHANGED_CMD,
      isActive: (flags & BUTTON_STATE_ACTIVE_FLAG) !== 0,
      triggerMask,
      totalButtons,
      eventSequence,
      droppedSnapshots,
    };
    
    return result;
    
  } catch (error) {
    console.error('解析按键状态二进制数据失败:', error);
    return null;
  }
}

/**
 * 将按键触发掩码转换为按键数组
 * @param triggerMask 32位触发掩码
 * @param totalButtons 总按键数量
 * @returns 触发的按键索引数组
 */
export function triggerMaskToButtonArray(triggerMask: number, totalButtons: number): number[] {
  const triggeredButtons: number[] = [];
  
  for (let i = 0; i < Math.min(totalButtons, 32); i++) {
    if ((triggerMask & (1 << i)) !== 0) {
      triggeredButtons.push(i);
    }
  }
  
  return triggeredButtons;
}

/**
 * 将按键触发掩码转换为二进制字符串（用于调试显示）
 * @param triggerMask 32位触发掩码
 * @returns 二进制字符串表示
 */
export function triggerMaskToBinaryString(triggerMask: number): string {
  return (triggerMask >>> 0).toString(2).padStart(32, '0');
}

/**
 * 检查特定按键是否被触发
 * @param triggerMask 32位触发掩码
 * @param buttonIndex 按键索引（0-31）
 * @returns 该按键是否被触发
 */
export function isButtonTriggered(triggerMask: number, buttonIndex: number): boolean {
  if (buttonIndex < 0 || buttonIndex >= 32) {
    return false;
  }
  return (triggerMask & (1 << buttonIndex)) !== 0;
}
