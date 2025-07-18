/**
 * 按键状态二进制数据解析器
 * 对应C++端的ButtonStateBinaryData结构体
 */

// 按键状态二进制数据结构（与C++端保持一致）
export interface ButtonStateBinaryData {
  command: number;        // 命令号：1 表示按键状态变化
  isActive: boolean;      // 布尔值：true=活跃，false=非活跃
  triggerMask: number;    // 32位按键触发掩码
  totalButtons: number;   // 总按键数量
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
    
    // 检查数据长度，ButtonStateBinaryData结构体大小为8字节
    if (arrayBuffer.byteLength < 8) {
      console.warn('二进制数据长度不足，期望至少8字节，实际:', arrayBuffer.byteLength);
      return null;
    }
    
    const dataView = new DataView(arrayBuffer);
    
    // 按照C++结构体的内存布局解析
    // #pragma pack(push, 1) 确保紧密排列
    const command = dataView.getUint8(0);           // offset 0: uint8_t command
    const isActiveRaw = dataView.getUint8(1);       // offset 1: uint8_t isActive
    const triggerMask = dataView.getUint32(2, true); // offset 2: uint32_t triggerMask (小端序)
    const totalButtons = dataView.getUint8(6);      // offset 6: uint8_t totalButtons
    // offset 7-8: uint8_t reserved[2] - 保留字节，不解析
    
    // 转换isActive为布尔值
    const isActive = isActiveRaw !== 0;
    
    const result: ButtonStateBinaryData = {
      command,
      isActive,
      triggerMask,
      totalButtons
    };
    
    console.log('解析按键状态二进制数据:', result);
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