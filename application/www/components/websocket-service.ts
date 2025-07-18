// 保留必要的类型定义，供其他组件使用
export interface KeysConfig {
  invertXAxis: boolean;
  invertYAxis: boolean;
  fourWayMode: boolean;
  socdMode: number;
  keysEnableTag: boolean[];
  keyMapping: Record<string, number[]>;
}

export interface LedsConfig {
  ledEnabled: boolean;
  ledsEffectStyle: number;
  ledColors: string[];
  ledBrightness: number;
  ledAnimationSpeed: number;
  hasAroundLed: boolean;
  aroundLedEnabled: boolean;
  aroundLedSyncToMainLed: boolean;
  aroundLedTriggerByButton: boolean;
  aroundLedEffectStyle: number;
  aroundLedColors: string[];
  aroundLedBrightness: number;
  aroundLedAnimationSpeed: number;
}

export interface TriggerConfig {
  topDeadzone: number;
  bottomDeadzone: number;
  pressAccuracy: number;
  releaseAccuracy: number;
}

export interface TriggerConfigs {
  isAllBtnsConfiguring: boolean;
  debounceAlgorithm: number;
  triggerConfigs: TriggerConfig[];
}

export interface GlobalConfig {
  inputMode: string;
  autoCalibrationEnabled: boolean;
  manualCalibrationActive: boolean;
}

export interface ProfileConfig {
  id: string;
  name: string;
  keysConfig: KeysConfig;
  ledsConfigs: LedsConfig;
  triggerConfigs: TriggerConfigs;
}

export interface CalibrationStatus {
  isActive: boolean;
  uncalibratedCount: number;
  activeCalibrationCount: number;
  allCalibrated: boolean;
  buttons?: Array<{
    index: number;
    phase: string;
    isCalibrated: boolean;
    topValue: number;
    bottomValue: number;
    ledColor: string;
  }>;
}

// WebSocket响应数据类型
export interface WebSocketResponseData {
  globalConfig?: GlobalConfig;
  profileList?: { 
    defaultId: string; 
    maxNumProfiles: number; 
    items: Array<{ id: string; name: string; enabled: boolean }> 
  };
  profileDetails?: ProfileConfig;
  hotkeysConfig?: Array<{
    key: number;
    action: string;
    isHold: boolean;
    isLocked: boolean;
  }>;
  calibrationStatus?: CalibrationStatus;
  message?: string;
  [key: string]: unknown;
}

// 事件回调函数类型
export type EventCallback = (...args: unknown[]) => void; 