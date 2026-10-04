# XORA WebConfig 和 admin 服务端部署

核对日期：2026-10-05。本文按当前源码整理上线所需改动、首次部署、更新和回滚方法。推荐将 WebConfig、admin 和 API 放在同一个 HTTPS origin，使用一套静态产物和一个 Node.js 服务。

示例假设 Linux 服务器使用 systemd、Nginx，域名沿用 `firmware.st-dash.com`；服务器地址和操作系统尚未实地确认。实际域名不同，须同步替换 DNS、Nginx、证书、`DOMAIN_NAME`、`SERVER_URL`、`DOMAIN_URL`、`USER_AUTH_PUBLIC_ORIGIN`、`WEB_CONFIG_ORIGINS`。部署脚本已实现；本次只做本地主机验证，没有执行远端部署、发送邮件或操作设备。

## 当前架构

```text
浏览器访问 https://firmware.st-dash.com
                    |
                  Nginx
                    |
             127.0.0.1:3000
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

前后端分域不适合作为首次上线方案。当前 `fetch` 使用相对 API 路径和 `credentials: 'same-origin'`，CSP 的 `connect-src` 为本站；只改 CORS 或域名变量不足以完成跨域部署。

## 部署前准备

1. 确认服务器、SSH 账户、域名 DNS、HTTPS 证书，以及现有 3000 端口的进程管理方式。仓库旧配置不能证明这些资源仍在使用。
2. 安装受支持的 Node.js LTS、npm、Nginx、tar、curl。建议先用最新 Node.js 22 LTS 补丁版建立本项目基线；以 [Node.js 官方发布表](https://nodejs.org/en/about/previous-releases) 确认支持状态。仓库最低 `18.17.0` 是代码最低要求，不是新部署推荐版本。本次本地主机测试使用 Node.js 20.19.0，尚未验证目标 Linux/Node.js 22 组合。
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
| `deploy --package <文件>` | 上传并核对包，安装 Linux 依赖，以临时数据库预检，停旧服务、备份生产数据、切换、启动并检查内外网 |
| `rollback --release <版本> --database-compatible` | 校验并预检保留版本，备份当前数据后切回旧代码；不还原数据库 |

所有命令从仓库根目录执行。`--config <文件>` 如需指定，放在子命令之前。脚本不会读取旧 `deploy-config.json`，旧 `deploy*.ps1` 不用于这条流程。

## 第一步填写部署配置

```powershell
New-Item -ItemType Directory -Force .hbox/deploy | Out-Null
Copy-Item server/tools/deploy-xora.example.json .hbox/deploy/config.json
notepad .hbox/deploy/config.json
```

配置示例：

```json
{
  "host": "182.92.72.220",
  "user": "root",
  "ssh_port": 22,
  "ssh_key": "E:/你的目录/服务器密钥.pem",
  "domain": "firmware.st-dash.com",
  "port": 3000,
  "node": "/usr/bin/node",
  "email_from": "XORA <no-reply@auth.st-dash.com>"
}
```

示例服务器并未核实；替换为真实目标。`node` 填服务器 `command -v node` 的绝对路径，同目录必须安装 npm；服务账户须可访问，不能使用被 systemd `ProtectHome` 屏蔽的个人目录路径。`ssh_key` 留空表示使用 SSH agent 或默认密钥。配置文件位于被 Git 忽略的 `.hbox/`，其中不要填写 API Key 或密码。

先手动 SSH 一次，通过可信渠道核对主机指纹并完成登录。脚本使用 `StrictHostKeyChecking=yes` 和 `BatchMode=yes`，不会跳过主机校验或提示输入密码。

```powershell
ssh -i "E:/你的目录/服务器密钥.pem" root@你的服务器
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

DNS 指向服务器，开放所需 80/443 与受控 SSH，3000 不对公网开放。在 Nginx 中先准备该域名的 HTTP 站点，再用所选工具申请证书；例如已安装 Certbot 与 Nginx 插件时运行：

```bash
sudo certbot --nginx -d firmware.st-dash.com
```

已有证书则跳过申请。模板默认读取 `/etc/letsencrypt/live/<域名>/fullchain.pem` 和 `privkey.pem`。确认没有旧配置重复声明同一域名；必要时保存旧配置并停用对应站点。然后在实际 Nginx 包含的目录中启用模板，例如使用 `conf.d/*.conf` 的发行版：

```bash
sudo ln -s /etc/xora/nginx.conf /etc/nginx/conf.d/xora.conf
sudo nginx -t
# 上一条成功后执行
sudo systemctl reload nginx
```

不要同时保留 Certbot 生成的同域站点和新模板造成冲突。首次服务尚未部署时看到 502 属正常准备阶段；如果这是已有线上站点，应安排维护窗口或临时使用独立域名完成迁移。

模板包含 64m 上传上限、完整原始路径代理及同源请求头，并保留后端 CSP、Cookie、安全头与缓存策略。Nginx 默认上传上限为 1m，见 [官方说明](https://nginx.org/en/docs/http/ngx_http_core_module.html#client_max_body_size)；后端继续执行各自的固件和图库限额。CDN、多层代理或不同证书目录须先调整设计，不能直接套用本例。

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
7. 原子切换 `current`，启动新版本，验证 loopback 服务，启用开机启动，再验证公网 HTTPS 返回同一 release。

每个子进程打印 PID、阶段、退出码和耗时；一台服务器同时只允许一个部署操作。SSH 中断或超时不表示远端操作已撤销，应先检查服务、版本、进程与日志，不直接重复部署。失败的上传和版本目录会保留以便诊断，不自动清理历史版本或备份。

## 第七步初始化管理员并验收

在服务器上预授权自己的管理员邮箱：

```bash
cd /opt/xora/current/server
sudo -u xora node scripts/account-role.js \
    --database /var/lib/xora/data/user_accounts.sqlite3 \
    --email '你的管理员邮箱' --role admin
```

未注册邮箱会得到待消费授权；在正式站点正常注册、收验证邮件并设置密码后成为管理员。已注册邮箱则更新角色。然后打开 `/admin/users/`、`/admin/firmware/`、`/admin/images/` 检查。部署脚本本身不发送邮件、不创建账户、不发布固件。

## 上线验收

按以下范围逐项记录结果；单个 `/health` 200 不代表整站验收。

1. `/health`、`/global/`、`/admin/users/`、`/admin/firmware/`、`/admin/images/`、`/admin/service-tokens/`、`/auth/verify/`、`/firmware/releases/` 可打开；刷新不出现 404，静态 chunk 无缺失。
2. 浏览器无 CSP 拒绝、Mock 标记和跨域 API 错误；HTML/route payload 禁止缓存，`/_next/static/` 采用长期不可变缓存，认证 API 不缓存。
3. 未登录调用 `/api/admin/profile` 返回 401，普通账户被管理员接口拒绝；管理员可读取管理数据，退出后旧会话失效。非法 origin 的写请求被拒绝。
4. `/api/auth/session` 的 `registrationEnabled` 为 true；人工验证注册、邮件链接指向线上域名、登录退出、Secure/HttpOnly Cookie。使用收件人授权的测试邮箱，不自动向真实用户发信。
5. 公开固件目录返回正确 JSON；没有发布固件时空列表可以通过，不能擅自发布固件来填充验收。
6. 单独授权功能验收时，导入已批准签名包为草稿，核对验签、列表和说明保存；错误签名被拒绝，大文件不被代理误拒绝。导入、发布、删除是不同操作，不以部署任务自动授权发布。
7. Chromium 在 HTTPS 下可由用户选择设备，建立 WebHID 会话、读取配置；更改配置、图片安装、固件升级按各自授权和设备规则单独验收。
8. 重启服务后账户、固件目录、图库保留；公网 3000 不可直接访问。

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

## 验证记录

脚本定向测试覆盖打包白名单、损坏/越界/链接/重复归档、Mock 与 chunk 校验、配置注入边界、备份失败、启动失败、公网失败和回滚确认。测试命令：

```powershell
python -m unittest discover -s server/tests -p test_deploy_xora.py -v
```

实际 Linux/systemd/Nginx、远端数据迁移、邮件送达及 WebHID 实机行为须在目标环境完成验收。本地脚本测试和构建通过不代表已经上线。

2026-10-05 本地完成 Hosted 正式构建与隔离校验，构建耗时约 44 秒；构建有 React Hooks lint 警告，无编译错误。生成真实部署包后，在 Windows 上以 `NODE_ENV=production`、临时数据库和测试公钥启动包内后端，验证配置页、admin、邮箱验证页、CSP/WebHID 响应头、账户 API 与未登录管理员 401 均通过。此检查复用本机 Node.js 20.19.0 的原生依赖，未替代目标 Linux/Node.js 22+ 上的安装和运行验证。
