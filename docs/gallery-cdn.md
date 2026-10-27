# 官方图库 CDN（现有服务器源站）

图片继续存放在服务器 `HBOX_GALLERY_ASSET_DIR`。CDN 仅缓存已发布官方图片的原图、预览图及设备图片包；个人图库、草稿、登录、管理员与图库列表接口不使用共享缓存。不需要 OSS，不修改设备固件。

## 请求路径与权限

- 图库列表仍调用站点同源 `/api/gallery/system`。设置 `GALLERY_CDN_ORIGIN` 后，仅已发布官方条目的三个文件地址使用该 HTTPS origin。
- CDN 源站入口为 `/cdn/gallery/<id>/<version>/{source,preview,device}`，版本取自图片更新时间和设备包摘要。回源时再次检查 scope、发布状态和版本；个人图片、草稿、过期版本及不存在的文件返回不缓存的 404。
- 成功文件响应 `Cache-Control: public, max-age=300`，允许无凭据跨域读取。下架后源站立即拒绝新请求，已缓存副本最多仍可使用约 5 分钟；紧急撤回时需同时刷新 CDN 缓存。
- 前端公开文件请求不经过设备授权客户端，不携带 Cookie 或 Authorization，不跟随重定向；设备包下载后仍核对 SHA-256 和原有图片元数据。
- CSP 只为已配置的 CDN origin 增加 `connect-src`。脚本、字体和其他来源保持原策略。预览继续通过 Blob 展示。
- 原 `/api/gallery/images/...` 兼容入口保留，个人图片仍校验所属账户，管理员预览沿用管理员入口。

## 阿里云配置

建议使用独立域名 `assets.st-dash.com`，不要将 config 或 manager 的 DNS 改为 CDN。

| 配置 | 值 |
| --- | --- |
| 业务类型 | 图片小文件 |
| 加速域名 | `assets.st-dash.com` |
| 源站类型 | IP |
| 源站地址 | `182.92.72.220` |
| 回源协议 / 端口 | HTTPS / 443 |
| 回源 Host | `config.st-dash.com` |
| 回源 SNI | `config.st-dash.com`，与源站证书匹配 |
| 客户端 HTTPS | 部署覆盖 `assets.st-dash.com` 的有效证书 |
| 默认目录 `/` 缓存 | 0 秒，不缓存 |
| 目录 `/cdn/gallery/` 缓存 | 300 秒，优先遵循源站缓存策略，权重高于默认规则 |
| 忽略源站不缓存标头 | 关闭，不覆盖 `no-store` / `private` |
| 状态码缓存 | 403 / 404 / 5xx 不缓存 |
| URL 参数 | 保留，不忽略 `poster=frame-3` |

加速区域根据实际用户位置和备案条件选择；包含中国内地的区域需要备案。添加域名后，将 DNS 中 `assets` 的 CNAME 指向阿里云分配的精确目标。CDN 不会自动替用户完成 DNS 切换。

源站使用 config 的回源 Host 是为了复用现有 TLS 站点。CDN 默认规则必须不缓存；图库公开路径按上述策略缓存，版本化网页资源另按 [CDN 部署指南](webconfig-cdn-deployment.md) 配置 `/cdn/web/`。不要设置全站长缓存或将 `/api/gallery/images/` 当作公开目录。网页和管理界面继续从 config / manager 进入。

官方参考：[添加域名与接入](https://help.aliyun.com/zh/cdn/getting-started/getting-started)、[缓存规则](https://help.aliyun.com/zh/cdn/user-guide/configure-the-cdn-cache-expiration-time)、[回源配置](https://help.aliyun.com/zh/cdn/user-guide/back-to-origin-settings/)。

## 部署与启用顺序

1. 先按 [部署指南](webconfig-admin-deployment.md) 部署支持 CDN 的后端和 Hosted 前端，暂不设置 `gallery_cdn_origin`。此时保留全部同源地址，同时可以验证新的公开回源入口。
2. 完成阿里云的域名、回源、缓存、证书和 DNS 配置；用真实官方图片的带版本路径验证 CDN GET/HEAD、CORS、内容摘要和重复请求的缓存命中，验证未知路径不缓存。
3. 在本机 `.hbox/deploy/config.json` 增加 `"gallery_cdn_origin": "https://assets.st-dash.com"`，并在服务器 `/etc/xora/server.env` 增加对应的 `GALLERY_CDN_ORIGIN="https://assets.st-dash.com"`。部署工具会核对两边一致；不把配置写入前端构建环境，也不放宽其他允许来源。
4. 重启 `xora-server`，确认图库列表返回 CDN 地址、HTML 的 CSP 包含对应 `connect-src`、个人图库仍为同源地址。用浏览器查看静态海报、悬停 GIF 和文件下载，设备安装验收需单独执行。
5. 后续部署继续保留此配置字段，正常执行 `deploy_xora.py check/package/deploy`。

CDN 失效时的回退：移除本机 `gallery_cdn_origin` 和服务器 `GALLERY_CDN_ORIGIN`，重启服务并刷新页面；图库重新返回原有同源地址，无需移动图片或更改 config / manager 的 DNS。已打开页面需重新加载图库列表。

## 定向验证

- `server/`：`node --test tests/gallery-cdn.test.js tests/image-gallery.test.js tests/hosted-webconfig.test.js tests/device-auth-v2.test.js`
- 仓库根目录：`python server/tests/test_deploy_gallery_cdn.py`
- `application/www/`：`node -r sucrase/register/ts-legacy-module-interop --test tests/gallery-cdn.test.cjs tests/gallery-gif-poster.test.cjs tests/installed-system-image.test.cjs`
- 发布前：`npm run build:hosted`，由部署打包入口完成构建变体验证。

主机检查不能证明已开通 CDN、DNS 已切换、证书有效或节点命中；这些必须分别在线核对。

## 免费证书与自动同步

`assets.st-dash.com` 使用独立的 Let's Encrypt RSA 2048 证书。证书签发免费；CDN 流量与 HTTPS 请求仍按阿里云的实际用量计费。证书不依赖本机电脑持续运行。

- Nginx 在 config / manager 的 HTTPS 站点中直接提供 `/.well-known/acme-challenge/`，文件根目录为 `/var/lib/letsencrypt`，响应带 `no-store`。assets 的 HTTP 请求通过已有 CDN 回源 Host/SNI 到 config，因此首次签发不要求 CDN 已启用 HTTPS。
- 使用已有 Certbot 账户执行 `certbot certonly --webroot -w /var/lib/letsencrypt -d assets.st-dash.com --cert-name assets.st-dash.com --key-type rsa --rsa-key-size 2048 --non-interactive`。证书和私钥保留在 `/etc/letsencrypt/live/assets.st-dash.com/`，不与 config / manager 共用私钥。
- 系统原有 `certbot.timer` 定期检查续期。部署钩子 `/etc/letsencrypt/renewal-hooks/deploy/xora-cdn-assets` 调用 `/opt/xora/cdn-cert/cdn_certificate.py`，只处理 assets 的 lineage，跳过其他站点证书。
- 独立 Python 虚拟环境位于 `/opt/xora/cdn-cert/venv`，依赖版本来自 [requirements](../server/tools/cdn-certificate-requirements.txt)。部署脚本、[service](../server/tools/xora-cdn-cert-sync.service)、[timer](../server/tools/xora-cdn-cert-sync.timer) 和[钩子](../server/tools/xora-cdn-assets-deploy-hook) 由服务器 root 管理。安装后需要 `systemctl daemon-reload` 和 `systemctl enable --now xora-cdn-cert-sync.timer`。
- RAM 专用用户 `xora-cdn-cert` 仅绑定 [限定策略](../server/tools/cdn-cert-assets-policy.json)，禁止额外绑定 CDN 管理员策略。凭据保存在 `/etc/xora/secrets/aliyun-cdn-assets.json`，字段为 `access_key_id` / `access_key_secret`，要求 root 所有、权限 600；不放入代码包或日志。
- 同步前检查受信任证书链、域名、有效期和私钥匹配。使用阿里云官方 SDK，通过 HTTPS POST 将该域名证书及私钥上传 CDN，并启用该域名 HTTPS。程序不申请管理其他域名或读取账户的权限。
- CDN API 确认接收后，程序仍需通过带域名与 CA 校验的 TLS 连接核对节点证书指纹。等待传播时返回状态 `pending`；15 分钟的重试定时器会继续核对。匹配时不会重复上传。API 失败不记录为成功，日志不打印密钥或原始请求。

运维检查（服务器 root）：

```sh
systemctl list-timers certbot.timer xora-cdn-cert-sync.timer --no-pager
/opt/xora/cdn-cert/venv/bin/python /opt/xora/cdn-cert/cdn_certificate.py status
systemctl start xora-cdn-cert-sync.service
journalctl -u xora-cdn-cert-sync.service --since today --no-pager
certbot renew --cert-name assets.st-dash.com --dry-run
```

续期演练不添加 `--run-deploy-hooks`，避免上传测试 CA 签发的证书。测试环境网络故障与实际续期配置错误分别记录，不能把一次正式签发成功当作演练通过。

停止自动上传时先停用 `xora-cdn-cert-sync.timer`，并移走对应 deploy hook，再禁用 RAM 专用 AccessKey。不要删除 config / manager 的 Certbot 定时任务或现有站点证书。CDN 已部署证书仍会在到期前使用；停用同步不等于撤销证书或停用 HTTPS。

## 2026-10-28 上线记录

- CNAME 为 `assets.st-dash.com.w.cdngslb.com`，现有服务器继续作为源站，图片未迁移到 OSS。
- 独立 Let's Encrypt 证书已签发并同步到 CDN；按香港时区，到期时间为 2027-01-26 00:50。证书 SHA-256 指纹为 `244733667b044cd40a7bfec86c50ccfc059dd0a5ac130f9f3db3940e8c4523b0`，经受信任 TLS 连接核对与节点一致。该到期日只作上线快照，后续以实际续期证书为准。
- `certbot.timer` 与 `xora-cdn-cert-sync.timer` 均已启用；首次上传及随后不重复上传的同步检查通过。RAM 用户只绑定 `XoraCdnAssetsCertificateSync`。单个 `acs:SourceIp` 按阿里云要求填写裸 IP `182.92.72.220`，不加 `/32`。
- 默认目录 0 秒、公开图库目录 300 秒的两条规则均已在控制台显示成功。实测原图、预览图及设备图片包的内容与源站一致，设备包 SHA-256 与图库元数据一致；GET / HEAD、跨域标头通过，重复 GET 命中节点内存缓存，节点过期时间为 300 秒。未知图片变体连续两次返回 404 / `no-store`、缓存时间 0 且未命中。GIF 静态海报 PNG 的查询参数路径及缓存命中通过，认证接口保持 `no-store` / 节点缓存时间 0。本机 Chrome 已通过 HTTPS 正常显示 CDN 预览图。
- 本机部署配置与服务器环境已启用 `https://assets.st-dash.com`。27 个已发布官方条目的文件地址与页面 CSP 验证通过。切换前环境备份为 `/var/backups/xora/server-env-before-cdn-20261028-021826.env`。
- 本次新增证书同步、Nginx 验证路径与既有部署工具定向主机检查共 34 项通过，远程部署配置检查通过。正式证书签发、CDN 同步和 Let's Encrypt 续期演练均通过，演练约 156 秒；未上传测试 CA 证书。未执行设备图片安装或硬件验收。
