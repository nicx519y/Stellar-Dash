# 校准API接口文档

本文档描述了用于本地调试的校准相关模拟API接口。

## 数据存储

- **校准数据文件**: `app/api/data/calibration_data.json`
- **校准数据管理**: `app/api/data/calibration_store.ts`

## API接口列表

### 1. 获取全局配置
- **接口**: `GET /api/global-config`
- **功能**: 获取全局配置，包括校准相关状态
- **返回数据**:
```json
{
  "errNo": 0,
  "data": {
    "globalConfig": {
      "inputMode": "XINPUT",
      "autoCalibrationEnabled": true,
      "manualCalibrationActive": false
    }
  }
}
```

### 2. 更新全局配置
- **接口**: `POST /api/update-global-config`
- **功能**: 更新全局配置，包括自动校准开关
- **请求数据**:
```json
{
  "globalConfig": {
    "inputMode": "XINPUT",
    "autoCalibrationEnabled": true
  }
}
```

### 3. 开始手动校准
- **接口**: `POST /api/start-manual-calibration`
- **功能**: 开始手动校准过程
- **返回数据**:
```json
{
  "errNo": 0,
  "data": {
    "message": "Manual calibration started",
    "calibrationStatus": {
      "isActive": true,
      "uncalibratedCount": 8,
      "activeCalibrationCount": 8,
      "allCalibrated": false,
      "buttons": [...]
    }
  }
}
```

### 4. 停止手动校准
- **接口**: `POST /api/stop-manual-calibration`
- **功能**: 停止手动校准过程
- **返回数据**:
```json
{
  "errNo": 0,
  "data": {
    "message": "Manual calibration stopped",
    "calibrationStatus": {
      "isActive": false,
      "uncalibratedCount": 3,
      "activeCalibrationCount": 0,
      "allCalibrated": false,
      "buttons": [...]
    }
  }
}
```

### 5. 获取校准状态
- **接口**: `GET /api/get-calibration-status` 或 `POST /api/get-calibration-status`
- **功能**: 获取实时校准状态，支持模拟校准进度
- **返回数据**:
```json
{
  "errNo": 0,
  "data": {
    "calibrationStatus": {
      "isActive": true,
      "uncalibratedCount": 3,
      "activeCalibrationCount": 5,
      "allCalibrated": false,
      "buttons": [
        {
          "index": 0,
          "phase": "TOP_SAMPLING",
          "isCalibrated": false,
          "topValue": 0,
          "bottomValue": 0,
          "ledColor": "CYAN"
        }
      ]
    }
  }
}
```

### 6. 清除手动校准数据
- **接口**: `POST /api/clear-manual-calibration-data`
- **功能**: 清除所有手动校准数据
- **返回数据**:
```json
{
  "errNo": 0,
  "data": {
    "message": "Manual calibration data cleared successfully",
    "calibrationStatus": {
      "isActive": false,
      "uncalibratedCount": 8,
      "activeCalibrationCount": 0,
      "allCalibrated": false,
      "buttons": [...]
    }
  }
}
```

## 校准状态说明

### 校准阶段 (phase)
- `IDLE`: 空闲状态
- `TOP_SAMPLING`: 顶部值采样中（按键释放状态）
- `BOTTOM_SAMPLING`: 底部值采样中（按键按下状态）
- `COMPLETED`: 校准完成
- `ERROR`: 校准出错

### LED颜色 (ledColor)
- `OFF`: 关闭
- `RED`: 红色（未校准）
- `CYAN`: 天蓝色（顶部值采样中）
- `DARK_BLUE`: 深蓝色（底部值采样中）
- `GREEN`: 绿色（校准完成）
- `YELLOW`: 黄色（校准出错）

## 模拟功能

为了便于前端开发和测试，这些API包含以下模拟功能：

1. **自动进度更新**: 调用 `get-calibration-status` 时，如果校准正在进行，会随机模拟校准进度
2. **状态持久化**: 校准状态会保存在JSON文件中，重启服务后仍然保持
3. **错误模拟**: 可以通过修改数据文件来模拟各种错误状态

## 测试接口

可以通过访问 `GET /api/test-calibration` 来测试所有校准API接口是否正常工作。

## 使用示例

```typescript
// 前端使用示例
import { useGamepadConfig } from '@/contexts/gamepad-config-context';

function CalibrationTest() {
  const { 
    startManualCalibration, 
    stopManualCalibration, 
    fetchCalibrationStatus,
    clearManualCalibrationData,
    calibrationStatus 
  } = useGamepadConfig();

  const handleStartCalibration = async () => {
    await startManualCalibration();
  };

  const handleStopCalibration = async () => {
    await stopManualCalibration();
  };

  const handleClearData = async () => {
    await clearManualCalibrationData();
  };

  // 轮询校准状态
  useEffect(() => {
    const interval = setInterval(() => {
      if (calibrationStatus.isActive) {
        fetchCalibrationStatus();
      }
    }, 1000);
    return () => clearInterval(interval);
  }, [calibrationStatus.isActive]);

  return (
    <div>
      <button onClick={handleStartCalibration}>开始校准</button>
      <button onClick={handleStopCalibration}>停止校准</button>
      <button onClick={handleClearData}>清除数据</button>
      <p>校准状态: {calibrationStatus.isActive ? '进行中' : '已停止'}</p>
    </div>
  );
}
```

## 注意事项

1. 这些接口仅用于本地开发和调试
2. 数据存储在本地JSON文件中，不适用于生产环境
3. 校准进度更新是模拟的，实际设备需要真实的硬件交互
4. 可以通过修改 `calibration_data.json` 来自定义初始状态 