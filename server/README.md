# STM32 HBox 固件管理服务器

基于Node.js的固件包管理服务器，支持固件版本列表管理、文件上传下载和RESTful API。

## 功能特性

- 🚀 **固件版本管理**: 存储和管理固件版本列表
- 📦 **文件上传下载**: 支持槽A和槽B固件包的上传和下载
- 🔒 **数据完整性**: SHA256文件校验，确保数据完整性
- 📊 **RESTful API**: 完整的REST接口，支持CRUD操作
- 💾 **简单存储**: 基于JSON文件的轻量级数据存储
- 🌐 **跨域支持**: 内置CORS支持，便于前端集成

## 环境要求

- Node.js 14.0.0 或更高版本
- npm 包管理器

## 快速开始

### 1. 安装依赖

```bash
# 自动安装（推荐）
node start.js

# 或手动安装
npm install
```

### 2. 启动服务器

```bash
# 使用启动脚本（推荐，包含环境检查）
node start.js

# 或直接启动
npm start
# 或
node server.js
```

### 3. 停止服务器

```bash
# 优雅停止
node stop.js graceful

# 强制停止
node stop.js stop

# 检查状态
node stop.js status

# 重启服务器
node stop.js restart
```

## 服务器配置

| 配置项 | 默认值 | 说明 |
|--------|--------|------|
| 端口 | 3000 | 服务器监听端口 |
| 上传目录 | `./uploads` | 固件包存储目录 |
| 数据文件 | `./firmware_list.json` | 固件列表存储文件 |
| 最大文件大小 | 50MB | 单个文件最大上传大小 |
| 支持格式 | .zip | 允许上传的文件格式 |

### 环境变量

```bash
PORT=3000                           # 服务器端口
SERVER_URL=http://localhost:3000    # 服务器URL（用于生成下载链接）
```

## API 接口

### 1. 健康检查

```http
GET /health
```

**响应:**
```json
{
  "status": "ok",
  "message": "STM32 HBox 固件服务器运行正常",
  "timestamp": "2024-12-01T10:30:00.000Z",
  "version": "1.0.0"
}
```

### 2. 获取固件列表

```http
GET /api/firmwares
```

**响应:**
```json
{
  "success": true,
  "data": [
    {
      "id": "abc123...",
      "name": "HBox主控固件",
      "version": "1.0.0",
      "desc": "修复了网络连接问题",
      "createTime": "2024-12-01T10:30:00.000Z",
      "updateTime": "2024-12-01T10:30:00.000Z",
      "slotA": {
        "originalName": "hbox_firmware_1.0.0_a_20241201_143022.zip",
        "filename": "1733050200000_hbox_firmware_1.0.0_a_20241201_143022.zip",
        "filePath": "1733050200000_hbox_firmware_1.0.0_a_20241201_143022.zip",
        "fileSize": 2458123,
        "downloadUrl": "http://localhost:3000/downloads/1733050200000_hbox_firmware_1.0.0_a_20241201_143022.zip",
        "uploadTime": "2024-12-01T10:30:00.000Z",
        "hash": "sha256_hash_value..."
      },
      "slotB": {
        "originalName": "hbox_firmware_1.0.0_b_20241201_143022.zip",
        "filename": "1733050200001_hbox_firmware_1.0.0_b_20241201_143022.zip",
        "filePath": "1733050200001_hbox_firmware_1.0.0_b_20241201_143022.zip",
        "fileSize": 2458456,
        "downloadUrl": "http://localhost:3000/downloads/1733050200001_hbox_firmware_1.0.0_b_20241201_143022.zip",
        "uploadTime": "2024-12-01T10:30:00.000Z",
        "hash": "sha256_hash_value..."
      }
    }
  ],
  "total": 1,
  "timestamp": "2024-12-01T10:30:00.000Z"
}
```

### 3. 上传固件包

```http
POST /api/firmwares/upload
Content-Type: multipart/form-data
```

**请求参数:**
- `name` (必需): 固件名称
- `version` (必需): 版本号
- `desc` (可选): 描述信息
- `slotA` (文件): 槽A固件包
- `slotB` (文件): 槽B固件包

**注意:** 至少需要上传一个槽的固件包

**响应:**
```json
{
  "success": true,
  "message": "固件上传成功",
  "data": {
    "id": "abc123...",
    "name": "HBox主控固件",
    "version": "1.0.0",
    "desc": "修复了网络连接问题",
    "createTime": "2024-12-01T10:30:00.000Z",
    "updateTime": "2024-12-01T10:30:00.000Z",
    "slotA": { ... },
    "slotB": { ... }
  }
}
```

### 4. 获取固件详情

```http
GET /api/firmwares/:id
```

**响应:**
```json
{
  "success": true,
  "data": {
    "id": "abc123...",
    "name": "HBox主控固件",
    "version": "1.0.0",
    ...
  }
}
```

### 5. 更新固件信息

```http
PUT /api/firmwares/:id
Content-Type: application/json
```

**请求体:**
```json
{
  "name": "新的固件名称",
  "version": "1.0.1",
  "desc": "更新描述"
}
```

**响应:**
```json
{
  "success": true,
  "message": "固件信息更新成功",
  "data": { ... }
}
```

### 6. 删除固件包

```http
DELETE /api/firmwares/:id
```

**响应:**
```json
{
  "success": true,
  "message": "固件删除成功",
  "data": {
    "id": "abc123...",
    "name": "HBox主控固件",
    "version": "1.0.0"
  }
}
```

### 7. 下载固件包

```http
GET /downloads/:filename
```

直接下载固件包文件。

## 数据结构

### 固件对象结构

```json
{
  "id": "string",                    // 唯一标识符
  "name": "string",                  // 固件名称
  "version": "string",               // 版本号
  "desc": "string",                  // 描述信息
  "createTime": "ISO8601",           // 创建时间
  "updateTime": "ISO8601",           // 更新时间
  "slotA": {                         // 槽A固件包信息
    "originalName": "string",        // 原始文件名
    "filename": "string",            // 服务器文件名
    "filePath": "string",            // 文件路径
    "fileSize": "number",            // 文件大小（字节）
    "downloadUrl": "string",         // 下载URL
    "uploadTime": "ISO8601",         // 上传时间
    "hash": "string"                 // SHA256校验和
  },
  "slotB": {                         // 槽B固件包信息（结构同slotA）
    ...
  }
}
```

### 数据存储文件结构

```json
{
  "firmwares": [                     // 固件列表
    {
      // 固件对象...
    }
  ],
  "lastUpdate": "ISO8601"            // 最后更新时间
}
```

## 使用示例

### 使用curl上传固件

```bash
# 上传槽A和槽B固件包
curl -X POST http://localhost:3000/api/firmwares/upload \
  -F "name=HBox主控固件" \
  -F "version=1.0.0" \
  -F "desc=修复了网络连接问题" \
  -F "slotA=@hbox_firmware_1.0.0_a_20241201_143022.zip" \
  -F "slotB=@hbox_firmware_1.0.0_b_20241201_143022.zip"

# 只上传槽A固件包
curl -X POST http://localhost:3000/api/firmwares/upload \
  -F "name=测试固件" \
  -F "version=1.0.1" \
  -F "slotA=@test_firmware.zip"
```

### 使用curl获取固件列表

```bash
curl http://localhost:3000/api/firmwares
```

### 使用curl删除固件

```bash
curl -X DELETE http://localhost:3000/api/firmwares/abc123...
```

## 错误处理

服务器返回标准的HTTP状态码和错误信息：

| 状态码 | 说明 |
|--------|------|
| 200 | 请求成功 |
| 400 | 请求参数错误 |
| 404 | 资源不存在 |
| 500 | 服务器内部错误 |

**错误响应格式:**
```json
{
  "success": false,
  "message": "错误描述",
  "error": "详细错误信息"
}
```

## 目录结构

```
server/
├── package.json              # 项目配置和依赖
├── server.js                 # 主服务器文件
├── start.js                  # 启动脚本
├── stop.js                   # 停止脚本
├── README.md                 # 使用说明
├── uploads/                  # 上传文件存储目录（自动创建）
├── firmware_list.json        # 固件列表数据文件（自动生成）
└── node_modules/             # 依赖包目录（npm install后生成）
```

## 安全考虑

- 文件大小限制：默认50MB
- 文件类型限制：只允许.zip文件
- 文件名安全：自动生成唯一文件名，避免路径注入
- CORS配置：默认允许所有来源，生产环境请根据需要配置

## 故障排除

### 常见问题

1. **端口被占用**
   ```
   Error: listen EADDRINUSE: address already in use :::3000
   ```
   解决：更改端口或停止占用端口的程序
   ```bash
   export PORT=3001
   node start.js
   ```

2. **权限问题**
   ```
   Error: EACCES: permission denied
   ```
   解决：确保有写入权限或使用适当的用户运行

3. **依赖安装失败**
   ```
   npm install failed
   ```
   解决：检查网络连接或使用国内镜像
   ```bash
   npm config set registry https://registry.npmmirror.com
   npm install
   ```

### 日志查看

服务器运行时会在控制台输出详细日志，包括：
- 启动信息
- 请求日志
- 错误信息
- 文件操作日志

## 开发与扩展

### 添加新功能

1. 在`server.js`中添加新的路由
2. 更新`FirmwareStorage`类以支持新的数据操作
3. 更新API文档

### 自定义配置

修改`server.js`中的`config`对象来自定义配置：

```javascript
const config = {
    uploadDir: path.join(__dirname, 'uploads'),
    dataFile: path.join(__dirname, 'firmware_list.json'),
    maxFileSize: 50 * 1024 * 1024,
    allowedExtensions: ['.zip'],
    serverUrl: process.env.SERVER_URL || `http://localhost:${PORT}`
};
```

## 许可证

MIT License 