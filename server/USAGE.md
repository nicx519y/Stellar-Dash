# STM32 HBox 固件服务器 - 快速使用指南

## 🚀 快速启动

### Windows用户
```cmd
# 双击启动（推荐）
start.bat

# 或使用命令行
node start.js
```

### Linux/macOS用户
```bash
# 启动服务器
node start.js

# 或
npm start
```

## 🛑 停止服务

### Windows用户
```cmd
# 双击停止
stop.bat

# 或使用命令行
node stop.js
```

### Linux/macOS用户
```bash
# 优雅停止
node stop.js graceful

# 强制停止
node stop.js stop
```

## 📡 服务器地址

- **本地访问**: http://localhost:3000
- **健康检查**: http://localhost:3000/health
- **API根路径**: http://localhost:3000/api

## 📋 主要接口

| 方法 | 路径 | 功能 |
|------|------|------|
| GET | `/health` | 健康检查 |
| GET | `/api/firmwares` | 获取固件列表 |
| POST | `/api/firmwares/upload` | 上传固件包 |
| DELETE | `/api/firmwares/:id` | 删除固件包 |
| GET | `/downloads/:filename` | 下载固件包 |

## 📦 上传固件包

### 使用curl命令
```bash
curl -X POST http://localhost:3000/api/firmwares/upload \
  -F "name=HBox主控固件" \
  -F "version=1.0.0" \
  -F "desc=修复网络连接问题" \
  -F "slotA=@firmware_slot_a.zip" \
  -F "slotB=@firmware_slot_b.zip"
```

### 固件字段说明
- **name**: 固件名称（必需）
- **version**: 版本号（必需）
- **desc**: 描述信息（可选）
- **slotA**: 槽A固件包文件（可选）
- **slotB**: 槽B固件包文件（可选）

**注意**: 至少需要上传一个槽的固件包

## 📂 存储结构

```
server/
├── uploads/                    # 上传的固件包存储目录
├── firmware_list.json          # 固件列表数据文件
└── ...
```

## 🔧 常见问题

### 1. 端口被占用
```bash
# 修改端口（Windows）
set PORT=3001
node start.js

# 修改端口（Linux/macOS）
export PORT=3001
node start.js
```

### 2. 检查服务器状态
```bash
node stop.js status
```

### 3. 重启服务器
```bash
node stop.js restart
```

## 📊 数据格式

### 固件列表响应格式
```json
{
  "success": true,
  "data": [
    {
      "id": "唯一ID",
      "name": "固件名称",
      "version": "版本号",
      "desc": "描述信息",
      "createTime": "创建时间",
      "updateTime": "更新时间",
      "slotA": {
        "downloadUrl": "http://localhost:3000/downloads/文件名",
        "fileSize": 2458123,
        "hash": "SHA256校验和"
      },
      "slotB": {
        "downloadUrl": "http://localhost:3000/downloads/文件名",
        "fileSize": 2458456,
        "hash": "SHA256校验和"
      }
    }
  ],
  "total": 1
}
```

## 🔗 相关文档

- [完整API文档](README.md) - 详细的API接口说明
- [项目主页](../README.md) - STM32 HBox项目总览

---

💡 **提示**: 首次运行时会自动安装依赖包，请确保网络连接正常。 