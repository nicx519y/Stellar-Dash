# XORA 固件上传与目录

管理员导入、校验、草稿、发布与撤回沿用现有流程。WebConfig 固件页消费整机目录，通过协议 2 的 v2 发布包执行主控 + TX 安装；旧协议包仍只供浏览。设备事务和验收边界见 [整机安装](firmware-release-install.md)。

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
    "protocol": 2, "order": "tx-then-stm32",
    "configRead": { "min": 35, "max": 35 }, "configWrite": 35,
    "stm32Maintenance": { "min": 2, "max": 2 },
    "txMaintenance": { "min": 2, "max": 2 }
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

可选 RX 使用 `component: "rx"`、`imageFormat: "ch585-rx-bin"`，其他字段同 TX，不带 `slot`。当前仅接收硬件 `2.0.0`，与现有 STM32 内层包验证器保持一致。v2 的 STM32/TX 版本、构建身份、配置与维护协议须匹配可执行文件内的身份记录；打包器计算 metadata 和 TX Application 摘要。RX 仍只展示签名声明，不纳入主机安装成功条件。一键打包从 `application/Inc/system/board_cfg.h` 读取 `CONFIG_VERSION`，生成相同格式的读取范围与写入版本；不能为了通过门禁虚填兼容范围。

```powershell
node server/scripts/create-firmware-bundle.js "path/to/release-source.json" "path/to/signing-key.pem" "path/to/xora-release.zip"
```

该工具只读取已有产物、签名、校验并生成文件；不会编译、联网、上传或访问硬件。输出已存在时拒绝覆盖。签名包总大小最多 12 MiB，平铺条目最多 6 个，每条解压后最多 4 MiB。

### 构建并上传远程 admin 草稿

当前仓库可使用一条命令自动完成无锁 A/B、TX 主机构建、发布身份注入、v2 签名打包和初版说明生成：

```powershell
python tools/local_firmware_draft.py
```

脚本复用 `.hbox/webconfig-local` 的开发状态，构建时隔离状态目录并在结束后恢复 `common/release_build_identity.h`；不烧录设备。远程服务器默认使用 `.hbox/deploy/server-readiness.json` 中与目标域名对应的 `private_key_local_path` 正式签名密钥；loopback 服务器使用本地 PKI。也可用 `--signing-key <私钥路径>` 显式指定。上传模式在构建前读取目标后台的公开验签公钥，拒绝不匹配的私钥或无法核对的服务器，避免构建后才发现签名不匹配。未配置远程签名密钥时拒绝回退到开发密钥。

选定的密钥同时用于外层发布包、STM32 内层 metadata 和隔离构建中的固件验签公钥；原开发 PKI、制造商与授权公钥保持不变。原设备若仍信任开发公钥，不能仅凭后台导入成功就认定可安装正式密钥签发的包；设备信任根迁移需独立处理与验收，本命令不执行设备迁移或刷写。

它默认使用版本 `1.0.0`，当前协议 2 测试版使用 `python tools/local_firmware_draft.py --version 1.0.2`，不覆盖已发布 1.0.1。产物放在 `.hbox/firmware-drafts/XORA-<版本>-<时间>/package/`。若 `.hbox/webconfig-local/firmware-manage-token.txt` 已保存目标后台签发的 `firmware.manage` 令牌，它会默认把包导入 `https://manager.st-dash.com` 草稿并写入说明；没有令牌时仍生成并校验签名包、Markdown 和 Git 依据文件，明确报告未上传。加 `--no-upload` 可在令牌存在时也只生成本地文件，不执行服务器公钥及版本预检；加 `--server http://localhost:3001` 可改为本地 admin。上传模式先查询同型号、硬件和版本的已有记录；存在时在构建前停止，显示记录状态、ID 和后台地址。同版本已有草稿时直接在后台查看与编辑说明；上传另一构建需显式指定未使用的 `--version`，不自动递增版本、复用旧二进制或覆盖已有包。版本预检仅为提前反馈，并发导入及曾发布后删除的版本仍由服务端最终门禁保护。最终打包或上传失败时，外层命令直接显示该阶段的具体错误并保留日志。

登录目标后台（默认 `https://manager.st-dash.com`），在 `/admin/service-tokens/` 创建含 `firmware.manage` 范围的服务令牌。本地 admin 创建的令牌不能用于远程后台。生成弹窗分别提供“复制 Windows 脚本”和“复制 macOS 脚本”；在仓库根目录的 Windows PowerShell 或 macOS 终端（zsh/bash）中粘贴执行一次，以后打包无需指定令牌文件。Windows 脚本调用 `python`，macOS 脚本调用 `python3`；两者均通过 `local_firmware_draft.py --save-token` 从标准输入接收令牌，原子替换 `.hbox/webconfig-local/firmware-manage-token.txt`，不保留旧令牌副本，也不撤销后台旧令牌；此设置操作不构建、不上传、不烧录。令牌与脚本只在创建弹窗中提供，关闭后不可再次取回。手动保存其他文件时仍可用 `--token-file <路径>` 临时指定。也可在已登录的目标 admin 固件页手动导入一键命令生成的 ZIP，再在草稿详情页导入旁边的 `.md` 更新说明。本地测试密钥对应 `local-serve` 配置的验签公钥；远程服务使用独立的正式公钥，不能接受测试密钥签发的包。正式密钥记录与配置见 [部署指南](webconfig-admin-deployment.md)。

底层 `create-firmware-draft.js` 同样默认使用 `https://manager.st-dash.com`，可用 `--server <后台 origin>` 覆盖。远程后台要求 HTTPS；`localhost`、`127.0.0.1` 或 `::1` 的本地服务也允许 HTTP。地址不允许携带用户名、密码、路径、查询参数或片段；请求不自动跟随重定向。本地草稿只保存在本地服务的数据目录，不会同步到正式服务。

本地一键命令的默认版本 `1.0.0` 生成简短的初版发布声明；后续版本自动查找本地较低版本包中的 Git 依据文件，比较上次实际源码快照与当前 Git 工作区，包括未提交及新加入的固件源码。新依据文件记录每个源码文件的摘要；历史文件只有提交信息时，比较该提交与当前工作区，并输出实际使用的基线版本。缺少本地记录时才查找可达的 `xora-v<版本>` 或 `v<版本>` 标签，也可加 `--since <上一版提交或标签>`。无设备源码变化时生成简短的常规维护说明，不虚构具体更新。WebConfig 托管页面的改动不会写入固件更新说明；发布前仍须核对文案。

本地一键命令始终生成并校验 `XORA-<版本>-release.zip`，同时生成 `XORA-<版本>-release-notes.md` 和记录 Git 提交、源码快照及分类依据的 `XORA-<版本>-release-notes-source.json`。有目标后台的服务令牌且未指定 `--no-upload` 时，重新验签后上传 ZIP，并把更新说明保存到 admin 草稿。私钥仅在本机使用；服务令牌从本机文件读取，通过请求的 Bearer 认证头发送到目标后台，不写入发布包，必须有 `firmware.manage` 范围。底层打包上传命令首次检查可加 `--dry-run` 并省略 `--server`、`--service-token-file`，只生成本地文件。直接使用底层命令时仍默认要求已提交源码；一键本地命令明确启用工作区和本地历史比较。

成功导入后命令显示草稿 ID 和 `/admin/firmware/` 地址。管理员从列表进入草稿详情，查看或修改用户可见更新说明；文字编辑会自动保存，点击“导入 .md”或将 `.md` 拖入更新说明输入框都会替换现有说明并立即保存。说明保存且非空后可由管理员人工发布。一键命令会构建固件，但不会烧录或发布；底层 `create-firmware-draft.js` 只打包已有产物。上传超时或连接中断后，先按版本在后台检查草稿，再决定是否重试，以免重复导入。`tools/release.py upload` 仍对应旧 STM32 固件接口，不用于此 v2 整机草稿。

正式发布并确认对应源码提交后，可为该提交建立版本标签（例如 `git tag xora-v1.0.0 <发布提交>`），供缺少本地包记录的环境定位基线。自动说明依据变更文件归纳体验主题，不代替管理员检查具体效果和措辞。

## 管理流程

1. 选择发布 ZIP，观察上传进度；上传结束后显示服务器验签阶段。
2. 服务端验证签名、声明文件集合、摘要、硬件、STM32 内层签名及槽布局和无锁模式声明。错误显示在页面；失败不会生成可发布版本。
3. 成功生成草稿。在详情页填写或导入 `.md` 更新说明，等待右上角显示“已保存”。保存失败时保留文字并可重试。
4. 人工管理员确认发布。服务端重新读取并验证存储包，提交发布状态与审计记录后，公开目录立即可查。
5. 撤回必须填写原因。新目录请求不再返回该版本，已打开页面需刷新；不撤销已经获取的内容。恢复发布同样重新验签。

发布后更新说明和发布包被冻结；仅草稿可编辑。替换镜像需要新版本。重复的型号／硬件／整机版本被拒绝。管理列表和详情页均提供“删除固件”，草稿、已发布和已撤回版本都可在确认后删除；删除后管理列表、公开详情和下载入口不再提供该版本，已下载或安装的固件不受影响。审计日志和内容存储保留，不做在线物理清理；曾发布版本删除后仍不可重新导入同型号／硬件／整机版本，未发布草稿可重新导入。历史验收记录仍保存在现有数据库字段中，但不再是发布门槛，也不在详情页编辑。

操作使用 `revision` 防止并发覆盖；自动保存冲突时保留本地文字、显示失败，不覆盖他处修改。服务令牌 `firmware.manage` 可以导入、查阅、编辑、删除草稿；发布、撤回、恢复发布及删除已发布／已撤回版本均要求人工管理员登录。

## 接口与存储

| 方法与路径 | 用途 |
|---|---|
| `POST /api/admin/firmware/imports` | 单个 multipart 字段 `bundle`；返回导入任务结果 |
| `GET /api/admin/firmware/imports/:id` | 导入状态与错误；不包含内部路径 |
| `GET /api/admin/firmware/releases` | 列表，支持 `query/status/hardware/offset/limit` |
| `GET /api/admin/firmware/releases/:id` | 管理详情及最近 100 条审计记录 |
| `PATCH /api/admin/firmware/releases/:id` | `{revision, notes}`，仅草稿；旧请求可选传 `acceptance` 以兼容历史客户端 |
| `POST /api/admin/firmware/releases/:id/publish` | `{revision}`；发布或恢复 |
| `POST /api/admin/firmware/releases/:id/withdraw` | `{revision, reason}` |
| `DELETE /api/admin/firmware/releases/:id` | `{revision}`，支持全部状态；已发布／已撤回版本要求人工管理员会话 |
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
