// 固件元数据相关的类型定义

// Manifest.json中的组件定义
export interface FirmwareComponent {
    name: string;
    file: string;
    address: string;
    size: number;
    sha256: string;
    status?: 'active' | 'inactive' | 'corrupted';
}

// 发版包元数据 (对应manifest.json结构)
export interface ReleasePackageManifest {
    version: string;
    slot: 'A' | 'B';
    build_date: string;
    components: FirmwareComponent[];
}

export interface FirmwareVersion {
    major: number;
    minor: number;
    patch: number;
    prerelease?: string | null; // alpha, beta, rc1 等
    build: string; // 构建号
    tag: string; // Git标签
    commit: string; // Git提交哈希
}

export interface FirmwareBuild {
    date: string; // ISO 8601格式的构建时间
    compiler: string; // 编译器名称
    compilerVersion: string; // 编译器版本
    optimizationLevel: string; // 优化级别: O0, O1, O2, O3, Os, Oz
    debugInfo: boolean; // 是否包含调试信息
    buildType: 'debug' | 'release'; // 构建类型
    buildMachine: string; // 构建机器
    builder: string; // 构建者
}

export interface FirmwareHardware {
    platform: string; // 目标平台
    architecture: string; // 架构
    frequency: number; // CPU频率 (Hz)
    flashSize: number; // Flash大小 (bytes)
    ramSize: number; // RAM大小 (bytes)
    vendor: string; // 厂商
    board: string; // 开发板型号
}

// 槽位组件信息
export interface SlotComponent {
    address: string;
    size: number;
    lastUpdated: string | null;
}

// 槽位信息
export interface SlotInfo {
    version: string | null;
    status: 'active' | 'inactive' | 'corrupted' | 'empty';
    baseAddress: string;
    components: {
        application: SlotComponent;
        webresources: SlotComponent;
        adc_mapping: SlotComponent;
    };
}

// 完整槽位信息
export interface FirmwareSlotInfo {
    current: 'A' | 'B'; // 当前运行槽位
    available: Array<'A' | 'B'>; // 可用槽位
    slotA: SlotInfo;
    slotB: SlotInfo;
}

export interface USBFeature {
    enabled: boolean;
    version: string;
    classes: string[]; // 支持的USB类
}

export interface WebInterfaceFeature {
    enabled: boolean;
    version: string;
}

export interface GamepadFeature {
    enabled: boolean;
    maxButtons: number;
    analogInputs: number;
}

export interface LEDFeature {
    enabled: boolean;
    maxLeds: number;
    effects: string[];
}

export interface CalibrationFeature {
    enabled: boolean;
    autoCalibration: boolean;
    manualCalibration: boolean;
}

export interface DualSlotFeature {
    enabled: boolean;
    crossSlotFlashing: boolean; // 支持跨槽刷写
    hotSwap: boolean; // 热切换
}

export interface FirmwareFeatures {
    usb: USBFeature;
    webInterface: WebInterfaceFeature;
    gamepadSupport: GamepadFeature;
    ledSupport: LEDFeature;
    calibration: CalibrationFeature;
    dualSlot: DualSlotFeature;
}

// 内存组件信息
export interface MemoryComponent {
    name: string;
    offset: string;
    size: number;
}

export interface MemorySlot {
    name: string;
    startAddress: string;
    size: number;
    type: 'application' | 'bootloader' | 'config' | 'external_flash';
    components?: MemoryComponent[];
}

export interface MemoryRegion {
    startAddress: string;
    size: number;
    type: string;
}

export interface FirmwareMemory {
    slots: MemorySlot[];
    bootloader: MemoryRegion;
    config: MemoryRegion;
}

// 发版包兼容性信息
export interface PackageCompatibility {
    manifestVersion: string; // manifest.json版本
    supportedComponents: string[]; // 支持的组件类型
    requiredComponents: string[]; // 必需组件
    optionalComponents: string[]; // 可选组件
    maxPackageSize: number; // 最大发版包大小
    supportedFormats: string[]; // 支持的包格式
}

export interface Dependency {
    name: string;
    version: string;
}

export interface FirmwareDependencies {
    hal: Dependency;
    tinyusb: Dependency;
    fatfs: Dependency;
    [key: string]: Dependency;
}

export interface FirmwareCompatibility {
    minBootloaderVersion: string;
    configVersion: string;
    apiVersion: string;
    manifestVersion: string;
}

export interface FirmwareIntegrity {
    packageCrc32: string; // 整个发版包的CRC32
    manifestSha256: string; // manifest.json的SHA256
    signed: boolean; // 是否数字签名
    signature: string | null; // 数字签名内容
    componentChecksums: Record<string, string>; // 各组件的校验和
}

// 完整的固件元数据结构
export interface FirmwareMetadata {
    // 基本信息 (来自manifest.json)
    version: string;
    slot: 'A' | 'B';
    build_date: string;
    components: FirmwareComponent[];
    
    // 扩展信息
    versionInfo: FirmwareVersion;
    build: FirmwareBuild;
    hardware: FirmwareHardware;
    slotInfo: FirmwareSlotInfo;
    features: FirmwareFeatures;
    memory: FirmwareMemory;
    packageCompatibility: PackageCompatibility;
    dependencies: FirmwareDependencies;
    compatibility: FirmwareCompatibility;
    integrity: FirmwareIntegrity;
}

// API响应类型
export interface FirmwareMetadataResponse {
    errNo: number;
    data?: {
        firmware: FirmwareMetadata;
    };
    errorMessage?: string;
}

// 版本比较结果
export interface VersionComparisonResult {
    isNewer: boolean;
    isDifferent: boolean;
    majorDiff: number;
    minorDiff: number;
    patchDiff: number;
}

// 固件更新状态
export type FirmwareUpdateStatus = 
    | 'up-to-date'
    | 'update-available'
    | 'updating'
    | 'update-failed'
    | 'update-success';

// 固件更新包信息
export interface FirmwareUpdatePackage {
    version: string;
    releaseDate: string;
    fileSize: number;
    downloadUrl: string;
    checksums: {
        md5: string;
        sha256: string;
    };
    changeLog: string[];
    compatibility: {
        minBootloaderVersion: string;
        maxBootloaderVersion: string;
    };
    manifest?: ReleasePackageManifest; // 包含的manifest信息
}

// 固件更新信息
export interface FirmwareUpdateInfo {
    hasUpdate: boolean;
    currentVersion: string;
    latestVersion: string;
    updateRequired: boolean; // 是否强制更新
    updatePackage: FirmwareUpdatePackage | null;
    updateMessage: string;
    priority: 'none' | 'optional' | 'recommended' | 'critical';
    updateType: 'none' | 'security' | 'bugfix' | 'feature' | 'major';
    systemStatus: {
        batteryLevel: number;
        storageSpace: number;
        canUpdate: boolean;
        blockingReasons: string[];
    };
}

// 固件升级状态
export interface FirmwareUpgradeStatus {
    isUpgrading: boolean;
    progress: number; // 0-100
    stage: 'idle' | 'downloading' | 'verifying' | 'backup' | 'flashing' | 'verifying_flash' | 'updating_config' | 'rebooting' | 'completed' | 'failed';
    message: string;
    error: string | null;
    startTime: number | null;
    estimatedTime: number; // 预计时间(秒)
}

// 系统信息
export interface SystemInfo {
    availableSlots: Array<'Slot A' | 'Slot B'>;
    currentSlot: 'Slot A' | 'Slot B';
    backupAvailable: boolean;
} 