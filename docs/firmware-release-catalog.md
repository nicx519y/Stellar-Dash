# XORA 固件上传与目录

管理员导入、校验、草稿、发布与撤回沿用现有流程。WebConfig 固件页消费整机目录，通过 v2 发布包执行 STM32 + TX 安装；v1 包仍只供浏览。设备事务和验收边界见 [整机安装](firmware-release-install.md)。

## 本地使用

在仓库根目录运行（需已有本地集成服务所需的 manifest 与产物）：

```powershell
python tools/hbox.py web build
python tools/hbox.py web local-serve --port 3001
```

- 管理页面：`http://localhost:3001/admin/firmware/`，也可从账户管理或官方图库进入。
- 公开目录：`http://localhost:3001/firmware/releases/`，无需登录或 HID 连接。
- WebConfig 固件页显示设备整机身份、相同目录和安装任务；产品页不再提供独立 STM32/TX 安装按钮。
- 管理权限沿用邮箱管理员会话和 Origin 校验。本地账号预授权参见 [邮箱认证](../server/doc/EMAIL_AUTH.md)。

`local-serve` 的固件产物前置检查仍存在，本功能没有修改冻结启动／烧录入口。网页更新后需重新构建静态网页；只运行 Next 开发服务不能替代账户与目录 API。

## 生成发布包

发布包是平铺 ZIP：`release.json`、`release.sig`、STM32 A/B 两个现有签名 ZIP、TX BIN，以及可选 RX BIN。`release.sig` 是对 ZIP 内 **release.json 原始字节** 的 ECDSA P-256/SHA-256 签名，编码为 64 字节 IEEE P1363，不是 DER 或 Base64。

签名私钥只用于本地工具，不发送到服务器。服务器沿用 `FIRMWARE_RELEASE_PUBLIC_KEY_FILE` 公钥验证外层发布与内层 STM32 包。内外层签名须使用该公钥对应的密钥。

将下列 `release-source.json` 和实际产物放在同一目录；版本、构建标识和兼容声明必须来自真实构建记录。组件的 `size` 和 `sha256` 由打包工具计算。A/B 的 `buildId` 表示共同源码／构建批次，不是二进制摘要。

```json
{
  "schemaVersion": 2,
  "buildId": "replace-with-generated-build-id",
  "install": {
    "protocol": 1, "order": "tx-then-stm32",
    "configRead": { "min": 34, "max": 34 }, "configWrite": 34,
    "stm32Maintenance": { "min": 1, "max": 1 },
    "txMaintenance": { "min": 1, "max": 1 }
  },
  "product": "XORA",
  "deviceModel": "STM32H750_HBOX",
  "hardwareVersion": "2.0.0",
  "version": "2.1.0",
  "bootSecurityMode": "unlocked-development",
  "requiresManualLifecycleProvisioning": false,
  "compatibility": {
    "stm32Tx": "填写已验收的主控与 TX 版本组合",
    "txRx": "填写已验收的 TX 与 RX 版本组合"
  },
  "artifacts": [
    {
      "component": "stm32", "slot": "A", "file": "stm32-A.zip",
      "version": "2.1.0", "buildId": "source-revision-build-batch",
      "hardwareVersion": "2.0.0", "bootSecurityMode": "unlocked-development",
      "requiresManualLifecycleProvisioning": false
    },
    {
      "component": "stm32", "slot": "B", "file": "stm32-B.zip",
      "version": "2.1.0", "buildId": "source-revision-build-batch",
      "hardwareVersion": "2.0.0", "bootSecurityMode": "unlocked-development",
      "requiresManualLifecycleProvisioning": false
    },
    {
      "component": "tx", "file": "RF_PHY_Hop_TX.bin",
      "version": "2.1.0", "buildId": "tx-source-revision",
      "imageFormat": "ch585-tx-combined",
      "hardwareVersion": "2.0.0", "bootSecurityMode": "unlocked-development",
      "requiresManualLifecycleProvisioning": false
    }
  ]
}
```

可选 RX 使用 `component: "rx"`、`imageFormat: "ch585-rx-bin"`，其他字段同 TX，不带 `slot`。当前仅接收硬件 `2.0.0`，与现有 STM32 内层包验证器保持一致。v2 的 STM32/TX 版本、构建身份、配置与维护协议须匹配可执行文件内的身份记录；打包器计算 metadata 和 TX Application 摘要。RX 仍只展示签名声明，不纳入主机安装成功条件。配置版本示例以当前 CONFIG_VERSION=34 为准，不能为了通过门禁虚填兼容范围。

```powershell
node server/scripts/create-firmware-bundle.js "path/to/release-source.json" "path/to/signing-key.pem" "path/to/xora-release.zip"
```

该工具只读取已有产物、签名、校验并生成文件；不会编译、联网、上传或访问硬件。输出已存在时拒绝覆盖。签名包总大小最多 12 MiB，平铺条目最多 6 个，每条解压后最多 4 MiB。

### 在本地 admin 服务生成草稿

当前仓库可使用一条命令自动完成无锁 A/B、TX 主机构建、发布身份注入、v2 签名打包和初版说明生成：

```powershell
python tools/local_firmware_draft.py
```

脚本只使用 `.hbox/webconfig-local` 的本地 PKI，构建时隔离状态目录并在结束后恢复 `common/release_build_identity.h`；不烧录设备。它默认使用版本 `1.0.0`，产物放在 `.hbox/firmware-drafts/XORA-1.0.0-<时间>/`。若 `.hbox/webconfig-local/firmware-manage-token.txt` 已保存从本地 admin 创建的 `firmware.manage` 令牌，它会把包导入 `http://localhost:3001` 草稿并写入说明；没有令牌时仅生成和校验本地文件，并明确报告未上传。不会向远端服务发送请求。重新运行会重新构建并创建新输出目录；已有包可直接在本地 admin 固件页导入。

先使用本页“本地使用”中的 `local-serve --port 3001` 启动本地 admin 服务，登录本地管理员账号，在 `/admin/users/` 创建仅含 `firmware.manage` 范围的服务令牌，并把只显示一次的令牌保存到 `.hbox/webconfig-local/firmware-manage-token.txt`。也可在已登录的本地 admin 固件页手动导入一键命令生成的 ZIP，随后粘贴旁边的更新说明文件内容。默认本地状态目录使用 `.hbox/webconfig-local/pki/firmware-release-private.pem` 测试密钥，对应 `local-serve` 配置的验签公钥；不要拿其他诊断目录的密钥混用。

底层 `create-firmware-draft.js` 默认使用 `http://localhost:3001`，并且只接受 `localhost`、`127.0.0.1` 或 `::1` 的本地 admin 服务地址；远端上传会在读取文件或发出请求前被拒绝。本地草稿只保存在本地服务的数据目录，不会同步到正式服务。

当前为初版，`--initial-release` 自动生成简短的初版发布声明，无需填写更新点。以后发布时去掉该参数：命令自动查找早于目标版本、且可从当前提交到达的最新 `xora-v<版本>` 或 `v<版本>` 标签，以两次发布之间已提交的 STM32、TX 与共用固件源码变更生成简洁、面向用户的说明。如果上一版未打标签，可加 `--since <上一版提交或标签>`。固件源码有未提交改动、找不到上一版或没有可归纳的设备源码变化时，命令会停止，避免说明与提交不符；发布前应先核对生成文案。WebConfig 托管页面的改动不会写入固件更新说明。

命令生成 `XORA-<版本>-release.zip`、`XORA-<版本>-release-notes.md` 和记录 Git 提交及分类依据的 `XORA-<版本>-release-notes-source.json`，本地重新验签后上传 ZIP，并把更新说明保存到 admin 草稿。私钥和服务令牌仅从本机文件读取，不会上传；服务令牌必须有 `firmware.manage` 范围。首次检查可加 `--dry-run` 并省略 `--server`、`--service-token-file`，只生成本地文件。

成功导入后命令显示草稿 ID 和 `/admin/firmware/` 地址。管理员在该页面查看、修改用户可见更新说明，补充仅管理员可见的实际验收记录，再人工发布。一键命令会构建固件，但不会烧录或发布；底层 `create-firmware-draft.js` 只打包已有产物。上传超时或连接中断后，先按版本在后台检查草稿，再决定是否重试，以免重复导入。`tools/release.py upload` 仍对应旧 STM32 固件接口，不用于此 v2 整机草稿。

初版正式发布并确认对应源码提交后，为该提交建立版本标签（例如 `git tag xora-v1.0.0 <发布提交>`）；下一版命令即可自动定位它。自动说明依据变更文件归纳体验主题，不代替管理员检查具体效果和措辞。

## 管理流程

1. 选择发布 ZIP，观察上传进度；上传结束后显示服务器验签阶段。
2. 服务端验证签名、声明文件集合、摘要、硬件、STM32 内层签名及槽布局和无锁模式声明。错误显示在页面；失败不会生成可发布版本。
3. 成功生成草稿。填写用户可见更新说明、内部验收记录并保存。
4. 人工管理员确认发布。服务端重新读取并验证存储包，提交发布状态与审计记录后，公开目录立即可查。
5. 撤回必须填写原因。新目录请求不再返回该版本，已打开页面需刷新；不撤销已经获取的内容。恢复发布同样重新验签。

发布后所有内容被冻结，包括更新说明和验收记录；首期仅草稿可编辑。替换镜像需要新版本。重复的型号／硬件／整机版本被拒绝。草稿可以删除，审计日志和内容存储保留，不做在线物理清理。

操作使用 `revision` 防止并发覆盖，冲突后点击“重新加载详情”。服务令牌 `firmware.manage` 可以导入、查阅、编辑、删除草稿；发布、撤回、恢复发布均要求人工管理员登录。

## 接口与存储

| 方法与路径 | 用途 |
|---|---|
| `POST /api/admin/firmware/imports` | 单个 multipart 字段 `bundle`；返回导入任务结果 |
| `GET /api/admin/firmware/imports/:id` | 导入状态与错误；不包含内部路径 |
| `GET /api/admin/firmware/releases` | 列表，支持 `query/status/hardware/offset/limit` |
| `GET /api/admin/firmware/releases/:id` | 管理详情及最近 100 条审计记录 |
| `PATCH /api/admin/firmware/releases/:id` | `{revision, notes, acceptance}`，仅草稿 |
| `POST /api/admin/firmware/releases/:id/publish` | `{revision}`；发布或恢复 |
| `POST /api/admin/firmware/releases/:id/withdraw` | `{revision, reason}` |
| `DELETE /api/admin/firmware/releases/:id` | `{revision}`，仅草稿 |
| `GET /api/admin/firmware/legacy` | 只读列出历史 STM32 数据，不进行迁移或发布 |
| `GET /api/firmware-releases` | 公开正式目录，可搜索和按硬件筛选 |
| `GET /api/firmware-releases/:id` | 公开详情；草稿／撤回均返回 404 |
| `GET /api/firmware-releases/:id/download` | 已发布 v2 ZIP，返回 X-Content-SHA256；下载时重新验签 |
| `GET /api/firmware-releases/verification-key` | 发布验签公钥 JWK，与设备内置信任根对应 |

除 ZIP 下载外，响应使用 `{success: true, data: ...}`，禁止缓存。公开接口沿用当前直连设备访问上下文，不要求账户登录或设备证明；不公开内部验收记录或管理员标识。

数据位于 `HBOX_SERVER_DATA_DIR/firmware_releases.sqlite3` 与 `firmware-release-assets/`。包按内容摘要保存，暂存区在其 `tmp/` 内；文件不会进入旧 `uploads`，没有静态下载地址。SQLite 事务原子提交状态与审计；文件先落盘再提交引用，失败遗留文件没有公开入口。备份时需要一致地备份数据库（含 WAL）和内容目录，勿直接复制运行中的单个 SQLite 文件。

旧固件 JSON、上传脚本与 API 保留兼容；旧写入接口也受整机事务互斥约束。“历史 STM32 发布”只读展示。重新打包必须使用新的唯一版本记录。

## 验证与预览

```powershell
cd server
node --test tests/firmware-releases.test.js tests/ota-package-signature.test.js tests/admin-access.test.js
```

前端在 `application/www` 运行 `npm run typecheck`、`npm run test:firmware-catalog`。`npm run dev:mock` 可预览草稿、发布、撤回与公开目录；使用现有 Mock 管理员账号。Mock 使用浏览器临时密钥生成测试包，走浏览器签名/摘要验证和模拟事务，不访问真实设备或服务端。它不替代设备验签或断电验收；数据在同一标签页的 sessionStorage 中共享。
