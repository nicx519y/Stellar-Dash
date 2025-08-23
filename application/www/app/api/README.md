# WebSocket 模拟服务器

这个目录包含了用于模拟 C++ 代码中 WebSocket 接口的 Node.js 实现。

## 文件结构

- `websocket-server/route.ts` - 简化的 WebSocket 服务器实现（仅提供 API 接口）
- `websocket-test/page.tsx` - WebSocket 测试页面
- `../websocket-server.js` - 独立的 WebSocket 服务器脚本
- `../start-websocket-server.bat` - Windows 启动脚本
- `../start-websocket-server.sh` - Linux/Mac 启动脚本

## 支持的 WebSocket 命令

### 全局配置相关命令
- `get_global_config` - 获取全局配置
- `update_global_config` - 更新全局配置
- `get_hotkeys_config` - 获取快捷键配置
- `update_hotkeys_config` - 更新快捷键配置
- `reboot` - 重启系统
- `push_leds_config` - 推送LED配置
- `clear_leds_preview` - 清除LED预览

### 配置文件相关命令
- `get_profile_list` - 获取配置文件列表
- `get_default_profile` - 获取默认配置文件
- `update_profile` - 更新配置文件
- `create_profile` - 创建配置文件
- `delete_profile` - 删除配置文件
- `switch_default_profile` - 切换默认配置文件

### 轴体映射相关命令
- `ms_get_list` - 获取轴体映射列表
- `ms_get_mark_status` - 获取映射标记状态
- `ms_set_default` - 设置默认映射
- `ms_get_default` - 获取默认映射
- `ms_create_mapping` - 创建映射
- `ms_delete_mapping` - 删除映射
- `ms_rename_mapping` - 重命名映射
- `ms_mark_mapping_start` - 开始映射标记
- `ms_mark_mapping_stop` - 停止映射标记
- `ms_mark_mapping_step` - 映射标记步骤
- `ms_get_mapping` - 获取映射详情

### 校准相关命令
- `start_manual_calibration` - 开始手动校准
- `stop_manual_calibration` - 停止手动校准
- `get_calibration_status` - 获取校准状态
- `clear_manual_calibration_data` - 清除校准数据
- `check_is_manual_calibration_completed` - 检查校准是否完成

### 按键监控相关命令
- `start_button_monitoring` - 开始按键监控
- `stop_button_monitoring` - 停止按键监控
- `start_button_performance_monitoring` - 开始按键性能监控
- `stop_button_performance_monitoring` - 停止按键性能监控
- `get_button_states` - 获取按键状态

### 固件相关命令
- `get_device_auth` - 获取设备认证
- `get_firmware_metadata` - 获取固件元数据
- `create_firmware_upgrade_session` - 创建固件升级会话
- `upload_firmware_chunk` - 上传固件分片
- `complete_firmware_upgrade_session` - 完成固件升级会话
- `abort_firmware_upgrade_session` - 中止固件升级会话
- `get_firmware_upgrade_status` - 获取固件升级状态
- `cleanup_firmware_upgrade_session` - 清理固件升级会话

### 特殊命令
- `ping` - Ping 测试

## 使用方法

### 方法一：同时启动所有服务（推荐）

#### 1. 一键启动 Next.js 和 WebSocket 服务器

```bash
cd application/www
npm run dev:all
```

这将同时启动：
- Next.js 开发服务器（端口 3000）
- WebSocket 模拟服务器（端口 8081）

#### 2. 访问测试页面

打开浏览器访问：`http://localhost:3000/api/websocket-test`

### 方法二：分别启动服务

#### 1. 启动 WebSocket 服务器

**Windows:**
```bash
cd application/www
start-websocket-server.bat
```

**Linux/Mac:**
```bash
cd application/www
./start-websocket-server.sh
```

**或者直接使用 Node.js:**
```bash
cd application/www
node websocket-server.js
```

#### 2. 启动 Next.js 开发服务器

```bash
cd application/www
npm run dev
```

#### 3. 访问测试页面

打开浏览器访问：`http://localhost:3000/api/websocket-test`

### 方法三：仅使用 Next.js API 路由

#### 1. 启动开发服务器

```bash
cd application/www
npm run dev
```

#### 2. 访问测试页面

打开浏览器访问：`http://localhost:3000/api/websocket-test`

### 3. 连接 WebSocket 服务器

在测试页面中：
1. 输入 WebSocket 服务器地址（默认：`ws://localhost:8081`）
2. 点击"连接"按钮
3. 等待连接成功

### 4. 发送命令

1. 从下拉菜单中选择要测试的命令
2. 在参数文本框中输入 JSON 格式的参数（可选）
3. 点击"发送命令"按钮
4. 在消息日志中查看响应

### 5. 快速测试

使用页面底部的快速测试按钮可以快速测试常用命令：
- Ping 测试
- 获取全局配置
- 获取配置文件列表
- 获取按键状态

## 消息格式

### 请求格式
```json
{
  "cid": 1,
  "command": "get_global_config",
  "params": {}
}
```

### 响应格式
```json
{
  "cid": 1,
  "command": "get_global_config",
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

## 错误处理

当命令执行失败时，响应格式为：
```json
{
  "cid": 1,
  "command": "unknown_command",
  "errNo": -1,
  "data": {
    "errorMessage": "未知命令: unknown_command"
  }
}
```

## 模拟数据

服务器使用内存中的模拟数据，包括：
- 全局配置
- 快捷键配置
- 配置文件列表
- 轴体映射列表
- 校准状态
- 按键监控状态
- 固件元数据

所有数据在服务器重启后会重置为默认值。

## 注意事项

1. 这是一个模拟服务器，不会真正执行硬件操作
2. 所有数据都存储在内存中，重启后会丢失
3. 主要用于前端开发和测试
4. 支持与真实 C++ WebSocket 服务器相同的消息格式 