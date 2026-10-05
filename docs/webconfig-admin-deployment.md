# XORA WebConfig 和 admin 服务端部署

核对日期：2026-10-05。本文按当前源码整理上线所需改动、首次部署、更新和回滚方法。WebConfig 使用 `https://config.st-dash.com`，admin 使用 `https://manager.st-dash.com`。两个站点共用一套静态产物与一个 Node.js 服务，每个站点自己的 `/api/*` 保持同源。

部署使用 Linux、systemd 和 Nginx。2026-10-05 已完成服务器上线、Resend 域名验证；用户随后确认首个管理员注册和登录可用。更换目标或域名时，须同步替换 DNS、Nginx、证书、`domain`、`admin_domain` 与对应的服务器环境配置。网页部署、固件发布与设备烧录是独立操作。

## 阅读入口

- 已有服务器发布网页或后端更新：从 [现有服务器更新速查](#现有服务器更新速查) 开始，复用本机配置。
- 新服务器：依次执行 [部署前准备](#部署前准备) 和第一步至第七步。
- 邮件与账户：[阿里云邮件 DNS 与 Resend](#阿里云邮件-dns-与-resend)、[管理员注册与登录](#第七步初始化管理员并验收)。
- 故障和版本恢复：[日常更新和回滚](#日常更新和回滚)、[运维与常见问题](#运维与常见问题)。

## 服务器部署记录（2026-10-05）

用户授权部署后，已通过 SSH 确认目标为 `182.92.72.220`、Debian 12；两个域名的 A 记录均指向该服务器。新服务使用独立目录 `/opt/xora` 和 loopback 3001，不迁移或覆盖旧服务数据。本机 `.hbox/deploy/config.json` 已设置实际 SSH 密钥路径、3001 端口及 `/opt/xora/runtime/node/bin/node`。

首次部署时保留了 `firmware.st-dash.com` 对应的旧 PM2 服务。用户于 2026-10-05 确认旧服务已无用途并要求停用后，已停止 `/opt/hbox-server/src/server.js`，从 root PM2 自动恢复清单移除 `hbox-firmware-server`，保存空清单、退出对应 root PM2 管理进程，并禁用 `pm2-root.service` 开机恢复。3000 端口已不再监听；随后核对并清理旧目录下两个无子应用的残留 PM2 管理进程，当前进程表中已无 PM2。旧程序、业务数据及 Nginx 站点配置保留。原 PM2 恢复清单备份为 `/root/.pm2/dump-before-old-service-stop-20261005.json`。当前 XORA 服务及 config、manager 两站的版本和健康检查通过。

已初始化专用账户、状态目录、systemd unit 和正式 Nginx 模板；从 Node.js 官方下载并校验 SHA-256 后独立安装 Node.js 22.23.3，没有替换系统 Node.js。两个 Let's Encrypt 证书已签发，当前到期日为 2027-01-02，`certbot.timer` 已启用。`nginx -t` 通过，正式应用已启动并启用开机启动，监听 `127.0.0.1:3001`。

用户确认此前没有正式签名密钥，并授权创建新的 P-256 密钥对，已完成签名/验签自检。私钥保存在本机仓库外的 `XORA-Secrets/firmware-release-private.pem`，只将公钥部署到 `/etc/xora/keys/firmware-release-public.pem`；新固件发布应使用对应私钥，不能把本地开发密钥签发的包当作正式包。本次没有更改设备信任根、构建或烧录固件。邮件 Key 已保存到 `/etc/xora/secrets/resend-api-key`，两个服务端文件均为 `root:xora`、权限 `640`。本机实际密钥路径和公钥指纹记录在被 Git 忽略的 `.hbox/deploy/server-readiness.json`，该文件不包含密钥内容。

准备站点已移至 `/etc/xora/bootstrap-nginx.saved`，正式站点由 `/etc/nginx/conf.d/xora.conf` 链接到 `/etc/xora/nginx.conf`；保留现有 `firmware.st-dash.com` 站点。首次部署版本为 `20261005-031902-6b09a7d4`，使用已校验的 `.hbox/deploy/packages/xora-20261005-031902-6b09a7d4.tar.gz`；部署时已核对包内代码与所选源码、Hosted 产物一致。Linux 原生依赖安装、临时数据库启动预检、生产服务启动与双域名公开检查通过，备份保存为 `/var/backups/xora/data-20261005-051426-ccc13b6d.tar.gz`。后续代码或产物变化后须重新打包，新版本以部署输出和服务器 `current` 为准。现有服务器的更新直接使用实际 `.hbox/deploy/config.json`，不要用通用示例覆盖其端口、运行时和 SSH 设置。

首个管理员 `33618409@qq.com` 已离线预授权。2026-10-05 用户提供的 Resend 控制台截图显示域名及 DKIM、两条发信 CNAME 记录均为 Verified，Enable Sending 已开启；随后用户确认通过正常页面完成注册和管理员登录。线上只读核对同时确认：`xora-server` 为 active，Resend 密钥文件存在，发件地址为 `XORA <no-reply@auth.st-dash.com>`，账户 API 返回 `registrationEnabled: true`。Resend Key 仅有发信权限，不能通过它查询域名状态。

本次验收证据区分如下：脚本和构建由主机检查验证，服务、HTTPS 和配置由线上检查验证，邮件验证与首次管理员登录由用户确认。后台各项写操作、真实固件发布、完整数据恢复和 WebHID 实机操作不在本次验收范围。

## 现有服务器更新速查

本节用于已上线的 `182.92.72.220`。在仓库根目录的 PowerShell 执行，保留本机 `.hbox/deploy/config.json` 的实际 SSH 密钥、`port: 3001` 和 `node: /opt/xora/runtime/node/bin/node`。

```powershell
python server/tools/deploy_xora.py check
```

预检查成功后构建发布包：

```powershell
python server/tools/deploy_xora.py package
```

将输出 `PACKAGE=` 后面的精确路径填入下面的 `--package`；不要直接执行含占位符的命令。构建或预检查失败时先处理失败原因。

```powershell
python server/tools/deploy_xora.py deploy --package ".hbox/deploy/packages/xora-<本次-release-id>.tar.gz"
```

成功输出 `DEPLOYED=` 与 `BACKUP=` 后，记录版本、备份位置和验证结果，再刷新 WebConfig 与 admin 检查变更。网页更新保留独立业务数据目录；已有管理员、Resend DNS 和 TLS 证书沿用现有配置。数据库不兼容变更须在发布前核对迁移与回滚方案。

## 当前架构

```text
config.st-dash.com       manager.st-dash.com
         |                       |
         +--------- Nginx -------+
                    |
             127.0.0.1:3001（当前服务器）
                    |
             server/src/server.js
                    |
        +-----------+----------------+
        |                            |
 Hosted 静态文件                API 和持久化数据
 WebConfig 和 admin          账户、图库、固件发布目录

浏览器通过 WebHID 与用户电脑上的设备直接通信
```

`application/www` 使用 Next.js 静态导出。`npm run build:hosted` 同时生成配置页、邮箱验证页和管理员页面，输出到 `application/www/build/`。服务器不需要运行 `next start`，也不需要单独部署 admin 前端服务。

| 访问路径 | 用途 |
| --- | --- |
| `/global/` | WebConfig 配置页 |
| `/admin/users/` | 管理员账户管理入口 |
| `/admin/firmware/` | 固件管理 |
| `/admin/images/` | 官方图库管理 |
| `/admin/service-tokens/` | 自动化服务令牌 |
| `/auth/verify/` | 邮箱验证和设置密码 |
| `/firmware/releases/` | 不连接设备也可浏览的发布目录 |
| `/api/*` | 同源后端 API |
| `/health` | 进程基础健康检查 |

当前 Hosted 通过 `session.open-direct` 建立加密 WebHID 会话，普通设备连接和公开固件、图库资源不要求服务器设备证明或 permit。账户与管理员权限仍通过邮箱会话校验。旧设备证明接口可以保持 fail-closed；KMS、Redis adapter 不是部署当前直连产品的前置条件。

核对入口：[WebConfig 规则](../application/www/AGENTS.md)、[服务组装](../server/src/server.js)、[静态托管](../server/src/hosted-webconfig.js)、[直连资源上下文](../server/src/direct-device-access.js)。旧 [V2 生产证明文档](WEBCONFIG_V2_PRODUCTION_DEPLOYMENT.md) 描述设备证明子系统，不作为本部署流程的依赖清单；其中硬件保护操作也不属于网页部署。

## 上线需要的改动

| 范围 | 当前问题或已有能力 | 要做的事情 |
| --- | --- | --- |
| 前端 | WebConfig 和 admin 已包含在 Hosted 导出内 | 构建并部署整个 `build/`，保持 API 同源；不需要拆前端 |
| 打包 | 旧 `server/tools/deploy*.ps1` 漏文件 | 新 `deploy_xora.py package` 已完整打包后端、管理员脚本、共享文件和 Hosted 导出 |
| 环境配置 | 旧脚本生成的 `UPLOAD_DIR`、`DB_*`、`JWT_SECRET` 不匹配当前配置入口 | 使用本文真实变量，由 systemd 或明确的 PM2 配置注入 |
| 启动 | 仓库 PM2 配置缺少必需生产变量，并使用额外包装进程 | 推荐直接运行 `src/server.js`；单实例，配置日志与自动重启 |
| 数据 | production 强制三个绝对路径 | 独立配置数据、上传和图库目录，更新版本时不覆盖 |
| 账户 | 未启用邮箱认证时 admin 无法正常登录使用 | 配置 Resend、发信域名、站点 origin；离线预授权首个管理员 |
| 固件验签 | production 强制 P-256 公钥文件 | 部署与实际固件签名密钥对应的公钥，私钥留在离线构建端 |
| Nginx | 现有模板没设置上传大小，旧脚本仍检查公网 3000 | 设置上传限额和代理头；仅代理 loopback，外部使用 HTTPS |
| 缓存和 CSP | Node 已按 HTML 字节生成脚本哈希并区分缓存策略 | 先全部代理给 Node，保留安全头与缓存头；不另加冲突 CSP 或通用 SPA fallback |
| 发布恢复 | 旧脚本原地覆盖版本 | 使用版本目录、固定数据目录、备份和可切换的 `current` 链接 |

当前不需要新增业务接口、修改设备固件或引入 MySQL。SQLite 和旧 JSON 文件仍是当前存储实现；先按单机单 Node 实例部署。多实例、共享限流和存储扩容须另行设计。

两个页面域名的 API 均通过自身域名代理到同一后端，例如 manager 页面调用 `https://manager.st-dash.com/api/auth/*`。不使用浏览器跨域 API 请求，因此保留 `credentials: 'same-origin'` 和 CSP 的 `connect-src 'self'`。Nginx 把 config 上的 `/admin/*` 跳转到 manager；manager 根路径进入 `/admin/users/`，配置页跳回 config。管理员权限继续由 API 校验，域名跳转不代替权限检查。

`USER_AUTH_PUBLIC_ORIGIN` 固定为 config，邮件验证链接指向 config；`USER_AUTH_ALLOWED_ORIGINS` 和 `WEB_CONFIG_ORIGINS` 均精确列出 config、manager 两个来源。账户数据库共享，Cookie 保持 `__Host-`、HttpOnly、Secure 和不含 Domain，各域名分别登录。manager 注册后从邮件进入 config 完成验证，再回 manager 登录；在 config 登录不会自动登录 manager。

## 部署前准备

1. 确认服务器、SSH 账户、域名 DNS、HTTPS 证书，以及现有 3000 端口的进程管理方式。仓库旧配置不能证明这些资源仍在使用。
2. 安装受支持的 Node.js LTS、npm、Nginx、tar、curl。当前线上基线是 Debian 12 / Node.js 22.23.3，已完成 Linux 原生依赖安装和启动验证；新服务器至少满足脚本的 Node.js 22 要求，并以 [Node.js 官方发布表](https://nodejs.org/en/about/previous-releases) 确认支持状态。仓库最低 `18.17.0` 是代码最低要求，不是部署脚本要求。
3. 后端含 `better-sqlite3` 和 Argon2 原生依赖，必须在目标 Linux 上安装，不上传 Windows 的 `node_modules`。预编译依赖不可用时准备 Python 3、make、C/C++ 编译工具。
4. 在 Resend 验证发信域名，例如 `auth.st-dash.com`，按控制台提供的实际 SPF/DKIM 记录配置 DNS；参见 [Resend 域名文档](https://resend.com/docs/dashboard/domains/introduction)。准备可读的 API Key 文件，不把密钥放进前端环境或发布包。
5. 准备固件验签公钥。如果迁移旧服务，保留原验签关系；不要临时生成无关公钥来通过启动。公钥还应与设备实际信任根对应。本地开发 PKI 不能默认当作正式发布密钥。
6. 若迁移已有数据，先记录旧服务真实环境和存储路径，停掉其写入进程后备份完整 data、uploads、gallery-assets。不将本地调试账户、服务令牌或固件草稿默认搬到生产。

当前 lockfile 中 Next.js 为 `15.0.3`。正式发布前应核查依赖通告和实际受影响范围，必要时单独升级并验证；静态导出部署不能直接等同于运行 Next.js SSR 服务。不要用 `npm audit fix --force` 随手改动依赖。

## 脚本入口

统一入口为 [deploy_xora.py](../server/tools/deploy_xora.py)，只使用 Python 标准库和 OpenSSH。Windows 本机需 Python 3.10+、Node/npm、ssh/scp；服务器需 Linux、Python 3.10+、受支持的 Node.js LTS（脚本要求至少 22）、npm、Nginx、systemd、sudo/runuser、tar。SSH 用户需 root 或可非交互使用 sudo 的部署账户。

| 命令 | 行为 |
| --- | --- |
| `package` | 本地构建 Hosted、校验页面和 Mock 隔离，生成发布包、文件清单与 SHA-256；不连接服务器 |
| `package --skip-build` | 复用已有 Hosted 产物，仍校验页面、chunk、Mock 隔离和发布包 |
| `setup` | 远端创建专用账户、目录、环境文件、systemd unit 和 Nginx 模板；不启动服务、不启用 Nginx 站点 |
| `check` | 检查远端运行时、配置一致性、数据权限、密钥可读性、P-256 公钥与 Nginx 语法；不启动应用 |
| `deploy --package <文件>` | 上传并核对包，安装 Linux 依赖，以临时数据库预检，停止托管的 `xora-server`、备份生产数据、切换、启动并检查内外网 |
| `rollback --release <版本> --database-compatible` | 校验并预检保留版本，备份当前数据后切回旧代码；不还原数据库 |

所有命令从仓库根目录执行。`--config <文件>` 如需指定，放在子命令之前。脚本不会读取旧 `deploy-config.json`，旧 `deploy*.ps1` 不用于这条流程。

## 第一步填写部署配置

```powershell
New-Item -ItemType Directory -Force .hbox/deploy | Out-Null
if (Test-Path .hbox/deploy/config.json) { throw "部署配置已存在，请使用现有配置。" }
Copy-Item server/tools/deploy-xora.example.json .hbox/deploy/config.json
notepad .hbox/deploy/config.json
```

配置示例：

```json
{
  "host": "182.92.72.220",
  "user": "root",
  "ssh_port": 22,
  "ssh_key": "",
  "domain": "config.st-dash.com",
  "admin_domain": "manager.st-dash.com",
  "port": 3001,
  "node": "/opt/xora/runtime/node/bin/node",
  "email_from": "XORA <no-reply@auth.st-dash.com>"
}
```

上例按当前服务器的端口和运行时填写；更换服务器时替换真实目标，并确认指定的 Node 路径已安装。`setup` 不负责安装 Node.js，Node 同目录必须安装 npm；服务账户须可访问，不能使用被 systemd `ProtectHome` 屏蔽的个人目录路径。`ssh_key` 留空表示使用 SSH agent 或默认密钥；使用 PEM 时填写本机实际路径。配置文件位于被 Git 忽略的 `.hbox/`，其中不要填写 API Key 或密码。现有服务器已有此文件，首次复制示例的命令只用于新建配置，不覆盖现有配置。

先手动 SSH 一次，通过可信渠道核对主机指纹并完成登录。脚本使用 `StrictHostKeyChecking=yes` 和 `BatchMode=yes`，不会跳过主机校验或提示输入密码。

```powershell
ssh -i "<本机服务器密钥.pem路径>" root@182.92.72.220
```

## 第二步初始化服务器

确认目标 Linux 已安装前述依赖，然后回到本机执行：

```powershell
python server/tools/deploy_xora.py setup
```

这会创建 `xora` 服务账户和以下结构：

```text
/opt/xora/releases/<release-id>/
    server/src/                  后端
    server/scripts/              管理员等离线工具
    server/node_modules/         在 Linux 上安装
    common/uimg-jpeg.cjs          共享图片解析
    webconfig/                   Hosted 导出，包含 admin
    manifest.json                发布文件摘要
    preflight.log                临时启动日志
/opt/xora/current                当前版本链接
/var/lib/xora/data/              数据库、JSON、firmware-release-assets
/var/lib/xora/uploads/           旧固件资源
/var/lib/xora/gallery-assets/    图片资源
/var/lib/xora/npm-cache/         Linux npm 缓存，不进入业务数据备份
/var/backups/xora/               停服务后创建的数据备份
/etc/xora/server.env
/etc/xora/nginx.conf             尚未启用的站点模板
/etc/systemd/system/xora-server.service
```

`setup` 对同配置可以重复执行；已有环境或 unit 内容不同会拒绝覆盖，需人工核对差异。它不会迁移已有 PM2 服务或旧数据，也不会创建管理员。

## 第三步放置公钥和发信密钥

通过你使用的安全运维方式，将以下文件放到服务器：

| 目标文件 | 内容 |
| --- | --- |
| `/etc/xora/keys/firmware-release-public.pem` | 对应实际固件签名密钥的 P-256 公钥 PEM，不能上传私钥 |
| `/etc/xora/secrets/resend-api-key` | 纯 Resend API Key，无变量名、引号或 JSON |

在服务器上设置权限：

```bash
sudo chown root:xora /etc/xora/keys/firmware-release-public.pem /etc/xora/secrets/resend-api-key
sudo chmod 640 /etc/xora/keys/firmware-release-public.pem /etc/xora/secrets/resend-api-key
```

生成的 `server.env` 已包含 `NODE_ENV=production`、loopback 监听、单层代理、同源账户 API、三个独立数据路径、验签公钥路径和 Resend Key 路径。环境由 systemd 加载；应用本身不自动读 `.env`。不得启用本地预览、设备认证 bypass 或本地管理员令牌。

本脚本固定采用单机单实例、一层 Nginx、上述管理目录。迁移旧数据库时，先停止准确的旧写入进程并做一致性备份，再复制完整 data、uploads、gallery-assets 到对应新路径；不要遗漏 data 内的 `firmware-release-assets/`。按实际文件设置 `xora` 读写权限，保留原公钥验签关系。脚本不会自动停止旧 PM2；若其仍占用端口，首次切换会拒绝继续。

## 第四步配置域名和 HTTPS

在阿里云 `st-dash.com` 的解析设置中添加以下网站记录，解析请求来源选择“默认”，TTL 选择 10 分钟。换服务器时先核对新的公网 IP。

| 类型 | 主机记录 | 记录值 |
| --- | --- | --- |
| A | `config` | `182.92.72.220` |
| A | `manager` | `182.92.72.220` |

服务器安全组和防火墙开放 80/443 与受控 SSH；当前应用端口 3001 仅监听 loopback，不开放公网。保留既有服务的端口和站点配置。在 Nginx 中先准备两个域名的 HTTP 站点，再用所选工具申请证书；例如已安装 Certbot 与 Nginx 插件时运行：

```bash
sudo certbot --nginx -d config.st-dash.com
sudo certbot --nginx -d manager.st-dash.com
```

两个域名已有证书则跳过申请。模板分别读取 `/etc/letsencrypt/live/config.st-dash.com/` 和 `/etc/letsencrypt/live/manager.st-dash.com/` 下的 `fullchain.pem` 与 `privkey.pem`，所以上例分别申请两个证书。若使用同一 SAN 证书，需调整模板两个站点的证书路径。确认没有旧配置重复声明同一域名；必要时保存旧配置并停用对应站点。然后在实际 Nginx 包含的目录中启用模板，例如使用 `conf.d/*.conf` 的发行版：

```bash
sudo ln -s /etc/xora/nginx.conf /etc/nginx/conf.d/xora.conf
sudo nginx -t
# 上一条成功后执行
sudo systemctl reload nginx
```

不要同时保留 Certbot 生成的同域站点和新模板造成冲突。首次服务尚未部署时看到 502 属正常准备阶段；如果这是已有线上站点，应安排维护窗口或临时使用独立域名完成迁移。

模板包含 64m 上传上限、完整原始路径代理及同源请求头，并保留后端 CSP、Cookie、安全头与缓存策略。Nginx 默认上传上限为 1m，见 [官方说明](https://nginx.org/en/docs/http/ngx_http_core_module.html#client_max_body_size)；后端继续执行各自的固件和图库限额。CDN、多层代理或不同证书目录须先调整设计，不能直接套用本例。

### 阿里云邮件 DNS 与 Resend

在 Resend 添加发信域名 `auth.st-dash.com`，保持 Enable Sending 开启。当前已验证的阿里云 `st-dash.com` 解析记录如下；这是本次控制台给出的配置，重新创建域名时以新控制台记录为准。

| 类型 | 主机记录 | 记录值 |
| --- | --- | --- |
| TXT | `resend._domainkey.auth` | 从 Resend 的 DKIM Content 完整复制，值以 `p=` 开头 |
| CNAME | `rsend.auth` | `rsend.forge.rmta.net` |
| CNAME | `send.auth` | `send.forge.rmta.net` |

解析请求来源均选择“默认”，TTL 选择 10 分钟。阿里云自动补全 `.st-dash.com`，主机记录填上表的前缀；第一条 CNAME 为 `rsend.auth`。TXT 必须使用复制按钮获取完整内容，截图中的省略内容不能作为记录值。DMARC 属可选配置；已有策略时先核对域名适用范围再调整。本项目只使用发信功能。

在 Resend 点击 **I've added the records**，等待域名及必需记录显示 **Verified**。此操作按 [阿里云 DNS 字段说明](https://help.aliyun.com/en/dns/pubz-add-parsing-record) 和 [Resend 域名说明](https://resend.com/docs/dashboard/domains/introduction) 核对。仅有发信权限的 API Key 无法查询域名状态，验证状态以 Resend 控制台为准。

服务器使用 `/etc/xora/secrets/resend-api-key` 与 `USER_AUTH_EMAIL_FROM="XORA <no-reply@auth.st-dash.com>"`。DNS 验证完成且发信配置未变时可直接使用注册流程；更换 API Key 或服务环境配置后，在维护窗口重启 `xora-server` 并检查日志，使进程加载新配置。

## 第五步预检查和构建

本机执行远端预检查：

```powershell
python server/tools/deploy_xora.py check
```

首次前端依赖未安装时，在 `application/www` 运行 `npm ci`；依赖未变时复用现有安装。检查前端 `.env.local` 使用 Hosted/WebHID 配置和同源 API，不能包含生产密钥。

```powershell
python server/tools/deploy_xora.py package
```

默认执行 `npm run build:hosted`，600 秒超时。成功后显示 `PACKAGE=...`、`SHA256=...` 和 `RELEASE=...`。发布包保存到 `.hbox/deploy/packages/`，采用白名单，只含前后端代码、共享解析文件和静态文件，不含真实数据、密钥、`.env`、Windows `node_modules`。

已有同一源码对应的有效 Hosted 构建时，可用 `package --skip-build`；脚本不会凭此判断源码与产物是否一致，使用者需确认它是所需版本。构建失败时立即退出，不用旧产物继续打包。

## 第六步执行部署

把 `--package` 替换为上一步打印的真实文件路径，不要按名称盲选另一个构建：

```powershell
python server/tools/deploy_xora.py deploy --package ".hbox/deploy/packages/xora-<release-id>.tar.gz"
```

脚本依次执行：

1. 本地验证归档文件清单与摘要，远端检查运行时、环境及权限。
2. 上传到唯一临时目录，核对归档 SHA-256，拒绝越界路径、符号链接、重复条目、额外文件和 Mock 代码。
3. 解包到新的版本目录；旧版本目录拒绝覆盖。
4. 以 `xora` 用户执行 `npm ci --omit=dev`，限时 600 秒，随后把代码所有权收回 root。
5. 用临时数据库和临时 loopback 端口启动候选版本，验证各页面、CSP/WebHID 策略、账户可用性和未登录管理员 API 拒绝访问。此步骤不发邮件、不连接设备、不打开生产数据库。
6. 停止已管理的 `xora-server`，备份 data、uploads、gallery-assets 到 `/var/backups/xora/`，备份限时 600 秒。
7. 原子切换 `current`，启动新版本，验证 loopback 服务，启用开机启动，再验证 config 和 manager 两个公网 HTTPS 返回同一 release，并检查站点跳转。

每个子进程打印 PID、阶段、退出码和耗时；一台服务器同时只允许一个部署操作。SSH 中断或超时不表示远端操作已撤销，应先检查服务、版本、进程与日志，不直接重复部署。失败的上传和版本目录会保留以便诊断，不自动清理历史版本或备份。

## 第七步初始化管理员并验收

在服务器上预授权自己的管理员邮箱：

```bash
cd /opt/xora/current/server
sudo -u xora /opt/xora/runtime/node/bin/node scripts/account-role.js \
    --database /var/lib/xora/data/user_accounts.sqlite3 \
    --email '你的管理员邮箱' --role admin
```

未注册邮箱会得到待消费授权；在正式站点正常注册、收验证邮件并设置密码后成为管理员。已注册邮箱则更新角色。然后在 `https://manager.st-dash.com` 登录，打开 `/admin/users/`、`/admin/firmware/`、`/admin/images/` 检查。部署脚本本身不发送邮件、不创建账户、不发布固件。

上例使用当前服务器的专用 Node；其他服务器替换为部署配置中的 `node` 路径。该命令会修改账户角色，只对已授权邮箱执行；现有管理员更新网页时无需再次运行。

首次使用时，在 manager 右上角登录入口选择注册，填写邮箱与图形验证码，提交后到邮箱打开验证链接并设置密码。邮件固定跳转 config 的 `/auth/verify/`，验证完成后返回 manager 单独登录；两域名的 Cookie 分别保存。当前 `33618409@qq.com` 的注册与管理员登录已由用户确认完成。

## 上线验收

按以下范围逐项记录结果；单个 `/health` 200 不代表整站验收。

1. config 的 `/global/`、`/firmware/releases/` 与 manager 的 `/admin/users/`、`/admin/firmware/`、`/admin/images/`、`/admin/service-tokens/` 可打开；两站 `/health` 与 `/auth/verify/` 正常，刷新无 404，静态 chunk 无缺失。config 管理入口跳转 manager；manager 的“返回 WebConfig”跳转 config。
2. 浏览器无 CSP 拒绝、Mock 标记和跨域 API 错误；HTML/route payload 禁止缓存，`/_next/static/` 采用长期不可变缓存，认证 API 不缓存。
3. 未登录调用 `/api/admin/profile` 返回 401，普通账户在两站均被管理员接口拒绝；管理员可读取管理数据，退出后旧会话失效。非法 origin 的写请求被拒绝。
4. `/api/auth/session` 的 `registrationEnabled` 为 true；人工验证注册、邮件链接指向线上域名、登录退出、Secure/HttpOnly Cookie。使用收件人授权的测试邮箱，不自动向真实用户发信。
5. 公开固件目录返回正确 JSON；没有发布固件时空列表可以通过，不能擅自发布固件来填充验收。
6. 单独授权功能验收时，导入已批准签名包为草稿，核对验签、列表和说明保存；错误签名被拒绝，大文件不被代理误拒绝。导入、发布、删除是不同操作，不以部署任务自动授权发布。
7. Chromium 在 HTTPS 下可由用户选择设备，建立 WebHID 会话、读取配置；更改配置、图片安装、固件升级按各自授权和设备规则单独验收。
8. 重启服务后账户、固件目录、图库保留；部署配置中的应用端口（当前 3001）不对公网开放，不改动既有 PM2 服务。

主机预检建议：前端 `npm run build:hosted`；后端 `node --test tests/hosted-webconfig.test.js tests/email-auth.test.js tests/admin-access.test.js tests/direct-device-access.test.js`。固件管理上线前再覆盖 `firmware-releases.test.js`、OTA 签名、图库相关测试；最终部署包在目标 Linux 环境验证原生依赖和启动。主机检查不能代替线上或设备验收。

## 日常更新和回滚

日常更新执行 `package`，再用其打印的精确路径执行 `deploy --package ...`。无需重复 setup、证书配置和管理员初始化。服务切换有短暂维护窗口，已打开旧网页的用户需刷新；本流程不承诺零停机。脚本保留所有旧版本与备份，清理策略由运维另行决定。

在服务器查看当前版本、保留版本和日志：

```bash
readlink -f /opt/xora/current
ls /opt/xora/releases
sudo systemctl status xora-server --no-pager
sudo journalctl -u xora-server -n 100 --no-pager
```

确认旧代码兼容当前数据库后，在本机执行：

```powershell
python server/tools/deploy_xora.py rollback --release "<previous-release-id>" --database-compatible
```

回滚仍执行旧版本文件校验、临时数据库启动检查、当前数据备份和启动检查。`--database-compatible` 表示操作者已核对 schema 兼容性；临时空数据库预检无法替代此项。回滚只切换代码，不恢复旧数据库，不覆盖上线后的新增账户和操作记录。

| 失败位置 | 脚本处理 |
| --- | --- |
| 包校验、依赖安装、临时启动失败 | 不停止生产服务；保留失败目录及 `preflight.log` |
| 停服务后备份失败 | 不切换版本；原来处于运行状态的服务会重新启动 |
| 新版本本地启动或健康检查失败 | 停止新服务，保留当前链接、旧版本与备份并报告位置；先查数据库迁移和日志，再明确回滚 |
| 本地健康但公网 HTTPS 检查失败 | 保持新服务运行，返回失败；排查 DNS、TLS、Nginx、CDN，不自动回滚 |

SQLite 采用 WAL，不能直接复制运行中的单个数据库文件作完整备份。脚本在服务停止后备份三个业务目录，不自动恢复数据。任何人工恢复都须先确认是否会覆盖更新后的业务数据。

代码包、业务数据和运维配置分别保管：发布包位于本机 `.hbox/deploy/packages/` 与服务器 `/opt/xora/releases/`；脚本备份只包含 `data`、`uploads`、`gallery-assets`。`/etc/xora`、TLS 证书、systemd unit、Resend API Key、离线固件签名私钥和本机 SSH 配置不在业务数据备份中，按安全运维方式另行备份。数据恢复需独立确认目标版本与恢复时间点，并在停止准确写入进程、保留恢复前备份后操作；不能把代码回滚当作数据库恢复。

## 运维与常见问题

以下只读命令在服务器执行：

```bash
sudo systemctl is-active xora-server
sudo journalctl -u xora-server -n 100 --no-pager
sudo nginx -t
readlink -f /opt/xora/current
curl --fail --silent --show-error --max-time 10 http://127.0.0.1:3001/health
curl --fail --silent --show-error --max-time 10 https://config.st-dash.com/api/auth/session
curl --fail --silent --show-error --max-time 10 https://manager.st-dash.com/health
sudo systemctl list-timers certbot.timer --no-pager
sudo certbot certificates
```

`/api/auth/session` 未登录返回 `authenticated: false` 属正常状态，线上应有 `registrationEnabled: true`。`/health` 用于进程可达性，部署版本由发布输出和 `current` 链接核对；静态产物、账户与管理权限仍按上线验收逐项检查。证书到期日是部署时快照，运行中以 `certbot certificates` 为准。日志排障仅提取必要错误，不公开 API Key、Cookie、密码或完整邮件验证链接。

| 现象 | 排查与处理 |
| --- | --- |
| `check` 报 `server.env` 或 unit 不一致 | 对照本机配置与 `/etc/xora/server.env`、systemd unit；`check` 要求与生成模板一致，人工改环境文件后须同步配置并核对，不能盲目重跑 `setup` 覆盖 |
| SSH 认证或主机校验失败 | 核对本机 `ssh_key`、SSH 用户及可信主机指纹，保持 `StrictHostKeyChecking=yes`；新机器需先完成可信 SSH 登录 |
| Node 版本或原生依赖错误 | 核对配置中的 Node/npm 路径与 Node 主版本，Linux 上安装依赖；保留发布阶段日志，不上传 Windows `node_modules` |
| 页面 502 或服务启动失败 | 查看 `xora-server` 状态、journal、`current` 链接和配置端口，再核对 Nginx 回源；本机失败时按脚本记录定位旧版本和备份 |
| 本机健康但公网验证失败 | 核对 A 记录、证书、站点冲突和 80/443；脚本此时保持新服务运行，不自动回滚 |
| 注册入口显示未配置 | 检查 `USER_AUTH_ENABLED`、密钥文件权限、发信配置，以及 `/api/auth/session` 的 `registrationEnabled` |
| Resend 已 Verified 但收不到邮件 | 核对提交注册后的页面错误、服务日志及 Resend 发信记录；检查 Key 的发信域名权限、发件地址、收件邮箱和垃圾邮件；再次发信需由用户主动提交 |
| 邮件链接过期或已使用 | 在原站点重新申请验证邮件；验证链接有效期与一次性限制以 [邮箱认证实现](../server/src/email-auth.js) 为准 |
| config 已登录但 manager 仍未登录 | 两域名各自保存会话，在 manager 单独登录；不通过扩大 Cookie Domain 绕过来源边界 |
| 登录后仍无法进入后台 | 核对实际登录邮箱是否为已预授权管理员及角色；不要向普通账户自动追加管理员权限 |
| 更新后看到旧页面或 chunk 404 | 刷新已打开的网页，核对 `current/webconfig` 与页面引用的静态文件；HTML 和认证 API 保持不缓存，静态 chunk 保持原缓存策略 |
| 上传返回 413 | 核对 Nginx 的 `client_max_body_size 64m` 和后端具体上传限额；调整代理须同时遵守后端门禁 |

## 验证记录

脚本定向测试覆盖打包白名单、损坏/越界/链接/重复归档、Mock 与 chunk 校验、配置注入边界、备份失败、启动失败、公网失败和回滚确认。测试命令：

```powershell
python -m unittest discover -s server/tests -p test_deploy_xora.py -v
```

新目标的 Linux/systemd/Nginx、远端数据迁移、邮件送达及 WebHID 实机行为须在对应目标环境完成验收。本地脚本测试和构建通过不代表已经上线。双域名验证覆盖来源白名单、来源越界拒绝、独立 Cookie、两站代理模板、公开站点检查和跳转规则；本次真实 DNS、证书、Nginx 路由和线上应用已验证，首次管理员注册与登录由用户确认，具体范围见本文服务器部署记录。

2026-10-05 本地完成 42 项定向测试（部署脚本 17 项、账户/管理员/托管 15 项、admin UI 约束 10 项），均通过。Hosted 正式构建与隔离校验通过，构建耗时约 64 秒；构建有 React Hooks lint 警告，无编译错误。生成真实部署包后，在 Windows 上以 `NODE_ENV=production`、临时数据库和测试公钥启动包内后端，验证配置页、admin、邮箱验证页、CSP/WebHID 响应头、账户 API 与未登录管理员 401 均通过；config 和 manager 的验证码请求均为 201，未授权兄弟域名为 403，主机测试没有发送邮件。该检查使用本机 Node.js 20.19.0 的原生依赖；随后在目标 Linux / Node.js 22.23.3 上独立完成依赖安装、隔离预检和正式服务启动验证。
