# server 协作规则

继承 [根目录规则](../AGENTS.md)。本目录是 Node.js/CommonJS + Express 服务，包含 WebConfig 托管、账户/设备认证、固件和图片资源。

## 按任务定位

| 范围 | 入口 |
|---|---|
| 服务组装与路由 | [server.js](src/server.js)、[action.js](src/action.js) |
| 网页托管、目标与 HTTP 策略 | [hosted-webconfig.js](src/hosted-webconfig.js)、[webconfig-target-policy.js](src/webconfig-target-policy.js)、[http-security.js](src/http-security.js) |
| 用户/管理员认证 | [email-auth.js](src/email-auth.js)、[admin-access.js](src/admin-access.js)、[user-account-store.js](src/user-account-store.js) |
| 设备认证 | [device-auth-v2.js](src/device-auth-v2.js)、[device-auth.js](src/device-auth.js)、[device-account-store.js](src/device-account-store.js) |
| 固件与下载权限 | [firmware.js](src/firmware.js)、[download-access.js](src/download-access.js) |
| 持久化路径 | [server-paths.js](src/server-paths.js) |

## 行为与数据边界

- 用户身份与管理员权限沿用现有登录和来源校验。WebConfig 的固件目录、下载与系统图库采用 [直连设备访问上下文](src/direct-device-access.js)，不索取设备证明或 bearer token；固件签名与目标版本校验仍由各自流程负责。
- 直连设备访问是当前 WebConfig 产品行为；本地 loopback 启动方式和用户登录来源检查仍按 [Web README](../application/www/README.md) 处理。
- 数据目录由 `server-paths.js` 解析；production 要求显式绝对路径。测试使用临时数据库/上传目录，不能清理或迁移真实 `data`、`uploads`、`gallery-assets` 来让测试通过。
- 保留固件签名、目标硬件、版本和访问权限检查；路由或账户变动不构成发布固件、修改真实账户权限或发送邮件的授权。
- 旧 `src/auth.js` 已不存在；不要按过时目录说明新增重复认证层。现有文档与代码冲突时核对实际路由及测试。

## 开发与验证

- 命令在本目录运行，见 [package.json](package.json)：`npm test` 使用 Node 原生测试；定向示例为 `node --test tests/device-auth-v2.test.js`。按修改选择 [tests](tests/) 中的用例。
- 单文件语法检查可用 `node --check src/server.js`；这不验证路由、数据库或认证行为。
- 日常按改动选择对应测试文件：认证选 `device-auth-v2.test.js` / `email-auth.test.js`，托管选 `hosted-webconfig.test.js`，固件选择 OTA 签名/硬件门禁用例；同时覆盖受影响的路由边界。共享认证、存储或路由组装变更再扩大，不能仅凭语法检查交付逻辑修改。
- 定向测试通过且无新改动时不追加同范围全套重跑；测试超时先排查未关闭的服务器、数据库、定时器或子进程，不能自动连接真实服务补测。
- 集成 WebConfig 优先从仓库根目录运行 `python tools/hbox.py web local-serve --port 3001`。直接 `npm start` 使用现有环境和数据路径，开发默认监听可能是 `0.0.0.0`，不要把它当无副作用检查。
- `npm run stop` 当前使用 Unix `pkill`，不适用于 Windows PowerShell；停止自己启动的具体服务进程，不按名称批量终止其他任务的 Node 进程。
