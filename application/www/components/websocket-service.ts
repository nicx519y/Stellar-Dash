import { 
  WebSocketFramework, 
  WebSocketConfig, 
  WebSocketDownstreamMessage,
  WebSocketError,
  WebSocketState 
} from './websocket-framework';

// 键值配置类型
export interface KeysConfig {
  invertXAxis: boolean;
  invertYAxis: boolean;
  fourWayMode: boolean;
  socdMode: number;
  keysEnableTag: boolean[];
  keyMapping: Record<string, number[]>;
}

// LED配置类型
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

// 触发器配置类型
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

// 全局配置类型
export interface GlobalConfig {
  inputMode: string;
  autoCalibrationEnabled: boolean;
  manualCalibrationActive: boolean;
}

// Profile配置类型
export interface ProfileConfig {
  id: string;
  name: string;
  keysConfig: KeysConfig;
  ledsConfigs: LedsConfig;
  triggerConfigs: TriggerConfigs;
}

// 校准状态类型
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

// WebSocket业务服务类
export class WebSocketService {
  private framework: WebSocketFramework;
  private eventListeners = new Map<string, Set<EventCallback>>();

  constructor(config?: WebSocketConfig) {
    this.framework = new WebSocketFramework({
      url: `ws://${window.location.hostname}:8081`,
      heartbeatInterval: 30000,
      reconnectInterval: 5000,
      maxReconnectAttempts: 5,
      timeout: 15000,
      autoReconnect: true,
      ...config
    });

    // 设置消息监听器
    this.framework.onMessage(this.handleMessage.bind(this));
    this.framework.onStateChange(this.handleStateChange.bind(this));
    this.framework.onError(this.handleError.bind(this));
  }

  // ===== 连接管理 =====
  public async connect(): Promise<void> {
    return this.framework.connect();
  }

  public disconnect(): void {
    this.framework.disconnect();
  }

  public getState(): WebSocketState {
    return this.framework.getState();
  }

  // ===== 事件监听 =====
  public on(event: string, callback: EventCallback): () => void {
    if (!this.eventListeners.has(event)) {
      this.eventListeners.set(event, new Set());
    }
    this.eventListeners.get(event)!.add(callback);
    
    return () => {
      this.eventListeners.get(event)?.delete(callback);
    };
  }

  private emit(event: string, ...args: unknown[]): void {
    this.eventListeners.get(event)?.forEach(callback => {
      try {
        callback(...args);
      } catch (error) {
        console.error(`Error in event listener for ${event}:`, error);
      }
    });
  }

  // ===== 全局配置相关 =====
  public async getGlobalConfig(): Promise<GlobalConfig> {
    const response = await this.framework.sendMessage('get_global_config', {});
    if (!response) {
      throw new Error('获取全局配置失败：无响应');
    }
    if (response.errNo !== 0) {
      throw new Error('获取全局配置失败');
    }
    return (response.data as WebSocketResponseData)?.globalConfig as GlobalConfig;
  }

  public async updateGlobalConfig(config: Partial<GlobalConfig>): Promise<GlobalConfig> {
    const response = await this.framework.sendMessage('update_global_config', {
      globalConfig: config
    });
    if (!response) {
      throw new Error('更新全局配置失败：无响应');
    }
    if (response.errNo !== 0) {
      throw new Error('更新全局配置失败');
    }
    return (response.data as WebSocketResponseData)?.globalConfig as GlobalConfig;
  }

  // ===== 配置文件相关 =====
  public async getProfileList(): Promise<{ 
    defaultId: string; 
    maxNumProfiles: number; 
    items: Array<{ id: string; name: string; enabled: boolean }> 
  }> {
    const response = await this.framework.sendMessage('get_profile_list', {});
    if (!response) {
      throw new Error('获取配置文件列表失败：无响应');
    }
    if (response.errNo !== 0) {
      throw new Error('获取配置文件列表失败');
    }
    return (response.data as WebSocketResponseData)?.profileList as { 
      defaultId: string; 
      maxNumProfiles: number; 
      items: Array<{ id: string; name: string; enabled: boolean }> 
    };
  }

  public async getDefaultProfile(): Promise<ProfileConfig> {
    const response = await this.framework.sendMessage('get_default_profile', {});
    if (!response) {
      throw new Error('获取默认配置文件失败：无响应');
    }
    if (response.errNo !== 0) {
      throw new Error('获取默认配置文件失败');
    }
    return (response.data as WebSocketResponseData)?.profileDetails as ProfileConfig;
  }

  public async updateProfile(profileDetails: Partial<ProfileConfig>): Promise<ProfileConfig> {
    const response = await this.framework.sendMessage('update_profile', {
      profileDetails
    });
    if (!response) {
      throw new Error('更新配置文件失败：无响应');
    }
    if (response.errNo !== 0) {
      throw new Error('更新配置文件失败');
    }
    return (response.data as WebSocketResponseData)?.profileDetails as ProfileConfig;
  }

  public async createProfile(profileName: string): Promise<{ 
    defaultId: string; 
    maxNumProfiles: number; 
    items: Array<{ id: string; name: string; enabled: boolean }> 
  }> {
    const response = await this.framework.sendMessage('create_profile', {
      profileName
    });
    if (!response) {
      throw new Error('创建配置文件失败：无响应');
    }
    if (response.errNo !== 0) {
      throw new Error('创建配置文件失败');
    }
    return (response.data as WebSocketResponseData)?.profileList as { 
      defaultId: string; 
      maxNumProfiles: number; 
      items: Array<{ id: string; name: string; enabled: boolean }> 
    };
  }

  public async deleteProfile(profileId: string): Promise<{ 
    defaultId: string; 
    maxNumProfiles: number; 
    items: Array<{ id: string; name: string; enabled: boolean }> 
  }> {
    const response = await this.framework.sendMessage('delete_profile', {
      profileId
    });
    if (!response) {
      throw new Error('删除配置文件失败：无响应');
    }
    if (response.errNo !== 0) {
      throw new Error('删除配置文件失败');
    }
    return (response.data as WebSocketResponseData)?.profileList as { 
      defaultId: string; 
      maxNumProfiles: number; 
      items: Array<{ id: string; name: string; enabled: boolean }> 
    };
  }

  public async switchDefaultProfile(profileId: string): Promise<{ 
    defaultId: string; 
    maxNumProfiles: number; 
    items: Array<{ id: string; name: string; enabled: boolean }> 
  }> {
    const response = await this.framework.sendMessage('switch_default_profile', {
      profileId
    });
    if (!response) {
      throw new Error('切换默认配置文件失败：无响应');
    }
    if (response.errNo !== 0) {
      throw new Error('切换默认配置文件失败');
    }
    return (response.data as WebSocketResponseData)?.profileList as { 
      defaultId: string; 
      maxNumProfiles: number; 
      items: Array<{ id: string; name: string; enabled: boolean }> 
    };
  }

  // ===== 快捷键配置相关 =====
  public async getHotkeysConfig(): Promise<Array<{
    key: number;
    action: string;
    isHold: boolean;
    isLocked: boolean;
  }>> {
    const response = await this.framework.sendMessage('get_hotkeys_config', {});
    if (!response) {
      throw new Error('获取快捷键配置失败：无响应');
    }
    if (response.errNo !== 0) {
      throw new Error('获取快捷键配置失败');
    }
    return (response.data as WebSocketResponseData)?.hotkeysConfig as Array<{
      key: number;
      action: string;
      isHold: boolean;
      isLocked: boolean;
    }>;
  }

  public async updateHotkeysConfig(hotkeysConfig: Array<{
    key: number;
    action: string;
    isHold: boolean;
    isLocked: boolean;
  }>): Promise<Array<{
    key: number;
    action: string;
    isHold: boolean;
    isLocked: boolean;
  }>> {
    const response = await this.framework.sendMessage('update_hotkeys_config', {
      hotkeysConfig
    });
    if (!response) {
      throw new Error('更新快捷键配置失败：无响应');
    }
    if (response.errNo !== 0) {
      throw new Error('更新快捷键配置失败');
    }
    return (response.data as WebSocketResponseData)?.hotkeysConfig as Array<{
      key: number;
      action: string;
      isHold: boolean;
      isLocked: boolean;
    }>;
  }

  // ===== 校准相关 =====
  public async getCalibrationStatus(): Promise<CalibrationStatus> {
    const response = await this.framework.sendMessage('get_calibration_status', {});
    if (!response) {
      throw new Error('获取校准状态失败：无响应');
    }
    if (response.errNo !== 0) {
      throw new Error('获取校准状态失败');
    }
    return (response.data as WebSocketResponseData)?.calibrationStatus as CalibrationStatus;
  }

  public async startManualCalibration(): Promise<{ message: string; calibrationStatus: CalibrationStatus }> {
    const response = await this.framework.sendMessage('start_manual_calibration', {});
    if (!response) {
      throw new Error('开始手动校准失败：无响应');
    }
    if (response.errNo !== 0) {
      throw new Error('开始手动校准失败');
    }
    return response.data as { message: string; calibrationStatus: CalibrationStatus };
  }

  public async stopManualCalibration(): Promise<{ message: string; calibrationStatus: CalibrationStatus }> {
    const response = await this.framework.sendMessage('stop_manual_calibration', {});
    if (!response) {
      throw new Error('停止手动校准失败：无响应');
    }
    if (response.errNo !== 0) {
      throw new Error('停止手动校准失败');
    }
    return response.data as { message: string; calibrationStatus: CalibrationStatus };
  }

  // ===== 系统控制 =====
  public async reboot(): Promise<{ message: string }> {
    const response = await this.framework.sendMessage('reboot', {});
    if (!response) {
      throw new Error('系统重启失败：无响应');
    }
    if (response.errNo !== 0) {
      throw new Error('系统重启失败');
    }
    return response.data as { message: string };
  }

  // ===== Ping测试 =====
  public async ping(params: Record<string, unknown> = {}): Promise<Record<string, unknown>> {
    const response = await this.framework.sendMessage('ping', params);
    if (!response) {
      throw new Error('Ping失败：无响应');
    }
    if (response.errNo !== 0) {
      throw new Error('Ping失败');
    }
    return (response.data as Record<string, unknown>) || {};
  }

  // ===== 内部事件处理 =====
  private handleMessage(message: WebSocketDownstreamMessage): void {
    // 处理服务器主动推送的消息
    if (message.command === 'welcome') {
      this.emit('welcome', message.data);
    } else if (message.command === 'notification') {
      this.emit('notification', message.data);
    } else if (message.command === 'config_changed') {
      this.emit('configChanged', message.data);
    } else if (message.command === 'calibration_update') {
      this.emit('calibrationUpdate', message.data);
    } else if (message.command === 'button_state_changed') {
      this.emit('buttonStateChanged', message.data);
    }
    
    // 通用消息事件
    this.emit('message', message);
  }

  private handleStateChange(state: WebSocketState): void {
    this.emit('stateChange', state);
  }

  private handleError(error: WebSocketError): void {
    this.emit('error', error);
  }
}

// 创建全局实例
export const webSocketService = new WebSocketService();

// 导出便捷方法
export const connectWebSocket = () => webSocketService.connect();
export const disconnectWebSocket = () => webSocketService.disconnect();
export const getWebSocketState = () => webSocketService.getState(); 