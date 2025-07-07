# 设备ID校验中间件使用说明

## 概述

本文档说明了STM32 HBox固件管理服务器中设备ID校验中间件的使用方法。

## 中间件功能

### 1. validateDeviceId(options)

设备ID校验中间件，用于验证和标准化设备ID相关信息。

#### 参数选项

```javascript
{
    requireRawUniqueId: false,  // 是否需要原始唯一ID
    requireDeviceId: false,     // 是否需要设备ID
    validateHash: false,        // 是否需要校验哈希
    source: 'body'              // 数据源: 'body', 'query', 'params', 'headers'
}
```

#### 功能特性

- **数据源灵活性**: 支持从不同的请求部分提取设备ID信息
- **格式验证**: 验证原始唯一ID和设备ID的格式
- **哈希验证**: 可选的设备ID哈希验证
- **数据标准化**: 将验证后的数据附加到 `req.deviceInfo`

#### 使用示例

```javascript
// 基本使用 - 只验证格式
app.post('/api/some-endpoint', validateDeviceId(), (req, res) => {
    // req.deviceInfo 包含标准化后的设备信息
});

// 要求设备ID必须存在
app.post('/api/firmware-check-update', validateDeviceId({ requireDeviceId: true }), (req, res) => {
    const { deviceId } = req.deviceInfo;
    // 处理逻辑
});

// 完整验证（用于设备注册）
app.post('/api/device/register', validateDeviceId({ 
    requireRawUniqueId: true, 
    requireDeviceId: true, 
    validateHash: true 
}), (req, res) => {
    // 所有验证都通过后的处理逻辑
});

// 从查询参数中提取设备ID
app.get('/api/device-info', validateDeviceId({ 
    requireDeviceId: true, 
    source: 'query' 
}), (req, res) => {
    // 处理 GET /api/device-info?deviceId=1234567890ABCDEF
});
```

### 2. requireRegisteredDevice()

设备存在性检查中间件，验证设备是否已注册。

#### 功能特性

- **设备存在性检查**: 验证设备是否在数据库中存在
- **自动更新活跃时间**: 更新设备的最后活跃时间
- **设备信息附加**: 将设备信息附加到 `req.registeredDevice`

#### 使用示例

```javascript
// 需要设备已注册才能访问的接口
app.post('/api/firmware-download', 
    validateDeviceId({ requireDeviceId: true }), 
    requireRegisteredDevice(), 
    (req, res) => {
        const { deviceId } = req.deviceInfo;
        const registeredDevice = req.registeredDevice;
        // 处理已注册设备的请求
    }
);
```

## 接口更新说明

### 已更新的接口

1. **设备注册接口** (`POST /api/device/register`)
   - 使用完整的设备ID校验（包括哈希验证）
   - 简化了重复的验证代码

2. **固件更新检查接口** (`POST /api/firmware-check-update`)
   - 添加了设备ID验证
   - 在响应中包含设备ID信息

3. **新增设备认证的固件下载接口** (`POST /api/firmware-download`)
   - 需要设备ID验证和设备注册验证
   - 提供安全的固件下载信息

## 数据格式

### 输入数据格式

```javascript
{
    rawUniqueId: "1234ABCD-5678EFGH-9012IJKL",  // 格式: XXXXXXXX-XXXXXXXX-XXXXXXXX
    deviceId: "1234567890ABCDEF",                // 格式: 16位十六进制
    deviceName: "HBox-Device-01"                 // 可选，会自动生成
}
```

### 中间件处理后的数据

```javascript
// req.deviceInfo
{
    rawUniqueId: "1234ABCD-5678EFGH-9012IJKL",
    deviceId: "1234567890ABCDEF",
    deviceName: "HBox-12345678"
}

// req.registeredDevice (如果使用 requireRegisteredDevice)
{
    rawUniqueId: "1234ABCD-5678EFGH-9012IJKL",
    deviceId: "1234567890ABCDEF",
    deviceName: "HBox-12345678",
    registerTime: "2024-01-01T00:00:00.000Z",
    lastSeen: "2024-01-01T12:00:00.000Z",
    status: "active",
    registerIP: "192.168.1.100"
}
```

## 错误处理

中间件会返回标准的错误响应格式：

```javascript
{
    success: false,
    message: "错误描述",
    errNo: 1,
    errorMessage: "English error message"
}
```

### 常见错误类型

1. **参数缺失**: 必需的参数未提供
2. **格式错误**: 原始唯一ID或设备ID格式不正确
3. **哈希验证失败**: 设备ID哈希与原始唯一ID不匹配
4. **设备未注册**: 设备ID在数据库中不存在

## 测试

使用提供的测试脚本验证中间件功能：

```bash
# 启动服务器
node server/server.js

# 在另一个终端运行测试
node server/test_middleware.js
```

## 扩展使用

### 添加新的需要设备验证的接口

```javascript
// 示例：设备状态上报接口
app.post('/api/device/status', 
    validateDeviceId({ requireDeviceId: true }), 
    requireRegisteredDevice(), 
    (req, res) => {
        const { deviceId } = req.deviceInfo;
        const registeredDevice = req.registeredDevice;
        const { status, batteryLevel, temperature } = req.body;
        
        // 处理设备状态上报
        // ...
    }
);
```

### 自定义验证选项

```javascript
// 从HTTP头部提取设备ID
app.get('/api/device/info', 
    validateDeviceId({ 
        requireDeviceId: true, 
        source: 'headers' 
    }), 
    (req, res) => {
        // 处理 X-Device-Id 头部
    }
);
```

## 注意事项

1. **中间件顺序**: `validateDeviceId` 必须在 `requireRegisteredDevice` 之前使用
2. **数据源选择**: 根据API设计选择合适的数据源（body、query、params、headers）
3. **错误处理**: 中间件会自动处理验证错误，无需在路由处理器中重复验证
4. **性能考虑**: 哈希验证涉及计算，只在必要时启用 