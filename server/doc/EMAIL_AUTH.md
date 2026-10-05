# WebConfig 邮箱账号部署

邮箱账号与设备身份相互独立：用户可以在未连接设备时注册和登录；设备通过 WebHID 直连加密会话通信。邮箱用户保存在
`HBOX_SERVER_DATA_DIR/user_accounts.sqlite3`，内部主键为 UUIDv4。后续如需让用户
管理设备，应新增显式的 `user_devices(user_uid, device_identity)` 绑定流程，不要按
邮箱、昵称或设备名称自动合并。

邮箱账号角色只有 `admin` 和 `user`。所有普通注册默认是 `user`；管理员必须先通过
本机离线命令按邮箱预授权，不能通过匿名 API 创建。预授权邮箱完成正常的邮箱验证和
密码设置后，注册事务会自动消费授权并创建 `admin` 账号。

## st-dash.com 发信配置

在 Resend 中添加并验证 `auth.st-dash.com`，按控制台给出的实际记录配置 DNS；
DMARC 属可选策略。阿里云字段填写、当前已验证的 TXT/CNAME 记录和首次管理员
流程统一见 [服务端部署指南](../../docs/webconfig-admin-deployment.md)。2026-10-05
用户已确认线上首个管理员注册和登录可用。线上显式配置的发件地址为：

```text
XORA <no-reply@auth.st-dash.com>
```

WebConfig 当前域名为 `https://config.st-dash.com`，管理后台为
`https://manager.st-dash.com`。两站的 `/api/auth/*` 分别通过自己的域名回源，
不是跨域 Cookie API。验证邮件固定指向 config；配置如下：

```text
USER_AUTH_ENABLED=1
USER_AUTH_PUBLIC_ORIGIN=https://config.st-dash.com
USER_AUTH_ALLOWED_ORIGINS=https://config.st-dash.com,https://manager.st-dash.com
USER_AUTH_EMAIL_FROM=XORA <no-reply@auth.st-dash.com>
WEB_CONFIG_ORIGINS=https://config.st-dash.com,https://manager.st-dash.com
RESEND_API_KEY_FILE=/run/secrets/st-dash-resend-api-key
```

省略 `USER_AUTH_ALLOWED_ORIGINS` 时只允许 `USER_AUTH_PUBLIC_ORIGIN`，保留单域名默认行为。
配置多个来源时必须包含 public origin，并且全部属于 `WEB_CONFIG_ORIGINS` 的精确白名单。
账户与权限共享，登录 Cookie 不含 Domain，两个域名分别登录。manager 注册的邮箱
仍在 config 完成邮件验证，再回 manager 登录。[部署脚本](../../docs/webconfig-admin-deployment.md)
使用 `/etc/xora/secrets/resend-api-key`，文件格式要求与下例一致。

`/run/secrets/st-dash-resend-api-key` 只包含 Resend API Key，不要带引号或额外字段。
生产环境强制使用绝对路径的 `RESEND_API_KEY_FILE`，拒绝从
`RESEND_API_KEY` 环境变量读取密钥；非生产环境可临时使用内联变量。

新部署须在 Resend 控制台完成域名验证，再由已授权的收件人通过正常注册流程验证
真实邮件送达。API Key、验证链接、原始会话令牌和密码不得写入日志。

## 接口与流程

```text
POST /api/auth/captcha
POST /api/auth/register/email/request
POST /api/auth/register/email/complete
POST /api/auth/login/email
GET  /api/auth/session
POST /api/auth/logout
```

管理员会话使用同一个邮箱 Cookie，不再支持 Basic Auth、默认管理员密码或在线改密
接口。管理员页面为 `/admin/users/`，该页面与邮箱验证页一样不挂载 HID Provider。
管理员接口包括：

```text
GET    /api/admin/profile
GET    /api/admin/users
PATCH  /api/admin/users/:uid/role
GET    /api/admin/service-tokens
POST   /api/admin/service-tokens
DELETE /api/admin/service-tokens/:id
```

自动化只能使用 `stsvc_` 服务令牌，范围为 `device.manage` 和
`firmware.manage`。数据库只保存 SHA-256 哈希，原始令牌仅在创建时显示一次，默认
90 天过期。`release.py` 从 `HBOX_ADMIN_SERVICE_TOKEN_FILE` 或
`--service-token-file` 指定的文件读取令牌。

注册流程：

1. 浏览器获取 5 分钟有效、一次性使用的自托管图片验证码。
2. 用户提交邮箱和验证码；服务端发送 30 分钟有效的验证链接。
3. 验证页不挂载 `GamepadConfigProvider`，不会请求或占用 HID。
4. 用户设置 10–128 字符密码；服务端使用 Argon2id 保存哈希并签发 7 天会话。

登录同样要求新的一次性图片验证码。生产 Cookie 名为
`__Host-st-dash-user`，带 `HttpOnly; Secure; SameSite=Lax; Path=/`。数据库只保存
验证令牌和会话令牌的 SHA-256 哈希。所有写接口检查精确 `Origin`，并按 IP/邮箱限流。

未配置或 `USER_AUTH_ENABLED` 不为 `1` 时，服务端保持启动，右上角登录入口会明确显示
功能尚未配置；设备连接、设备认证和固件升级不受影响。

## 本地联调

`python tools/hbox.py web local-serve --port 3001` 会显式启用仅限 loopback 的
邮箱认证预览模式。该模式不会向邮箱发送邮件，也不会把验证令牌写入日志；提交注册
信息后，页面会显示本地验证入口，供完整测试设置密码、登录和退出流程。

`USER_AUTH_LOCAL_PREVIEW=1` 在生产环境或非 loopback origin 会直接拒绝启动，不能用于
`st-dash.com` 部署。线上仍需完成域名验证并配置 `RESEND_API_KEY_FILE`。

本地管理员预授权命令：

```powershell
python tools/hbox.py web local-grant-account-role --email 33618409@qq.com --role admin
```

若该邮箱尚未注册，命令会写入 `pending_role_grants`；若已经注册，则直接更新现有
用户角色并记录审计事件。`local-serve` 只为当前进程生成 loopback 专用的
`device.manage` 令牌，不落库也不输出到日志。

## 反向代理检查

- 保持 `/api/auth/*` 与 WebConfig 页面同源，不要把 Cookie 接口跨域部署。
- 代理必须转发 `Origin`、`Cookie` 和 `Set-Cookie`，认证响应禁止缓存。
- `TRUST_PROXY_HOPS` 必须与实际受控代理层数一致，限流才能使用可信客户端 IP。
- `/auth/verify/` 必须回源到导出的静态页面，不能被 SPA fallback 改写成设备页。
