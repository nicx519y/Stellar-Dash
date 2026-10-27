# WebConfig / admin CDN 部署

生产默认采用现有服务器源站与阿里云 CDN。config / manager 继续提供网页入口、路由和 API；公开脚本、样式、字体与界面图片使用 assets 域名。系统图库继续沿用 [图库 CDN](gallery-cdn.md)。本流程不需要 OSS，也不改变 WebHID 来源、登录 Cookie 或设备固件。

## 日常发布

仓库根目录执行：

```sh
python server/tools/deploy_xora.py check
python server/tools/deploy_xora.py package
python server/tools/deploy_xora.py deploy --package .hbox/deploy/packages/xora-<发布编号>.tar.gz
```

`package` 默认读取 `.hbox/deploy/config.json`（可用 `--config` 指定），构建 Hosted 并生成独立的 32 位十六进制资源版本。生产配置同时保留：

```json
{
  "gallery_cdn_origin": "https://assets.st-dash.com",
  "web_cdn_origin": "https://assets.st-dash.com"
}
```

上例为现有部署配置中的两个字段，其他 SSH、站点、端口和邮件字段继续保留。`setup` 不会覆盖已有环境配置；首次增加 CDN 配置时先备份并核对生成模板，再同步服务器 `/etc/xora/server.env`。服务器初始化、密钥、账户和业务数据维护见 [服务端部署指南](webconfig-admin-deployment.md)。CDN HTTPS 证书自动续期沿用 [现有方案](gallery-cdn.md#免费证书与自动同步)，不扩大证书 RAM 用户权限。

部署入口传入 `NEXT_PUBLIC_WEB_ASSET_BASE=https://assets.st-dash.com/cdn/web/<构建版本>`；Next `assetPrefix` 负责 JS/CSS 和打包字体，public 图片及图标字体通过 `lib/public-asset.ts` 生成版本地址。`build/cdn-assets.json` 记录来源和版本。普通本地 / Mock 构建不使用 CDN；`--skip-build` 只允许复用与目标 CDN 配置一致、完整通过校验的 Hosted 产物。

## 资源与缓存边界

| 内容 | 访问和缓存 |
| --- | --- |
| Next JS/CSS、WASM、打包字体、public/fonts、public/images | `https://assets.st-dash.com/cdn/web/<版本>/...`；一年不可变缓存 |
| 已发布官方图库 | `/cdn/gallery/<图片ID>/<版本>/...`；5 分钟，遵循源站 |
| 已发布且可安装的固件升级包 | `/cdn/firmware/<发布ID>/<包SHA-256>/release.zip`；60 秒，遵循源站 |
| HTML、Next 路由数据 | config / manager，同源 `no-store` |
| 登录、管理、上传、图库列表、个人图片等 API | 保持原有同源权限和缓存策略，不进入公共 CDN 资源清单 |
| `webhid-network-trace-sw.js` | 保持同源，满足浏览器 service worker 的来源要求 |

阿里云域名、源站、Host、SNI、证书沿用 assets 当前配置，新增目录 `/cdn/web/`：缓存一年、优先遵循源站、忽略源站不缓存标头关闭，权重 90；默认 `/` 保持 0 秒。缺失资源返回 `no-store`。不要将整个域名设置为长期缓存，也不要过滤会影响版本或图库海报的 URL 参数。

固件分发目录 `/cdn/firmware/` 使用 60 秒缓存，优先遵循源站、不忽略不缓存标头，权重 90；不能沿用网页资源的一年缓存。未命中时源站重新检查发布状态、可安装契约、整包摘要及签名，失败返回 `no-store`。已缓存的公开包在下架后可能仍可通过原 CDN 地址访问至缓存过期；目录和详情 API 立即隐藏，下发设备前继续回源检查发布状态。紧急停止公开分发时另行在阿里云刷新对应 URL，不能宣称源站下架已清除所有边缘副本。

后台上传入口、临时文件、草稿及发布审核仍位于原服务器。正式包保存在 `/var/lib/xora/data/firmware-release-assets/<包SHA-256>.zip`，CDN 按需回源缓存，不需要改为上传 OSS，也不需要重复上传已有发布包。`WEB_CONFIG_CDN_ORIGIN` 同时启用这一公开分发入口；只允许当前直连公开目录模式，设备鉴权模式不能开启公共固件路由。前端只接受与构建 CDN 来源、发布 ID 和摘要完全匹配的地址，匿名跨域读取，不携带 Cookie 或设备认证头，不跟随重定向；整包 SHA-256、签名、内部组件摘要及硬件/安装检查继续执行。旧版页面和普通/Mock 部署保留原下载 API。

源站资源位于 `/opt/xora/web-assets/<版本>/`，与 `/opt/xora/current` 的切换独立。发布工具从完整性清单挑选公开资源、核对每个文件 SHA-256，并原子发布；同一资源版本内容不可覆盖。资源接口只响应清单中存在的文件；HTML、配置、源映射与业务数据不会被该入口提供。CORS 允许公开无凭据读取，CSP 仅增加配置的精确 CDN 来源，脚本仍使用原来的内联哈希与执行限制。

## 验证、更新和回滚

发布先验证归档、构建变体、CDN 来源和清单，再发布静态资源并启动隔离候选服务。切换前备份业务数据；切换后检查两个网页入口、API、安全头，并逐个从 CDN 下载公开资源核对摘要及跨域响应头。缺失资源检查与缓存命中可在线补查。CDN 校验未通过不能报告发布完成；现有部署失败处理保留服务状态和日志，按具体失败修复。

旧版本资源自动保留，供已打开页面的懒加载和回滚使用。首次迁移同时将旧版 `/_next/static/` 文件保留到 `web-assets/legacy`，源站在新包缺少对应文件时使用同源后备路径。不要随着 current 切换删除旧目录，也不要覆盖同一版本文件。普通更新不需要手动刷新网页资源缓存：新版本 URL 独立，HTML 实时获取新引用。缓存清理和旧目录回收需要独立制定保留策略。

回滚沿用 `deploy_xora.py rollback --release <发布编号> --database-compatible`，先核对数据库兼容；工具复核原始发布包文件与资源清单，再验证回滚页面及 CDN。配置 CDN 后禁止直接回滚到不支持 CDN 资源入口的旧后端；如需恢复这类历史界面，应保留 CDN 源站能力并生成兼容发布包，避免已打开的新页面无法回源。不要恢复旧数据库或修改设备作为网页回滚的一部分。

定向主机检查：

```sh
python -m unittest discover -s server/tests -p "test_deploy*.py"
node --test server/tests/web-assets.test.js server/tests/hosted-webconfig.test.js server/tests/gallery-cdn.test.js
```

前端目录执行 `npm run typecheck` 及 `node -r sucrase/register/ts-legacy-module-interop --test tests/public-asset.test.cjs tests/account-avatar-assets.test.cjs`。生产发布由 `package` 完成 Hosted 构建和变体校验，不再额外重复普通构建。

## QUIC / HTTP/2 与压缩

assets 同时启用 HTTP/3（QUIC）与 HTTP/2。浏览器依据 CDN 的 Alt-Svc、TLS ALPN、客户端能力和网络条件自动选择协议；首次请求可能使用 HTTP/2，QUIC 不可用时保留 HTTP/2，极端兼容环境也可能退至 HTTP/1.1。网页不伪造主站 Alt-Svc，不引入 WebTransport / 自制 QUIC 客户端或业务层协议重试。

WebConfig 与 admin 的 HTML 响应通过 HTTP Link 提前进行 assets 的 DNS 解析与匿名连接预热。公开资源提供 Timing-Allow-Origin，便于通过浏览器 Network Protocol / Resource Timing 验证 h3 或 h2；实际协议必须以网络记录为准，不能用控制台开关或预连接标签替代实测。

Next 构建已最小化 JS/CSS。现有 CDN 保留页面优化、Gzip、Brotli 设置；阿里云说明压缩与页面优化同时开启时优先压缩，页面改写不生效。部署校验请求 Gzip 并在解压后核对源文件 SHA-256；浏览器自动解压，设备资源校验仍使用原始字节，不能为兼容 CDN 改写而取消摘要校验。Brotli 响应同样须解压后验证。HTML 始终由主站实时返回，保留 CSP 内联脚本哈希；不将 HTML、路由数据或 API 切入内容改写和共享缓存。

参考：[阿里云 QUIC](https://help.aliyun.com/zh/cdn/user-guide/what-is-the-quic-protocol/)、[阿里云页面优化与压缩限制](https://help.aliyun.com/zh/cdn/user-guide/enable-html-optimization)、[浏览器预连接](https://developer.mozilla.org/en-US/docs/Web/HTML/Reference/Attributes/rel/preconnect)。QUIC 请求按已开通的阿里云计费规则计费。

## 2026-10-28 上线验收

- 发布编号 `20261028-024801-75eee7e4`，包 SHA-256 为 `95ec773d3b58dba6543c91dbd9b07cb909677a220b5817ac070bd19d7b4e8117`；业务数据备份为 `/var/backups/xora/data-20261028-025047-142002bb.tar.gz`，环境变更前备份为 `/var/backups/xora/server-env-before-web-cdn-20261028-024800.env`。
- 网页资源版本 `da2f56d414e448e1a2bc7018699d5f59`，148 个公开资源逐个经过 CDN 内容摘要及 CORS 校验，通过，耗时约 27 秒。首次迁移保留了 85 个旧版同源静态资源，抽查三个新包中不存在的旧脚本地址通过。
- WebConfig `/global/`、admin `/admin/users/` 的 CDN 引用、连接预热、安全策略和 HTML 不缓存均通过。私有图库 API 返回未登录 401，缺失资源及清单外 HTML 返回 404；重复请求均保持 `no-store`，未命中共享缓存。
- 使用独立 aioquic 客户端经受信任 TLS 校验，实测 HTTP/3 / QUIC / UDP 获取新版本 CSS，状态 200，原始内容摘要一致。独立 HTTP/2 客户端同样通过，CDN 返回标准 h3 Alt-Svc。此记录证明 CDN 两种协议可用，不表示所有终端的每次请求都必然采用 h3。
- HTTP/2 下分别请求 Brotli、Gzip，JS/CSS 均按对应编码返回，解压后摘要与构建文件一致；WOFF2 返回原有字体压缩格式。两种请求均观察到缓存命中。保留用户已开启的 CDN 性能优化设置。
- 45 项相关主机测试、前端类型检查及 Hosted CDN 构建通过；构建约 89 秒，有 React Hooks lint 警告，无编译错误。发布与远端检查通过。未执行设备连接、图片安装或硬件验收。

### 固件分发接入

固件 CDN 代码上线版本为 `20261028-040803-63c663ef`，部署包 SHA-256 为 `f329a40b9e90d23bb32e7ce6a547260135c7c99223d894b21793490a43523a1c`；数据备份为 `/var/backups/xora/data-20261028-040834-ca77250c.tar.gz`。Hosted 构建及变体校验通过，构建约 53 秒，部署约 50 秒；148 个新版网页资源的 CDN 摘要及 CORS 全部通过。固件发布/下载、OTA 签名/硬件门禁、前端目录/安装与 CDN 下载定向测试通过；保留原有 React Hooks lint 警告。

已通过 CDN 域名完整下载并校验现有发布包：1.0.1 为 658846 字节、SHA-256 `c19166217b95787c3976fa5dc738d944c359f3b5a3a6cd0506986f3dbc67d0d2`；1.0.0 为 659008 字节、SHA-256 `44ed006f84d6cad3e851ffab1972d7f637f1815f0e3d2d5080d1ab9a4fa8db6b`。CORS、暴露的摘要响应头及错误路径 `404 no-store` 均通过。已在阿里云补齐 `/cdn/firmware/` 目录规则：60 秒、权重 90、优先遵循源站，其余开关关闭；两个版本均实测首次请求 `MISS`、第二次 `HIT TCP_MEM_HIT`，`X-Swift-CacheTime: 60`，命中后的完整内容摘要仍一致。此次仅更新网站，不创建或发布固件，不连接或升级设备。
