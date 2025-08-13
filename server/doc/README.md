# STM32 HBox 固件服务器

## 项目简介

STM32 HBox 固件服务器是一个专为STM32 HBox设备设计的固件管理和分发系统。该系统提供固件上传、版本管理、设备注册、OTA更新等功能，支持多种游戏手柄协议。

## 功能特性

- 🔧 **固件管理**: 支持固件上传、版本控制、批量删除
- 📱 **设备注册**: 自动设备ID验证和注册
- 🔄 **OTA更新**: 支持设备在线固件更新
- 🎮 **多协议支持**: PS4、PS Classic、Switch、Xbox One、XInput等
- 🔐 **安全认证**: 设备ID哈希验证，确保固件安全性
- 📊 **状态监控**: 实时服务状态和日志监控
- 🌐 **Web界面**: 现代化的Web管理界面

## 系统架构

```
server/
├── src/                    # 核心源代码
│   ├── server.js          # 主服务器文件
│   ├── firmware.js        # 固件管理模块
│   ├── auth.js            # 认证模块
│   ├── action.js          # 动作处理模块
│   └── device-auth.js     # 设备认证模块
├── data/                   # 数据存储
│   ├── firmware_list.json # 固件列表
│   ├── device_ids.json    # 设备ID数据库
│   └── auth_config.json   # 认证配置
├── tools/                  # 部署和管理工具
│   ├── deploy.ps1         # 主部署脚本
│   ├── deploy-simple.ps1  # 简化部署脚本
│   ├── service-manager.ps1 # 服务管理脚本
│   └── deploy-config.json # 部署配置
├── uploads/               # 固件文件存储
├── start.js              # 服务启动脚本
├── stop.js               # 服务停止脚本
└── package.json          # 项目依赖配置
```

## 快速开始

### 环境要求

- **Node.js**: 16.x 或更高版本
- **PM2**: 用于进程管理
- **操作系统**: Linux (推荐 Ubuntu/Debian)

### 本地开发

1. **克隆项目**
   ```bash
   git clone <repository-url>
   cd HBox_Git/server
   ```

2. **安装依赖**
   ```bash
   npm install
   ```

3. **配置环境**
   ```bash
   cp .env.example .env
   # 编辑 .env 文件配置数据库和其他参数
   ```

4. **启动服务**
   ```bash
   npm start
   # 或者使用 PM2
   pm2 start ecosystem.config.js
   ```

### 生产部署

使用提供的PowerShell部署脚本：

```powershell
# 进入tools目录
cd server/tools

# 一键部署到生产环境
.\deploy-simple.ps1
```

## API 文档

### 固件管理 API

#### 获取固件列表
```http
GET /api/firmwares
```

**响应示例:**
```json
{
  "success": true,
  "data": [
    {
      "id": "firmware-001",
      "name": "HBox固件 v1.0.0",
      "version": "1.0.0",
      "description": "初始版本固件",
      "createTime": "2024-01-01T00:00:00.000Z",
      "slotA": {
        "filePath": "firmware_v1.0.0_slotA.bin",
        "size": 1024000
      },
      "slotB": {
        "filePath": "firmware_v1.0.0_slotB.bin", 
        "size": 1024000
      }
    }
  ]
}
```

#### 上传固件
```http
POST /api/firmwares/upload
Content-Type: multipart/form-data
```

**参数:**
- `name`: 固件名称
- `version`: 版本号
- `description`: 描述信息
- `slotA`: 槽A固件文件
- `slotB`: 槽B固件文件

#### 删除固件
```http
DELETE /api/firmwares/:id
```

### 设备管理 API

#### 设备注册
```http
POST /api/devices/register
Content-Type: application/json
```

**请求体:**
```json
{
  "rawUniqueId": "12345678-87654321-ABCDEF01",
  "deviceId": "A1B2C3D4E5F6G7H8"
}
```

#### 获取设备列表
```http
GET /api/devices
```

### 固件更新 API

#### 检查更新
```http
GET /api/firmware/check-update?deviceId=:deviceId&currentVersion=:version
```

#### 下载固件
```http
GET /api/firmware/download/:firmwareId/:slot
```

## 部署配置

### 环境配置

编辑 `tools/deploy-config.json` 文件：

```json
{
  "environments": {
    "prod": {
      "host": "182.92.72.220",
      "user": "root", 
      "path": "/opt/hbox-server",
      "ssh_key": "E:\\Works\\Ali-cloude\\182.92.72.220\\PC1125.pem",
      "port": 3000,
      "description": "生产环境"
    }
  },
  "default_env": "prod"
}
```

### 环境变量

创建 `.env` 文件：

```env
NODE_ENV=production
PORT=3000
UPLOAD_DIR=./uploads
MAX_FILE_SIZE=50MB
JWT_SECRET=your_jwt_secret_here
LOG_LEVEL=info
```

## 管理工具

### 部署脚本

- **`deploy-simple.ps1`**: 一键部署到生产环境
- **`deploy.ps1`**: 完整部署脚本，支持多环境
- **`service-manager.ps1`**: 服务管理工具

### 服务管理

```powershell
# 查看服务状态
.\service-manager.ps1 status

# 重启服务
.\service-manager.ps1 restart

# 查看日志
.\service-manager.ps1 logs

# 健康检查
.\service-manager.ps1 health
```

### 连接测试

```powershell
# 测试服务器连接
.\test-connection.ps1
```

## 监控和日志

### PM2 管理

```bash
# 查看进程状态
pm2 list

# 查看日志
pm2 logs hbox-firmware-server

# 重启服务
pm2 restart hbox-firmware-server

# 停止服务
pm2 stop hbox-firmware-server
```

### 日志文件

- `logs/out.log`: 标准输出日志
- `logs/err.log`: 错误日志
- `logs/combined.log`: 合并日志

## 安全考虑

### 设备ID验证

系统使用SHA256哈希算法验证设备ID：

```javascript
// 设备ID哈希算法
const salt1 = 0x48426F78;  // "HBox"
const salt2 = 0x32303234;  // "2024"
// ... 详细算法见 firmware.js
```

### 文件上传安全

- 文件类型验证
- 文件大小限制
- 病毒扫描（可选）

### 访问控制

- JWT令牌认证
- 请求频率限制
- CORS配置

## 故障排除

### 常见问题

1. **部署失败**
   - 检查SSH密钥权限
   - 确认服务器网络连接
   - 验证Node.js版本

2. **服务无法启动**
   - 检查端口占用
   - 验证配置文件
   - 查看错误日志

3. **固件上传失败**
   - 检查文件大小限制
   - 验证文件格式
   - 确认存储权限

### 调试模式

```bash
# 启用调试日志
export DEBUG=*
npm start
```

## 开发指南

### 代码结构

- **模块化设计**: 每个功能模块独立
- **错误处理**: 统一的错误处理机制
- **日志记录**: 结构化日志输出
- **配置管理**: 环境变量配置

### 添加新功能

1. 在 `src/` 目录创建新模块
2. 在 `server.js` 中注册路由
3. 更新API文档
4. 添加单元测试

### 测试

```bash
# 运行测试
npm test

# 代码检查
npm run lint
```

## 版本历史

### v1.0.0 (2024-01-01)
- 初始版本发布
- 基础固件管理功能
- 设备注册和认证
- Web管理界面

## 贡献指南

1. Fork 项目
2. 创建功能分支
3. 提交更改
4. 发起 Pull Request

## 许可证

本项目采用 MIT 许可证 - 详见 [LICENSE](LICENSE) 文件

## 联系方式

- 项目维护者: [维护者姓名]
- 邮箱: [邮箱地址]
- 项目地址: [GitHub地址]

---

**注意**: 这是一个生产环境的固件服务器，请确保在生产部署前进行充分的测试。
