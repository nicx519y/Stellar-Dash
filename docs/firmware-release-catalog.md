# XORA 固件上传与目录

本次功能只覆盖管理员导入、校验、草稿、正式发布、撤回，以及 WebConfig 浏览版本和组件详情。没有设备写入、安装计划、自动更新、下载入口或升级统计。已有 STM32 升级接口和设备流程保持独立；新整机包不会进入旧升级目录。

## 本地使用

在仓库根目录运行（需已有本地集成服务所需的 manifest 与产物）：

```powershell
python tools/hbox.py web build
python tools/hbox.py web local-serve --port 3001
```

- 管理页面：`http://localhost:3001/admin/firmware/`，也可从账户管理或官方图库进入。
- 公开目录：`http://localhost:3001/firmware/releases/`，无需登录或 HID 连接。
- WebConfig 原固件页也显示同一目录；旧升级按钮不消费新目录的数据。
- 管理权限沿用邮箱管理员会话和 Origin 校验。本地账号预授权参见 [邮箱认证](../server/doc/EMAIL_AUTH.md)。

`local-serve` 的固件产物前置检查仍存在，本功能没有修改冻结启动／烧录入口。网页更新后需重新构建静态网页；只运行 Next 开发服务不能替代账户与目录 API。

## 生成发布包

发布包是平铺 ZIP：`release.json`、`release.sig`、STM32 A/B 两个现有签名 ZIP、TX BIN，以及可选 RX BIN。`release.sig` 是对 ZIP 内 **release.json 原始字节** 的 ECDSA P-256/SHA-256 签名，编码为 64 字节 IEEE P1363，不是 DER 或 Base64。

签名私钥只用于本地工具，不发送到服务器。服务器沿用 `FIRMWARE_RELEASE_PUBLIC_KEY_FILE` 公钥验证外层发布与内层 STM32 包。内外层签名须使用该公钥对应的密钥。

将下列 `release-source.json` 和实际产物放在同一目录；版本、构建标识和兼容声明必须来自真实构建记录。组件的 `size` 和 `sha256` 由打包工具计算。A/B 的 `buildId` 表示共同源码／构建批次，不是二进制摘要。

```json
{
  "schemaVersion": 1,
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

可选 RX 使用 `component: "rx"`、`imageFormat: "ch585-rx-bin"`，其他字段同 TX，不带 `slot`。当前仅接收硬件 `2.0.0`，与现有 STM32 内层包验证器保持一致。TX/RX 的身份、版本和构建模式由签名清单声明；服务端不能从无自描述头的 BIN 独立证明其源码或运行行为。兼容声明只展示，不计算设备升级路径。

```powershell
node server/scripts/create-firmware-bundle.js "path/to/release-source.json" "path/to/signing-key.pem" "path/to/xora-release.zip"
```

该工具只读取已有产物、签名、校验并生成文件；不会编译、联网、上传或访问硬件。输出已存在时拒绝覆盖。签名包总大小最多 12 MiB，平铺条目最多 6 个，每条解压后最多 4 MiB。

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

所有响应使用 `{success: true, data: ...}`，禁止缓存。公开接口沿用当前直连设备访问上下文，不要求账户登录或设备证明；不公开内部验收记录或管理员标识。

数据位于 `HBOX_SERVER_DATA_DIR/firmware_releases.sqlite3` 与 `firmware-release-assets/`。包按内容摘要保存，暂存区在其 `tmp/` 内；文件不会进入旧 `uploads`，没有静态下载地址。SQLite 事务原子提交状态与审计；文件先落盘再提交引用，失败遗留文件没有公开入口。备份时需要一致地备份数据库（含 WAL）和内容目录，勿直接复制运行中的单个 SQLite 文件。

旧固件 JSON、上传脚本、API 与升级逻辑本次均不迁移；“历史 STM32 发布”只是只读展示。未来升级执行和旧接口退役另行设计。

## 验证与预览

```powershell
cd server
node --test tests/firmware-releases.test.js tests/ota-package-signature.test.js tests/admin-access.test.js
```

前端在 `application/www` 运行 `npm run typecheck`、`npm run test:firmware-catalog`。`npm run dev:mock` 可预览草稿、发布、撤回与公开目录；使用现有 Mock 管理员账号。Mock 明确标记，不执行真实验签，也不访问服务端；数据在同一标签页的 sessionStorage 中共享。
