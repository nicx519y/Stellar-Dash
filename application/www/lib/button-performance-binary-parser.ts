/**
 * 按键性能监控二进制数据解析器
 * 对应C++端的ButtonPerformanceMonitoringBinaryData和ButtonPerformanceData结构体
 */

// 单个按键性能监控数据（与C++端保持一致）
export interface ButtonPerformanceData {
  buttonIndex: number;    // 按键索引
  virtualPin: number;     // 虚拟引脚
  isPressed: boolean;     // 是否按下
  currentDistance: number; // 当前物理行程（mm）
  pressTriggerDistance: number; // 按下触发时的物理行程（mm）
  pressStartDistance: number; // 按下开始值对应的物理行程（mm）
  releaseTriggerDistance: number; // 释放触发时的物理行程（mm）
  releaseStartDistance: number; // 释放开始值对应的物理行程（mm）
}

// 按键性能监控推送的二进制数据结构（与C++端保持一致）
export interface ButtonPerformanceMonitoringBinaryData {
  command: number;        // 命令号：2 表示按键性能监控
  isActive: boolean;      // 布尔值：true=活跃，false=非活跃
  buttonCount: number;    // 本次推送的按键数量
  timestamp: number;      // 时间戳
  maxTravelDistance: number; // 最大物理行程（mm），从当前映射获取
  buttonData: ButtonPerformanceData[]; // 按键数据数组
}

// 命令号定义
export const BUTTON_PERFORMANCE_MONITORING_CMD = 2;

/**
 * 解析按键性能监控二进制数据
 * @param buffer ArrayBuffer 或 Uint8Array
 * @returns 解析后的按键性能监控数据，如果解析失败返回null
 */
export function parseButtonPerformanceMonitoringBinaryData(buffer: ArrayBuffer | Uint8Array): ButtonPerformanceMonitoringBinaryData | null {
  try {
    // 确保是ArrayBuffer
    const arrayBuffer = buffer instanceof ArrayBuffer ? buffer : buffer.buffer.slice(buffer.byteOffset, buffer.byteOffset + buffer.byteLength);
    
    const dataView = new DataView(arrayBuffer);
    
    // 检查数据长度，至少需要头部数据
    if (arrayBuffer.byteLength < 8) {
      console.warn('二进制数据长度不足，期望至少8字节，实际:', arrayBuffer.byteLength);
      return null;
    }
    
    // 解析头部数据
    // #pragma pack(push, 1) 确保紧密排列
    const command = dataView.getUint8(0);           // offset 0: uint8_t command
    const isActiveRaw = dataView.getUint8(1);       // offset 1: uint8_t isActive
    const buttonCount = dataView.getUint8(2);       // offset 2: uint8_t buttonCount
    // const reserved = dataView.getUint8(3);          // offset 3: uint8_t reserved
    const timestamp = dataView.getUint32(4, true);  // offset 4: uint32_t timestamp (小端序)
    const maxTravelDistance = dataView.getFloat32(8, true); // offset 8: float maxTravelDistance (小端序)
    
    // 转换isActive为布尔值
    const isActive = isActiveRaw !== 0;
    
    // 计算按键数据的大小
    // ButtonPerformanceData结构体使用 #pragma pack(push, 1) 紧密排列，无内存对齐
    // uint8_t buttonIndex(1) + uint8_t virtualPin(1) + uint8_t isPressed(1) + 
    // float currentDistance(4) + float pressTriggerDistance(4) + float pressStartDistance(4) + 
    // float releaseTriggerDistance(4) + float releaseStartDistance(4) + uint8_t reserved(1)
    // = 1+1+1+4+4+4+4+4+1 = 24字节
    const buttonDataSize = 24;
    const expectedTotalSize = 12 + buttonCount * buttonDataSize; // 8字节头部 + 4字节maxTravelDistance
    
    if (arrayBuffer.byteLength < expectedTotalSize) {
      console.warn(`二进制数据长度不足，期望${expectedTotalSize}字节，实际:`, arrayBuffer.byteLength);
      return null;
    }
    
    // 解析按键数据数组
    const buttonData: ButtonPerformanceData[] = [];
    let offset = 12; // 头部数据后的偏移（8字节头部 + 4字节maxTravelDistance）
    
    
    for (let i = 0; i < buttonCount; i++) {
      const buttonIndex = dataView.getUint8(offset + 0);     // offset + 0: uint8_t buttonIndex
      const virtualPin = dataView.getUint8(offset + 1);      // offset + 1: uint8_t virtualPin
      const isPressedRaw = dataView.getUint8(offset + 2);    // offset + 2: uint8_t isPressed
      const currentDistance = dataView.getFloat32(offset + 3, true); // offset + 3: float currentDistance (小端序)
      const pressTriggerDistance = dataView.getFloat32(offset + 7, true); // offset + 7: float pressTriggerDistance (小端序)
      const pressStartDistance = dataView.getFloat32(offset + 11, true); // offset + 11: float pressStartDistance (小端序)
      const releaseTriggerDistance = dataView.getFloat32(offset + 15, true); // offset + 15: float releaseTriggerDistance (小端序)
      const releaseStartDistance = dataView.getFloat32(offset + 19, true); // offset + 19: float releaseStartDistance (小端序)
      // const buttonReserved = dataView.getUint8(offset + 23); // offset + 23: uint8_t reserved
      
      // 转换isPressed为布尔值
      const isPressed = isPressedRaw !== 0;
      
      // if(buttonIndex == 0) {
      //   console.log('buttonIndex:', buttonIndex, 'virtualPin:', virtualPin, 'isPressed:', isPressed, 'currentDistance:', currentDistance, 'pressTriggerDistance:', pressTriggerDistance, 'pressStartDistance:', pressStartDistance, 'releaseTriggerDistance:', releaseTriggerDistance, 'releaseStartDistance:', releaseStartDistance);
      // }
      
      buttonData.push({
        buttonIndex,
        virtualPin,
        isPressed,
        currentDistance,
        pressTriggerDistance,
        pressStartDistance,
        releaseTriggerDistance,
        releaseStartDistance
      });
      
      offset += buttonDataSize;
    }
    
    const result: ButtonPerformanceMonitoringBinaryData = {
      command,
      isActive,
      buttonCount,
      timestamp,
      maxTravelDistance,
      buttonData
    };
    
    return result;
    
  } catch {
    console.error('解析按键性能监控二进制数据失败:');
    return null;
  }
}

/**
 * 检查二进制数据是否为按键性能监控数据
 * @param buffer ArrayBuffer 或 Uint8Array
 * @returns 是否为按键性能监控数据
 */
export function isButtonPerformanceMonitoringData(buffer: ArrayBuffer | Uint8Array): boolean {
  try {
    const arrayBuffer = buffer instanceof ArrayBuffer ? buffer : buffer.buffer.slice(buffer.byteOffset, buffer.byteOffset + buffer.byteLength);
    
    if (arrayBuffer.byteLength < 1) {
      return false;
    }
    
    const dataView = new DataView(arrayBuffer);
    const command = dataView.getUint8(0);
    
    return command === BUTTON_PERFORMANCE_MONITORING_CMD;
  } catch {
    return false;
  }
} 