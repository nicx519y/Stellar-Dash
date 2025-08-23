import { NextRequest } from 'next/server';

// WebSocket 消息类型定义
interface WebSocketUpstreamMessage {
  cid: number;
  command: string;
  params?: Record<string, unknown>;
}

interface WebSocketDownstreamMessage {
  cid?: number;
  command: string;
  errNo: number;
  data?: Record<string, unknown>;
}

// 模拟数据存储
class MockDataStore {
  private globalConfig = {
    inputMode: "XINPUT",
    autoCalibrationEnabled: true,
    manualCalibrationActive: false
  };

  private hotkeysConfig = {
    hotkeys: []
  };

  private profiles = [
    { id: "default", name: "默认配置", isDefault: true },
    { id: "profile1", name: "配置1", isDefault: false },
    { id: "profile2", name: "配置2", isDefault: false }
  ];

  private msMappings = [
    { id: "mapping1", name: "映射1", isDefault: true },
    { id: "mapping2", name: "映射2", isDefault: false }
  ];

  private calibrationStatus = {
    isActive: false,
    progress: 0,
    step: 0
  };

  private buttonMonitoringStatus = {
    isActive: false,
    isTestModeEnabled: false
  };

  private firmwareMetadata = {
    version: "1.0.0",
    buildDate: "2024-01-01",
    deviceId: "DEVICE_12345",
    originalUniqueId: "0123456789ABCDEF"
  };

  // Getter 方法
  getGlobalConfig() { return this.globalConfig; }
  getHotkeysConfig() { return this.hotkeysConfig; }
  getProfiles() { return this.profiles; }
  getMsMappings() { return this.msMappings; }
  getCalibrationStatus() { return this.calibrationStatus; }
  getButtonMonitoringStatus() { return this.buttonMonitoringStatus; }
  getFirmwareMetadata() { return this.firmwareMetadata; }

  // Setter 方法
  updateGlobalConfig(config: any) { this.globalConfig = { ...this.globalConfig, ...config }; }
  updateHotkeysConfig(config: any) { this.hotkeysConfig = { ...this.hotkeysConfig, ...config }; }
  setCalibrationStatus(status: any) { this.calibrationStatus = { ...this.calibrationStatus, ...status }; }
  setButtonMonitoringStatus(status: any) { this.buttonMonitoringStatus = { ...this.buttonMonitoringStatus, ...status }; }
}

const dataStore = new MockDataStore();

// 命令处理器
class CommandHandler {
  static createSuccessResponse(cid: number, command: string, data?: Record<string, unknown>): WebSocketDownstreamMessage {
    return {
      cid,
      command,
      errNo: 0,
      data: data || {}
    };
  }

  static createErrorResponse(cid: number, command: string, errorMessage: string): WebSocketDownstreamMessage {
    return {
      cid,
      command,
      errNo: -1,
      data: { errorMessage }
    };
  }

  // 全局配置相关命令
  static handleGetGlobalConfig(request: WebSocketUpstreamMessage): WebSocketDownstreamMessage {
    return this.createSuccessResponse(request.cid, request.command, {
      globalConfig: dataStore.getGlobalConfig()
    });
  }

  static handleUpdateGlobalConfig(request: WebSocketUpstreamMessage): WebSocketDownstreamMessage {
    const config = request.params?.globalConfig;
    if (config) {
      dataStore.updateGlobalConfig(config);
      return this.createSuccessResponse(request.cid, request.command, {
        message: "全局配置更新成功"
      });
    }
    return this.createErrorResponse(request.cid, request.command, "无效的配置数据");
  }

  static handleGetHotkeysConfig(request: WebSocketUpstreamMessage): WebSocketDownstreamMessage {
    return this.createSuccessResponse(request.cid, request.command, {
      hotkeysConfig: dataStore.getHotkeysConfig()
    });
  }

  static handleUpdateHotkeysConfig(request: WebSocketUpstreamMessage): WebSocketDownstreamMessage {
    const config = request.params?.hotkeysConfig;
    if (config) {
      dataStore.updateHotkeysConfig(config);
      return this.createSuccessResponse(request.cid, request.command, {
        message: "快捷键配置更新成功"
      });
    }
    return this.createErrorResponse(request.cid, request.command, "无效的快捷键配置数据");
  }

  static handleReboot(request: WebSocketUpstreamMessage): WebSocketDownstreamMessage {
    return this.createSuccessResponse(request.cid, request.command, {
      message: "系统正在重启"
    });
  }

  static handlePushLedsConfig(request: WebSocketUpstreamMessage): WebSocketDownstreamMessage {
    return this.createSuccessResponse(request.cid, request.command, {
      message: "LED配置推送成功"
    });
  }

  static handleClearLedsPreview(request: WebSocketUpstreamMessage): WebSocketDownstreamMessage {
    return this.createSuccessResponse(request.cid, request.command, {
      message: "LED预览模式已清除"
    });
  }

  // 配置文件相关命令
  static handleGetProfileList(request: WebSocketUpstreamMessage): WebSocketDownstreamMessage {
    return this.createSuccessResponse(request.cid, request.command, {
      profiles: dataStore.getProfiles()
    });
  }

  static handleGetDefaultProfile(request: WebSocketUpstreamMessage): WebSocketDownstreamMessage {
    const defaultProfile = dataStore.getProfiles().find(p => p.isDefault);
    return this.createSuccessResponse(request.cid, request.command, {
      defaultProfile
    });
  }

  static handleUpdateProfile(request: WebSocketUpstreamMessage): WebSocketDownstreamMessage {
    return this.createSuccessResponse(request.cid, request.command, {
      message: "配置文件更新成功"
    });
  }

  static handleCreateProfile(request: WebSocketUpstreamMessage): WebSocketDownstreamMessage {
    return this.createSuccessResponse(request.cid, request.command, {
      message: "配置文件创建成功",
      profileId: "new_profile_" + Date.now()
    });
  }

  static handleDeleteProfile(request: WebSocketUpstreamMessage): WebSocketDownstreamMessage {
    return this.createSuccessResponse(request.cid, request.command, {
      message: "配置文件删除成功"
    });
  }

  static handleSwitchDefaultProfile(request: WebSocketUpstreamMessage): WebSocketDownstreamMessage {
    return this.createSuccessResponse(request.cid, request.command, {
      message: "默认配置文件切换成功"
    });
  }

  // 轴体映射相关命令
  static handleMsGetList(request: WebSocketUpstreamMessage): WebSocketDownstreamMessage {
    return this.createSuccessResponse(request.cid, request.command, {
      mappings: dataStore.getMsMappings()
    });
  }

  static handleMsGetMarkStatus(request: WebSocketUpstreamMessage): WebSocketDownstreamMessage {
    return this.createSuccessResponse(request.cid, request.command, {
      isMarking: false,
      currentStep: 0
    });
  }

  static handleMsSetDefault(request: WebSocketUpstreamMessage): WebSocketDownstreamMessage {
    return this.createSuccessResponse(request.cid, request.command, {
      message: "默认映射设置成功"
    });
  }

  static handleMsGetDefault(request: WebSocketUpstreamMessage): WebSocketDownstreamMessage {
    const defaultMapping = dataStore.getMsMappings().find(m => m.isDefault);
    return this.createSuccessResponse(request.cid, request.command, {
      defaultMapping
    });
  }

  static handleMsCreateMapping(request: WebSocketUpstreamMessage): WebSocketDownstreamMessage {
    return this.createSuccessResponse(request.cid, request.command, {
      message: "映射创建成功",
      mappingId: "new_mapping_" + Date.now()
    });
  }

  static handleMsDeleteMapping(request: WebSocketUpstreamMessage): WebSocketDownstreamMessage {
    return this.createSuccessResponse(request.cid, request.command, {
      message: "映射删除成功"
    });
  }

  static handleMsRenameMapping(request: WebSocketUpstreamMessage): WebSocketDownstreamMessage {
    return this.createSuccessResponse(request.cid, request.command, {
      message: "映射重命名成功"
    });
  }

  static handleMsMarkMappingStart(request: WebSocketUpstreamMessage): WebSocketDownstreamMessage {
    return this.createSuccessResponse(request.cid, request.command, {
      message: "映射标记开始"
    });
  }

  static handleMsMarkMappingStop(request: WebSocketUpstreamMessage): WebSocketDownstreamMessage {
    return this.createSuccessResponse(request.cid, request.command, {
      message: "映射标记停止"
    });
  }

  static handleMsMarkMappingStep(request: WebSocketUpstreamMessage): WebSocketDownstreamMessage {
    return this.createSuccessResponse(request.cid, request.command, {
      message: "映射标记步骤完成"
    });
  }

  static handleMsGetMapping(request: WebSocketUpstreamMessage): WebSocketDownstreamMessage {
    return this.createSuccessResponse(request.cid, request.command, {
      mapping: {
        id: "mapping1",
        name: "映射1",
        values: [100, 200, 300, 400],
        originalValues: [50, 150, 250, 350]
      }
    });
  }

  // 校准相关命令
  static handleStartManualCalibration(request: WebSocketUpstreamMessage): WebSocketDownstreamMessage {
    dataStore.setCalibrationStatus({ isActive: true, progress: 0, step: 1 });
    return this.createSuccessResponse(request.cid, request.command, {
      message: "手动校准开始",
      status: "active"
    });
  }

  static handleStopManualCalibration(request: WebSocketUpstreamMessage): WebSocketDownstreamMessage {
    dataStore.setCalibrationStatus({ isActive: false, progress: 0, step: 0 });
    return this.createSuccessResponse(request.cid, request.command, {
      message: "手动校准停止",
      status: "stopped"
    });
  }

  static handleGetCalibrationStatus(request: WebSocketUpstreamMessage): WebSocketDownstreamMessage {
    return this.createSuccessResponse(request.cid, request.command, {
      calibrationStatus: dataStore.getCalibrationStatus()
    });
  }

  static handleClearManualCalibrationData(request: WebSocketUpstreamMessage): WebSocketDownstreamMessage {
    return this.createSuccessResponse(request.cid, request.command, {
      message: "校准数据清除成功"
    });
  }

  static handleCheckIsManualCalibrationCompleted(request: WebSocketUpstreamMessage): WebSocketDownstreamMessage {
    return this.createSuccessResponse(request.cid, request.command, {
      isCompleted: false,
      progress: 50
    });
  }

  // 按键监控相关命令
  static handleStartButtonMonitoring(request: WebSocketUpstreamMessage): WebSocketDownstreamMessage {
    dataStore.setButtonMonitoringStatus({ isActive: true, isTestModeEnabled: false });
    return this.createSuccessResponse(request.cid, request.command, {
      message: "按键监控启动成功",
      status: "active"
    });
  }

  static handleStopButtonMonitoring(request: WebSocketUpstreamMessage): WebSocketDownstreamMessage {
    dataStore.setButtonMonitoringStatus({ isActive: false, isTestModeEnabled: false });
    return this.createSuccessResponse(request.cid, request.command, {
      message: "按键监控停止成功",
      status: "stopped"
    });
  }

  static handleStartButtonPerformanceMonitoring(request: WebSocketUpstreamMessage): WebSocketDownstreamMessage {
    dataStore.setButtonMonitoringStatus({ isActive: true, isTestModeEnabled: true });
    return this.createSuccessResponse(request.cid, request.command, {
      message: "按键性能监控启动成功",
      status: "active",
      isTestModeEnabled: true
    });
  }

  static handleStopButtonPerformanceMonitoring(request: WebSocketUpstreamMessage): WebSocketDownstreamMessage {
    dataStore.setButtonMonitoringStatus({ isActive: false, isTestModeEnabled: false });
    return this.createSuccessResponse(request.cid, request.command, {
      message: "按键性能监控停止成功",
      status: "stopped"
    });
  }

  static handleGetButtonStates(request: WebSocketUpstreamMessage): WebSocketDownstreamMessage {
    return this.createSuccessResponse(request.cid, request.command, {
      buttonStates: {
        DPAD_UP: false,
        DPAD_DOWN: false,
        DPAD_LEFT: false,
        DPAD_RIGHT: false,
        B1: false,
        B2: false,
        B3: false,
        B4: false,
        L1: false,
        L2: false,
        L3: false,
        R1: false,
        R2: false,
        R3: false,
        S1: false,
        S2: false,
        A1: false,
        A2: false
      }
    });
  }

  // 固件相关命令
  static handleGetDeviceAuth(request: WebSocketUpstreamMessage): WebSocketDownstreamMessage {
    return this.createSuccessResponse(request.cid, request.command, {
      deviceId: "DEVICE_12345",
      originalUniqueId: "0123456789ABCDEF",
      challenge: "DEV_12345678_87654321",
      timestamp: Date.now(),
      signature: "SIG_12345678",
      expiresIn: 1800
    });
  }

  static handleGetFirmwareMetadata(request: WebSocketUpstreamMessage): WebSocketDownstreamMessage {
    return this.createSuccessResponse(request.cid, request.command, {
      firmwareMetadata: dataStore.getFirmwareMetadata()
    });
  }

  static handleCreateFirmwareUpgradeSession(request: WebSocketUpstreamMessage): WebSocketDownstreamMessage {
    return this.createSuccessResponse(request.cid, request.command, {
      sessionId: "session_" + Date.now(),
      message: "固件升级会话创建成功"
    });
  }

  static handleUploadFirmwareChunk(request: WebSocketUpstreamMessage): WebSocketDownstreamMessage {
    return this.createSuccessResponse(request.cid, request.command, {
      message: "固件分片上传成功",
      chunkReceived: true
    });
  }

  static handleCompleteFirmwareUpgradeSession(request: WebSocketUpstreamMessage): WebSocketDownstreamMessage {
    return this.createSuccessResponse(request.cid, request.command, {
      message: "固件升级完成",
      upgradeSuccess: true
    });
  }

  static handleAbortFirmwareUpgradeSession(request: WebSocketUpstreamMessage): WebSocketDownstreamMessage {
    return this.createSuccessResponse(request.cid, request.command, {
      message: "固件升级会话已中止"
    });
  }

  static handleGetFirmwareUpgradeStatus(request: WebSocketUpstreamMessage): WebSocketDownstreamMessage {
    return this.createSuccessResponse(request.cid, request.command, {
      status: "idle",
      progress: 0,
      message: "无升级任务"
    });
  }

  static handleCleanupFirmwareUpgradeSession(request: WebSocketUpstreamMessage): WebSocketDownstreamMessage {
    return this.createSuccessResponse(request.cid, request.command, {
      message: "固件升级会话清理完成"
    });
  }

  // Ping 命令
  static handlePing(request: WebSocketUpstreamMessage): WebSocketDownstreamMessage {
    return this.createSuccessResponse(request.cid, request.command, {
      message: "pong",
      timestamp: Date.now()
    });
  }
}

// 命令映射表
const commandHandlers: Record<string, (request: WebSocketUpstreamMessage) => WebSocketDownstreamMessage> = {
  // 全局配置相关命令
  "get_global_config": CommandHandler.handleGetGlobalConfig,
  "update_global_config": CommandHandler.handleUpdateGlobalConfig,
  "get_hotkeys_config": CommandHandler.handleGetHotkeysConfig,
  "update_hotkeys_config": CommandHandler.handleUpdateHotkeysConfig,
  "reboot": CommandHandler.handleReboot,
  "push_leds_config": CommandHandler.handlePushLedsConfig,
  "clear_leds_preview": CommandHandler.handleClearLedsPreview,

  // 配置文件相关命令
  "get_profile_list": CommandHandler.handleGetProfileList,
  "get_default_profile": CommandHandler.handleGetDefaultProfile,
  "update_profile": CommandHandler.handleUpdateProfile,
  "create_profile": CommandHandler.handleCreateProfile,
  "delete_profile": CommandHandler.handleDeleteProfile,
  "switch_default_profile": CommandHandler.handleSwitchDefaultProfile,

  // 轴体映射相关命令
  "ms_get_list": CommandHandler.handleMsGetList,
  "ms_get_mark_status": CommandHandler.handleMsGetMarkStatus,
  "ms_set_default": CommandHandler.handleMsSetDefault,
  "ms_get_default": CommandHandler.handleMsGetDefault,
  "ms_create_mapping": CommandHandler.handleMsCreateMapping,
  "ms_delete_mapping": CommandHandler.handleMsDeleteMapping,
  "ms_rename_mapping": CommandHandler.handleMsRenameMapping,
  "ms_mark_mapping_start": CommandHandler.handleMsMarkMappingStart,
  "ms_mark_mapping_stop": CommandHandler.handleMsMarkMappingStop,
  "ms_mark_mapping_step": CommandHandler.handleMsMarkMappingStep,
  "ms_get_mapping": CommandHandler.handleMsGetMapping,

  // 校准相关命令
  "start_manual_calibration": CommandHandler.handleStartManualCalibration,
  "stop_manual_calibration": CommandHandler.handleStopManualCalibration,
  "get_calibration_status": CommandHandler.handleGetCalibrationStatus,
  "clear_manual_calibration_data": CommandHandler.handleClearManualCalibrationData,
  "check_is_manual_calibration_completed": CommandHandler.handleCheckIsManualCalibrationCompleted,

  // 按键监控相关命令
  "start_button_monitoring": CommandHandler.handleStartButtonMonitoring,
  "stop_button_monitoring": CommandHandler.handleStopButtonMonitoring,
  "start_button_performance_monitoring": CommandHandler.handleStartButtonPerformanceMonitoring,
  "stop_button_performance_monitoring": CommandHandler.handleStopButtonPerformanceMonitoring,
  "get_button_states": CommandHandler.handleGetButtonStates,

  // 固件相关命令
  "get_device_auth": CommandHandler.handleGetDeviceAuth,
  "get_firmware_metadata": CommandHandler.handleGetFirmwareMetadata,
  "create_firmware_upgrade_session": CommandHandler.handleCreateFirmwareUpgradeSession,
  "upload_firmware_chunk": CommandHandler.handleUploadFirmwareChunk,
  "complete_firmware_upgrade_session": CommandHandler.handleCompleteFirmwareUpgradeSession,
  "abort_firmware_upgrade_session": CommandHandler.handleAbortFirmwareUpgradeSession,
  "get_firmware_upgrade_status": CommandHandler.handleGetFirmwareUpgradeStatus,
  "cleanup_firmware_upgrade_session": CommandHandler.handleCleanupFirmwareUpgradeSession,

  // 特殊命令
  "ping": CommandHandler.handlePing,
};

// 处理 WebSocket 消息
function handleWebSocketMessage(message: WebSocketUpstreamMessage): WebSocketDownstreamMessage {
  // 验证消息格式
  if (!message.cid || !message.command) {
    return {
      cid: message.cid || 0,
      command: message.command || '',
      errNo: -1,
      data: { errorMessage: "缺少必要字段: cid 或 command" }
    };
  }

  // 查找命令处理器
  const handler = commandHandlers[message.command];
  if (handler) {
    return handler(message);
  } else {
    return {
      cid: message.cid,
      command: message.command,
      errNo: -1,
      data: { errorMessage: `未知命令: ${message.command}` }
    };
  }
}

export async function GET(request: NextRequest) {
  // 返回 WebSocket 服务器状态信息
  return new Response(JSON.stringify({
    status: "running",
    message: "WebSocket 模拟服务器已启动",
    supportedCommands: Object.keys(commandHandlers),
    timestamp: Date.now()
  }), {
    status: 200,
    headers: {
      'Content-Type': 'application/json',
    },
  });
}

// 导出处理函数供其他模块使用
export { handleWebSocketMessage, dataStore }; 