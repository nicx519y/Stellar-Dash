// 固件元数据类型定义
export interface FirmwareComponent {
    name: string;
    file: string;
    active: boolean;
    address: number | string;
    size: number;
    sha256: string;
    data?: Uint8Array; // 组件数据
}

export interface FirmwareMetadata {
    version: string;
    currentSlot: string;
    targetSlot: string;
    buildDate: string;
    components: FirmwareComponent[];
}

export interface DeviceFirmwareInfo {
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

// 固件更新检查相关类型定义
export interface FirmwareUpdateCheckRequest {
    currentVersion: string;
}

export interface FirmwareUpdateInfo {
    id: string;
    name: string;
    version: string;
    desc: string;
    createTime: string;
}

export interface FirmwareUpdateCheckResponse {
    currentVersion: string;
    updateAvailable: boolean;
    updateCount: number;
    checkTime: string;
    latestVersion?: string;
    latestFirmware?: {
        id: string;
        name: string;
        version: string;
        desc: string;
        createTime: string;
        updateTime: string;
        slotA?: any;
        slotB?: any;
    };
    availableUpdates?: FirmwareUpdateInfo[];
}

// 固件升级包下载和传输相关类型定义
export interface FirmwarePackageDownloadProgress {
    stage: 'downloading' | 'extracting' | 'downcompleted' | 'uploading' | 'uploadcompleted' | 'failed';
    progress: number; // 0-100
    message: string;
    error?: string;
    current_component?: string;
    chunk_progress?: number;
    bytes_transferred?: number;
    total_bytes?: number;
}

export interface FirmwareManifest {
    version: string;
    slot: string;
    build_date: string;
    components: FirmwareComponent[];
}

export interface FirmwarePackage {
    manifest: FirmwareManifest;
    components: { [componentName: string]: FirmwareComponent };
    totalSize: number;
}

export interface FirmwareUpgradeConfig {
    chunkSize: number; // 分片大小，必须是4KB(4096字节)的倍数，建议范围: 4KB-16KB
    maxRetries: number; // 最大重试次数
    timeout: number; // 超时时间(ms)
}

export interface FirmwareUpgradeSession {
    sessionId: string;
    status: 'uploading' | 'completed' | 'failed';
    progress: number; // 0-100
    currentComponent: string | null;
    error: string | null;
}

export interface ChunkTransferRequest {
    session_id: string;
    component_name: string;
    chunk_index: number;
    total_chunks: number;
    target_address: string;        // 精确的写入地址（十六进制字符串）
    chunk_size: number;           // 当前chunk的实际大小（字节）
    chunk_offset: number;         // 在组件内的偏移量（字节）
    data: string;                 // Base64编码的数据
    checksum: string;             // SHA256校验和
}

export interface ChunkTransferResponse {
    errNo: number;
    errorMessage?: string;
    progress?: {
        received_size: number;
        total_size: number;
    };
}