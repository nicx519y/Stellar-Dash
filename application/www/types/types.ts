
// 固件元数据类型定义
export interface FirmwareComponent {
    name: string;
    file: string;
    address: string;
    size: number;
    sha256: string;
    status: 'active' | 'inactive' | 'corrupted';
}

export interface FirmwareMetadata {
    version: string;
    slot: string;
    build_date: string;
    components: FirmwareComponent[];
}

export interface DeviceFirmwareInfo {
    device_id: string;
    firmware: FirmwareMetadata;
}


// 校准状态类型定义
export interface CalibrationButtonStatus {
    index: number;
    phase: 'IDLE' | 'TOP_SAMPLING' | 'BOTTOM_SAMPLING' | 'COMPLETED' | 'ERROR';
    isCalibrated: boolean;
    topValue: number;
    bottomValue: number;
    ledColor: 'OFF' | 'RED' | 'CYAN' | 'DARK_BLUE' | 'GREEN' | 'YELLOW';
}

export interface CalibrationStatus {
    isActive: boolean;
    uncalibratedCount: number;
    activeCalibrationCount: number;
    allCalibrated: boolean;
    buttons: CalibrationButtonStatus[];
}

// 按键状态类型定义
export interface ButtonStates {
    triggerMask: number;
    triggerBinary: string;
    totalButtons: number;
    timestamp: number;
}


export interface LEDsConfig {
    ledEnabled: boolean;
    ledsEffectStyle: number;
    ledColors: string[];
    ledBrightness: number;
    ledAnimationSpeed: number;
}