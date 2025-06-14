// 按键监控状态管理
interface ButtonMonitoringState {
    isActive: boolean;
    triggerMask: number;
    totalButtons: number;
    lastUpdateTime: number;
}

// 全局状态
const buttonState: ButtonMonitoringState = {
    isActive: false,
    triggerMask: 0,
    totalButtons: 8, // 假设8个按键
    lastUpdateTime: Date.now(),
};

let _simulationCounter = 0;

// 生成模拟的按键触发数据
function generateSimulatedButtonStates(): number {
    _simulationCounter++;
    
    // // 每5次调用模拟一次按键触发
    // if (simulationCounter % 5 === 0) {
    //     // 随机生成1-3个按键的触发
    //     const numTriggers = Math.floor(Math.random() * 3) + 1;
    //     let triggerMask = 0;
        
    //     for (let i = 0; i < numTriggers; i++) {
    //         const buttonIndex = Math.floor(Math.random() * buttonState.totalButtons);
    //         triggerMask |= (1 << buttonIndex);
    //     }
        
    //     return triggerMask;
    // } else {
    //     // 大部分时间没有按键触发
    //     return 0;
    // }
    return 0;
}

// 将数字转换为二进制字符串
function toBinaryString(mask: number, totalButtons: number): string {
    let binaryStr = '';
    for (let i = totalButtons - 1; i >= 0; i--) {
        binaryStr += (mask & (1 << i)) ? '1' : '0';
    }
    return binaryStr;
}

// 开启按键监控
export function startButtonMonitoring() {
    buttonState.isActive = true;
    buttonState.lastUpdateTime = Date.now();
    return {
        message: "Button monitoring started successfully",
        status: "active",
        isActive: buttonState.isActive,
    };
}

// 关闭按键监控
export function stopButtonMonitoring() {
    buttonState.isActive = false;
    buttonState.triggerMask = 0;
    buttonState.lastUpdateTime = Date.now();
    return {
        message: "Button monitoring stopped successfully",
        status: "inactive",
        isActive: buttonState.isActive,
    };
}

// 获取按键状态
export function getButtonStates() {
    if (!buttonState.isActive) {
        return {
            triggerMask: 0,
            triggerBinary: toBinaryString(0, buttonState.totalButtons),
            totalButtons: buttonState.totalButtons,
            timestamp: Date.now(),
        };
    }
    
    // 生成新的触发状态
    const triggerMask = generateSimulatedButtonStates();
    const triggerBinary = toBinaryString(triggerMask, buttonState.totalButtons);
    const timestamp = Date.now();
    
    // 更新状态
    buttonState.triggerMask = triggerMask;
    buttonState.lastUpdateTime = timestamp;
    
    return {
        triggerMask,
        triggerBinary,
        totalButtons: buttonState.totalButtons,
        timestamp,
    };
}

// 获取按键监控状态
export function getButtonMonitoringStatus() {
    return {
        isActive: buttonState.isActive,
        lastUpdateTime: buttonState.lastUpdateTime,
    };
} 