# XORA 服务端

## 项目简介

XORA 服务端提供 Hosted WebConfig、管理员后台、邮箱账号，以及固件和图片资源的管理与分发。设备通信与手柄协议由固件实现，服务器不替代设备侧验证。

## 功能特性

- 🔧 **固件管理**: 支持固件上传、版本控制、批量删除
- 📱 **设备注册**: 自动设备ID验证和注册
- 🔄 **OTA更新**: 支持设备在线固件更新
- 🧲 **轴体映射库**: 按产品、PCB、硬件版本发布不可变 ADC 曲线版本
- 🎮 **多协议支持**: PS4、PS Classic、Switch、Xbox One、XInput等
- 🔐 **访问控制**: 邮箱会话、管理员角色与 scoped 服务令牌；WebConfig 使用设备加密直连。V2 设备证明与旧 legacy weak 兼容属于独立子系统
- ✉️ **邮箱账号**: 独立 UUID 用户、邮箱验证、Argon2id 密码与 7 天会话
- 📊 **状态监控**: 实时服务状态和日志监控
- 🌐 **Web界面**: 现代化的Web管理界面

## 系统架构

```
server/
├── src/                    # 核心源代码
│   ├── server.js          # 主服务器文件
│   ├── firmware.js        # 固件管理模块
│   ├── admin-access.js    # 邮箱管理员与 scoped 服务令牌
│   ├── action.js          # 动作处理模块
│   ├── device-auth.js     # V1 legacy weak 兼容
│   ├── device-auth-v2.js  # V2 设备证明与会话授权
│   ├── device-account-store.js # 设备身份映射
│   ├── email-auth.js      # 邮箱注册、登录、验证码与 Resend
│   ├── switch-mappings.js # 轴体 ADC 映射目录、版本和发布接口
│   └── user-account-store.js # 独立邮箱用户数据库
├── data/                   # 数据存储
│   ├── firmware_list.json # 固件列表
│   ├── device_ids.json    # 设备ID数据库
│   ├── accounts.sqlite3   # 设备身份映射
│   ├── switch_mappings.sqlite3 # 轴体映射目录及不可变版本
│   └── user_accounts.sqlite3 # 邮箱账号、角色与服务令牌
├── tools/                  # 部署和管理工具
│   ├── deploy_xora.py     # 当前 WebConfig / admin 部署入口
│   ├── deploy-xora.example.json # 新部署配置示例
│   ├── service-manager.ps1 # 服务管理脚本
│   └── deploy-config.json # 旧服务配置，当前部署不使用
├── uploads/               # 固件文件存储
└── package.json          # 项目依赖配置
```

## 快速开始

### 环境要求

- **Node.js**: `package.json` 声明最低 18.17；当前服务器已验证的独立运行时为 22.23.3，原生依赖必须在目标平台/Node 版本下安装
- **进程管理**: 当前 WebConfig / admin 使用 systemd `xora-server`；原固件服务继续由 PM2 管理
- **操作系统**: Linux (推荐 Ubuntu/Debian)

V2 密钥配置、wire API、部署门禁和吊销策略见
[DEVICE_AUTH_V2.md](./DEVICE_AUTH_V2.md)。完整的制造身份与生产部署门禁见
[DEVICE_IDENTITY_PROVISIONING.md](../../docs/DEVICE_IDENTITY_PROVISIONING.md)
和
[WEBCONFIG_V2_PRODUCTION_DEPLOYMENT.md](../../docs/WEBCONFIG_V2_PRODUCTION_DEPLOYMENT.md)。
`st-dash.com` 邮箱发信与账号部署见 [EMAIL_AUTH.md](./EMAIL_AUTH.md)。
轴体 ADC 映射库的数据模型、认证和接口见
[SWITCH_MAPPINGS.md](./SWITCH_MAPPINGS.md)。

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
   ```

### 生产部署

当前 WebConfig（config.st-dash.com）和 admin（manager.st-dash.com）部署请使用
[XORA 服务端部署方案](../../docs/webconfig-admin-deployment.md)。
新入口为 `python server/tools/deploy_xora.py`（从仓库根目录执行），提供
`package`、`setup`、`check`、`deploy` 和 `rollback`；配置示例为
[`deploy-xora.example.json`](../tools/deploy-xora.example.json)。
现有 `tools/deploy*.ps1` 属于旧服务入口，不作为当前 WebConfig / admin 上线流程。
当前部署包含完整静态产物、共享图片解析文件和必需生产配置，应用仅监听 loopback。
旧 V2 设备证明部署要求须与当前 WebHID 直连产品路径区分。

## 当前生产访问与配置

| 入口 | 地址 / 配置 |
| --- | --- |
| WebConfig | `https://config.st-dash.com` |
| 管理后台 | `https://manager.st-dash.com` |
| 邮件发信域名 | `auth.st-dash.com`，Resend 已验证 |
| 本机部署配置 | `.hbox/deploy/config.json`，被 Git 忽略 |
| 服务进程 | `xora-server.service`，专用 `xora` 账户 |
| 应用监听 | `127.0.0.1:3001`，通过 Nginx 提供 HTTPS |
| Node.js | `/opt/xora/runtime/node/bin/node` |
| 服务环境 | `/etc/xora/server.env`，由 systemd 加载 |
| 当前代码 | `/opt/xora/current` 指向保留的 release 目录 |
| 业务数据 | `/var/lib/xora/data`、`uploads`、`gallery-assets` |
| 更新备份 | `/var/backups/xora`，版本切换前停服务备份 |

2026-10-05 已完成双域名上线与 Resend DNS 验证，用户确认首个管理员注册和登录可用。
配置文件、密钥文件及实际运维路径按 [部署指南](../../docs/webconfig-admin-deployment.md)
管理；新服务沿用独立数据目录，原 `firmware.st-dash.com` 的 PM2 服务继续保留。

## 部署与运维入口

从仓库根目录运行 `python server/tools/deploy_xora.py`。首次部署依次配置 SSH、
运行时、`setup`、公钥和发信密钥、DNS/TLS、`check`、`package`、`deploy` 与管理员
初始化，详见 [完整部署步骤](../../docs/webconfig-admin-deployment.md)。

已有服务器更新时，复用 `.hbox/deploy/config.json`，逐步执行 `check`、`package`，
再把本次输出的精确 `PACKAGE=` 路径传给 `deploy --package`。回滚使用
`rollback --release <保留版本> --database-compatible`，须先核对数据库兼容性；
代码回滚保留当前业务数据，不还原旧数据库。

`tools/deploy*.ps1`、`deploy-config.json` 与 PM2 管理脚本属于旧服务入口，
当前 WebConfig / admin 使用新的 Python 脚本与 systemd 流程。

## 监控和日志

在服务器执行只读检查：

```bash
sudo systemctl status xora-server --no-pager
sudo journalctl -u xora-server -n 100 --no-pager
readlink -f /opt/xora/current
curl --fail --silent --show-error --max-time 10 https://config.st-dash.com/health
curl --fail --silent --show-error --max-time 10 https://manager.st-dash.com/health
```

双域名登录 Cookie 分别保存。邮箱验证链接固定进入 config，完成注册后回 manager
单独登录；详情见 [EMAIL_AUTH.md](EMAIL_AUTH.md)。服务日志、DNS/TLS、邮件与
版本切换的排障见 [部署指南](../../docs/webconfig-admin-deployment.md)。
原 `firmware.st-dash.com` 的日志仍由其 PM2 进程管理，不用它判断 `xora-server` 状态。

## 安全考虑

### V2 设备真实性

V2 使用制造 CA 签发的固定二进制设备证书、每次启动 Boot Attestation、一次性
challenge 和由在线 KMS/HSM 签发的短期 scoped permit。`deviceId` 由设备公钥
SHA-256 的前 128 bit 派生，它本身不是秘密，也不能单独作为认证凭据。

当前仓库的本地 PEM signer、JSON 设备库和进程内 challenge/token store 只适合
单进程开发。独立部署 V2 设备证明时必须使用不可导出的 KMS key、共享 Redis 原子消费和事务型设备
策略/吊销数据库；该子系统依赖不可用时保持 fail-closed。这些要求不是当前直连站点的上线前置条件。

### V1 legacy weak

旧版公开 32 位哈希只为已出货 V1 设备保留，标记为 `legacy_weak`。它不能证明
设备持有不可复制的秘密，不得用于 V2 API、受保护下载或“正版设备”宣传，也
不得作为 V2 服务不可用时的回退。

### 文件上传安全

- 文件类型验证
- 文件大小限制
- 固件签名、目标与版本门禁以 `src/firmware.js` 为准；本仓库未提供通用病毒扫描集成

### 访问控制

- 邮箱 Cookie 会话、管理员角色与 scoped 服务令牌（非 JWT），实现见 `src/email-auth.js` / `src/admin-access.js`
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

### 调试与诊断

本地启动使用 `npm start`，环境与持久化目录按 [服务端规则](../AGENTS.md) 核对。生产日志使用上文 systemd / journal 入口；不将通用 `DEBUG=*` 当作本服务已实现的日志开关。

## 开发指南

### 代码结构

- **模块化设计**: 每个功能模块独立
- **错误处理**: 统一的错误处理机制
- **日志记录**: 结构化日志输出
- **配置管理**: 环境变量配置

### 添加新功能

1. 在 `src/` 目录创建新模块
2. 在 `src/server.js` 中注册路由
3. 更新API文档
4. 添加单元测试

### 测试

```bash
# 运行测试
npm test

# 单文件语法检查；不替代行为测试
node --check src/server.js
```

`package.json` 未定义 `lint` 脚本。测试按受影响路由和模块选择，环境及外部依赖要求见 [服务端规则](../AGENTS.md)。

## 贡献指南

1. Fork 项目
2. 创建功能分支
3. 提交更改
4. 发起 Pull Request

## 许可证

`package.json` 声明 `license: MIT`，但当前仓库未提供服务端或仓库级许可证正文；发布前需由维护者补齐相应文件，不能将第三方库许可证当作项目许可证。

---

**注意**: 这是一个生产环境的固件服务器，请确保在生产部署前进行充分的测试。
