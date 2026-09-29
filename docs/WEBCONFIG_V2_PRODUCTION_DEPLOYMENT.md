# WebConfig V2 生产部署与发布门禁

WebConfig V2 是同源 HTTPS 页面 + WebHID + 在线设备证明架构。当前仓库已提供
协议、浏览器客户端、服务端验证器、内部 Flash 身份格式和 fail-closed 公钥
fallback。开发模式仍可使用单进程内存 store 和本地 PEM signer；当
`NODE_ENV=production` 时，代码会拒绝本地 PEM signer，并要求加载
`DEVICE_AUTH_V2_ADAPTER_MODULE`。仓库不包含实际 KMS/Redis/生产数据库 adapter，
也尚未完成工厂治具、option-byte 工序和 USB 实机门禁，因此当前状态仍是
`engineering validation`，不能描述为量产安全架构。

## 服务组成与信任边界

```text
Chromium browser ── HTTPS ── WebConfig static site / device-auth API
       │                                  │
     WebHID                         Redis + device DB
       │                                  │
     CH585                         online authorization KMS
       │ transparent UsbBoardLink
     STM32 ── Kdev / boot key / local configuration
```

- CH585 只转发 64B HID report，不保存证书私钥或会话密钥。
- 浏览器保存 5 分钟 API token 和 ECDH 会话材料时仅限页面内存。
- STM32 是配置和身份的可信执行端；服务器不保存按键、宏、校准或用户图片。
- 制造 CA 与 firmware release signer 保持离线；在线服务只能调用
  authorization KMS。
- V2 只在物理 USB 档且 CH585 Maintenance role 锁定后启用。RF `0xA5`、
  RX 和 8K 调度保持冻结。

## 生产 adapter 是强制门禁

生产进程的最低环境如下。`DEVICE_AUTH_V2_ADAPTER_MODULE` 必须是绝对路径；
adapter factory 必须同步返回依赖对象，依赖对象的方法可以返回同步结果或
`Promise`。

```text
NODE_ENV=production
LISTEN_HOST=127.0.0.1
TRUST_PROXY_HOPS=1
WEB_CONFIG_ORIGINS=https://firmware.st-dash.com
WEB_CONFIG_STATIC_DIR=/srv/hbox-webconfig
WEB_CONFIG_REQUIRE_STATIC=1
HBOX_SERVER_DATA_DIR=/var/lib/hbox
HBOX_SERVER_UPLOAD_DIR=/var/lib/hbox/uploads
DEVICE_CA_PUBLIC_KEY_FILE=/run/secrets/hbox-device-ca-public.pem
FIRMWARE_RELEASE_PUBLIC_KEY_FILE=/run/secrets/hbox-firmware-release-public.pem
DEVICE_AUTH_V2_ADAPTER_MODULE=/opt/hbox/device-auth-v2-production-adapter.js
WEB_CONFIG_TARGET_POLICY_FILE=/etc/hbox/webconfig-target-policy.json
```

设备产品与页面版本选择不使用 STM32 silicon `REV_ID`。制造证书签名区中的四字节
`productId` 证明产品族，原有 `hardware_version_le` 表示 PCB revision。服务端仅在
二者通过证书、enrollment 和 target-policy 白名单后返回
`productId + pcbRevision + webConfigProfile`；浏览器再次与设备 attestation 的
hardware version 交叉校验，并只跳转到本站
`/webconfig/<profile>/...`。未知产品、未知 PCB 或未知 profile 均拒绝建立会话。

策略文件示例：

```json
{
  "schemaVersion": 1,
  "productId": "HBOX",
  "pcbRevisions": {
    "2.0.0": { "profile": "hbox-pcb-v2" },
    "3.0.0": { "profile": "hbox-pcb-v3" }
  }
}
```

`HBOX_SERVER_DATA_DIR`、`HBOX_SERVER_UPLOAD_DIR` 和
`FIRMWARE_RELEASE_PUBLIC_KEY_FILE` 在生产环境中都必须显式配置为绝对路径；缺少
任一项、使用相对路径，或发布公钥不是 P-256 公钥时，进程必须启动失败。
状态目录保存服务端 JSON 状态（在生产 adapter 完全接管前仅用于单机管理面），
上传目录保存待校验/已发布固件；两者不得落在代码仓库、静态站点目录或容器临时
层内。发布公钥文件只包含离线 firmware release signer 的公钥，私钥不得进入
WebConfig server。

`TRUST_PROXY_HOPS` 只能是 `0`、`1` 或 `2`，必须等于 Node 前方由本方控制的反向
代理层数；没有受控代理时保持默认 `0`。生产默认 `LISTEN_HOST=127.0.0.1`，不要
为了取得客户端 IP 而信任任意 `X-Forwarded-For`。

adapter 模块必须导出：

```js
function createDeviceAuthV2Dependencies({ storageManager, environment }) {
  return {
    permitSigner,      // sign(claims)
    challengeStore,    // issue(record), consume(challengeId)
    tokenStore,        // ttlMs, issue(record), resolve(token), revoke(token)
    devicePolicy,      // check(identity, attestation), get(deviceId)
    challengeLimiter,  // check(key)
    verifyLimiter      // check(key)
  };
}
```

六项依赖缺少任一项时 V2 认证保持 fail-closed。`challengeStore`、`tokenStore` 和
两个 rate limiter 必须跨所有 worker/主机共享；`devicePolicy` 必须读取事务型
设备库。当前管理员 enrollment/policy/revoke 路由仍通过 server
`storageManager` 写策略，生产部署必须让这些管理操作与 adapter 的
`devicePolicy` 使用同一事务数据库，或在边界代理处禁用仓库内管理路由并使用
受审计的外部管理面。JSON 文件不能作为多写生产策略库。

## KMS/HSM permit signer

生产不得设置 `WEB_CONFIG_AUTH_PRIVATE_KEY_PEM` /
`WEB_CONFIG_AUTH_PRIVATE_KEY_FILE`，也不得把可导出私钥放进容器。生产代码不会
把这些变量作为 KMS fallback。
[`server/src/device-auth-v2.js`](../server/src/device-auth-v2.js) 的
`DeviceAuthV2Service` 接受注入的 `permitSigner`，其 `sign(claims)` 必须：

1. 按 `hbox_device_session_permit_v1_t` 生成固定 172B signed bytes；
2. 调用指定 KMS/HSM P-256 key 对这 172B 做 ECDSA/SHA-256；
3. 把 DER 签名规范化为固定 64B `r || s`；
4. 返回完整 236B permit；
5. 把 firmware 中已开放的 current/next slot 写入 byte 5。

KMS policy 只允许服务身份调用 `Sign`，禁止 Export/CreateKey/DeleteKey。每次调用
记录 KMS key ID、slot、permit ID、device ID、policy version 和结果，但不得记录
完整 permit、签名输入、nonce 或 token。

`BinaryP256PermitSigner(local PEM)` 只用于非生产开发和隔离测试。轮换按“固件
先信任 next → KMS 切 next slot → 观察至少一个最长升级周期 → 新固件移除旧
slot”执行，不允许直接覆盖唯一可信公钥。

## Redis 与持久设备策略

内置 `MemoryChallengeStore`、`OpaqueTokenStore` 和
`SlidingWindowRateLimiter` 只适用于一个 Node.js 进程，且在生产模式不会满足
adapter 门禁。多 worker/多主机必须注入共享实现：

### challenge store

- `challengeId` 128 bit、nonce 256 bit，均由 CSPRNG 生成；
- TTL 固定 60 秒；
- 记录 origin、remote/rate-limit binding、scope mask 和可选 browser public key；
- 创建使用 `SET NX EX/PX` 或等价事务；
- 消费使用 Redis `GETDEL` 或单条 Lua 脚本，必须先删除再做密码学验证；
- 全集群执行 per-remote 与全局 outstanding challenge 限流；
- 不允许失败重试复用同一 challenge。

### opaque token store

- token 至少 256 bit，TTL 不超过 300 秒；
- Redis key 使用 token 的 HMAC/SHA-256 索引，避免把 bearer token 明文写入
  dump、诊断或 metrics label；
- `resolve`、`revoke` 和 TTL 在所有节点一致；
- 每次受保护请求重新检查设备 `status` 与 `policyVersion`，吊销或 policy
  更新应在下一次服务器请求时使旧 API session 失效；
- 是否在部署重启时保留不足 5 分钟的 token 必须作为显式策略；默认建议使用
  deployment epoch 前缀使重启失效。

设备登记、firmware measurement allowlist、minimum security version 和吊销信息
需要事务型持久数据库。当前 JSON 文件存储不是多写生产数据库。数据库需对
device ID、certificate serial 和 certificate fingerprint 建唯一索引，并对
policy version 做单调递增更新。

Redis/数据库不可用时认证和受保护下载必须返回 503/拒绝，不能切回 V1 weak auth
或公开下载。

### 吊销传播边界

服务器可在下一次 API/download 请求时通过共享 `devicePolicy.get()` 拒绝并撤销
opaque token；但已经安装到 STM32 的 permit 是离线可验证的自包含结构，设备
不会为每条 WebHID 命令回查服务器。因此吊销不能瞬时杀死已建立的本地 AES-GCM
会话。最坏传播时间是 permit/token 的 5 分钟 TTL；USB 断开、PI10 掉电、角色
变化、显式 `session.end` 或协议错误会更早销毁会话。产品和运维文档必须明确这
一上限，不能宣称“实时远程吊销设备当前会话”。

## WebConfig 设备身份与邮箱账号

设备通过 V2 证书、启动证明、签名 transcript 和已登记设备策略校验后，服务端才把
证书中的 16 字节唯一 `deviceId` 映射到一个 UUIDv4 设备账号；首次成功连接自动创建，
后续连接稳定复用。不得增加一个仅凭前端提交 `deviceId` 就能注册设备账号的公开接口。

设备账号映射保存在 `HBOX_SERVER_DATA_DIR/accounts.sqlite3`。SQLite 启用 WAL、foreign
keys 和事务迁移；未来设备功能表只通过外键关联 `accounts.uid`。内部 UID 会写入
当前连接绑定的设备 Bearer session，退出网页或设备断开时随设备会话失效，不使用
用户 Cookie，也不改变既有设备登记、撤销和固件策略门禁。

独立邮箱账号保存在 `HBOX_SERVER_DATA_DIR/user_accounts.sqlite3`，使用另一套 UUIDv4
用户 UID、Argon2id 密码哈希和 7 天 HttpOnly Cookie 会话。邮箱账号可以在没有设备时
注册和登录，不自动等同或合并任何设备账号。完整的 Resend、`st-dash.com` 发信域名、
接口和反向代理配置见 [`server/doc/EMAIL_AUTH.md`](../server/doc/EMAIL_AUTH.md)。

## HTTPS 与浏览器部署

生产页面和 `/api/v2/device-auth/*` 建议同一 origin。WebHID 需要 secure context，
且设备选择器必须由用户手势触发。

环境只配置精确 origin，不使用 `*`：

```text
WEB_CONFIG_ORIGINS=https://firmware.st-dash.com
```

TLS 终端必须：

- 只公开 443，80 只做 HTTPS 重定向；Node/PM2 端口仅监听 loopback 或私网；
- 使用 TLS 1.2/1.3、自动证书续期和失败告警；
- 保留客户端 `Origin` 与 `Authorization` header；
- 对 `/downloads/*` 禁止共享缓存和访问日志中的 token/query；
- 不加载第三方运行时脚本、标签管理器、广告或远程字体。

API 和非 HTML 静态资源至少返回：

```text
Strict-Transport-Security: max-age=31536000; includeSubDomains
Content-Security-Policy: default-src 'self'; base-uri 'self'; frame-ancestors 'none'; object-src 'none'; script-src 'self'; script-src-attr 'none'; style-src 'self' 'unsafe-inline'; img-src 'self' data:; connect-src 'self'; font-src 'self'; form-action 'self'
Permissions-Policy: hid=(self), camera=(), microphone=(), geolocation=()
Referrer-Policy: no-referrer
X-Content-Type-Options: nosniff
X-Frame-Options: DENY
Cross-Origin-Opener-Policy: same-origin
Cross-Origin-Resource-Policy: same-origin
```

Next 静态导出的每个 HTML 文件可能包含不同的内联 bootstrap script。
`server/src/hosted-webconfig.js` 会对“生成后的精确 HTML 字节”逐段计算
`sha256-...`，并为该文件覆盖 CSP：

```text
script-src 'self' 'sha256-<该 HTML 的实际 hash>' ...;
script-src-attr 'none'
```

这些 hash 不能硬编码在 Nginx 模板中，也不能跨页面复用。传输压缩不影响策略，
但 CDN/代理不得在 Node 计算 hash 后注入或改写解压后的 HTML 内容；代理也不得
用无 hash 的 CSP 覆盖 Node 响应。绝不以 `script-src 'unsafe-inline'` 解决构建
差异。

`style-src 'unsafe-inline'` 是当前 Next.js 样式兼容项；发布前应记录它，后续使用
nonce/hash 移除。静态部署的 `index.html` 使用 `no-cache` 或短缓存，带内容哈希
的 JS/CSS 可 immutable cache。认证响应与 protected downloads 必须
`Cache-Control: no-store` / `private, no-store`。

Nginx、CDN 与 Node 三层的重复 header 必须在 staging 用浏览器网络面板和自动
测试核对，避免代理覆盖 CSP/CORS。禁止把 WebHID permission 当设备真伪认证；
它只代表用户授权当前 origin 访问所选 HID collection。

## 服务端机密与日志

- secret 通过 workload identity 挂载/调用，不进入 `.env`、镜像层或 CI artifact。
- 制造 CA 只部署公钥；authorization 服务只取得 KMS key ID；firmware release
  私钥不进入 WebConfig server，服务端仅挂载用于验签的 P-256 公钥。
- 日志对 `Authorization`、cookie、nonce、certificate bytes、attestation、
  signature、permit 和 ECDH key 做字段级删除。
- device ID、证书指纹、policy version 和错误码可用于审计；IP 保留遵循隐私政策。
- 管理员 enrollment/policy/revoke API 使用独立强认证、最小权限和完整审计。
- firmware download URL 不接受 query token；只能使用内存 bearer token header。

## OTA 上传包精确验证

管理员上传的每个 Slot A/Slot B OTA ZIP 都必须带有由发布工具生成的
`manifest.json` 和恰好 807 字节的 `metadata.bin`。服务端在保存目录和固件目录
更新前执行 fail-closed 校验：

- ZIP 只允许 `manifest.json`、`metadata.bin` 和 manifest 中声明为 active 的
  component，缺文件、重复文件、路径穿越或额外文件均拒绝；
- `metadata.bin` 的大小、manifest descriptor 的大小/SHA-256、metadata CRC32、
  magic/version/target slot/hardware version/firmware version 必须逐字节匹配；
- 每个 component 的名称、地址、大小、SHA-256、active 状态和保留字段必须与
  manifest 及 ZIP 内容一致，整体 firmware hash 也必须一致；
- security version、签名算法、签名长度、authorization key mask、保留区和可选
  hosted webresources 语义必须是规范编码；
- 服务端把 CRC、firmware hash 和 signature 字段归零后重建 canonical metadata，
  使用 `FIRMWARE_RELEASE_PUBLIC_KEY_FILE` 指定的 P-256 公钥验证 64 字节
  `r || s` ECDSA/SHA-256 签名。

因此，仅修改 `manifest.json`、重新计算 ZIP CRC 或 metadata SHA-256 不能绕过
门禁；缺失、未签名、签名被替换、字段不一致或包含额外内容的包都会在上传阶段
被拒绝。生产发布任务必须先生成并签署精确 `metadata.bin`，再把同一公钥部署到
服务端验证节点。

## 发布前自动化

最低自动门禁：

```powershell
python -m unittest tools.tests.test_device_identity_provisioning -v
python -m unittest tools.tests.test_release_ota_gate -v
python tools/check_rf_frozen.py --require-rx-binary

cd server
npm test

cd ../application/www
npm test
npm run build:hosted
```

生产 CI 还必须：

- 只向构建容器提供公钥 trust header，并设置同一个绝对路径
  `HBOX_TRUST_HEADER`；
- 设置审批清单中的 `HBOX_TRUST_HEADER_SHA256`；`tools/release.py auto` 会
  对文件重新计算 SHA-256、检查已配置 marker/auth key mask、拒绝私钥文本和
  默认无 header 的开发构建，不一致时中止；
- 验证 bootloader/application 使用同一个 `HBOX_TRUST_HEADER`，并确认最终
  manifest 的 `trust_bundle_sha256` 等于审批值；
- 验证 firmware manifest 的完整 32B SHA-256、P-256 签名和 security version；
- 对 staging origin 执行 CSP/CORS/HSTS/Permissions-Policy 响应测试；
- 扫描静态 bundle，禁止第三方 origin、source map secrets 和 embedded private key；
- 验证 KMS key slot 与 firmware current/next mask 一致；
- 验证 Redis 原子消费、跨节点重放和吊销传播。

示例（仅展示门禁变量；路径内只能是已审批的生产公钥）：

```powershell
$env:HBOX_TRUST_HEADER = "D:/secure-build/hbox-production-trust.h"
$env:HBOX_TRUST_HEADER_SHA256 = `
  (Get-FileHash $env:HBOX_TRUST_HEADER -Algorithm SHA256).Hash.ToLowerInvariant()
python tools/release.py auto --version 2.0.0
```

本地计算出的 hash 不能自行成为“审批值”；CI 必须先与独立批准清单比对，再传给
release 进程。

## 不可由桌面测试替代的硬件门禁

### 身份与安全启动

- 实际 STM32 TRNG 连续生成、上电健康和故障注入时均 fail-closed。
- `Kdev` 生成后从未出现在 USB/SWD/UART/日志或主机内存中。
- STM32H750xB 没有可用于本方案的 OTP；不得写入只读 system Flash
  `0x1FF0F000`。实机确认唯一 128KiB 内部用户 Flash sector 的实际 revision
  行为。
- linker map 保证 bootloader code 止于 `0x0801BFFF`；identity 使用
  `0x0801C000..0x0801CFFF`（14 个 288B commit-last slot），security-version
  journal 使用 `0x0801D000..0x0801FFFF`（384 个 32B 追加记录）。
- identity/security-version 与 bootloader 同属一个物理 erase sector。每个
  32B 编程边界断电只能得到完整记录或 fail-closed 状态，现场流程不得擦除该
  sector；返厂擦除必须把 bootloader、身份、最低安全版本、重新发证和旧证书
  吊销作为一个事务。
- 正式 bootloader 当前默认
  `HBOX_DEVICE_IDENTITY_PROVIDER_READY=0`；量产 provider、一次性工厂端点和
  option-byte 顺序必须按 `DEVICE_IDENTITY_PROVISIONING.md` 完成实机评审后才
  能启用，不能把 portable journal 单元测试等同于量产置备。
- 启用仓库内 internal-Flash provider 不再要求精确 silicon `REV_ID`。运行时始终
  校验 `DEV_ID=0x450`、RDP1、SECURITY/Secure mode、完整 128KiB SCAR 与内部
  Flash 执行位置。只有勘误/兼容性评审要求时，才同时设置
  `HBOX_STM32H750_REVISION_QUALIFICATION=1` 和批准的
  `HBOX_STM32H750_REVISION_ID`，启用额外精确 revision 门禁。该门禁不能作为产品
  身份；产品归属、唯一设备身份和 PCB 版本必须由制造证书、`Kdev` 持有证明及
  证书内硬件版本策略建立。
- 复制 VID/PID、序列号或 208B 证书到另一板，无 `Kdev` 时认证失败。
- unsigned、篡改、错误 hardware target、低 security version 固件不启动。
- A/B 启动、回退和恢复模式不能绕过签名与 anti-rollback policy。
- RDP/PCROP/Secure Access 生效后重启、升级、返厂恢复和调试行为符合批准策略。

### 在线认证与授权

- challenge/permit 重放、过期、错误 origin、错误 browser/device ECDH key 均失败。
- 设备接收方向严格要求 `sequence == last + 1`；前向 gap、重复、后退或回绕均
  销毁 session。
- 浏览器在 bootstrap 和认证阶段都要求严格连续。V1 使用全局 report sequence，
  接收端无法证明缺失的是可丢 `PERF_SAMPLE` 还是控制响应，因此任何前向 gap
  都先清空 fragment/checkpoint assembler、拒绝 pending RPC，再销毁 session。
  重连认证成功后通过新 checkpoint 重建遥测缓存，绝不跨 gap 拼接旧消息。
- revoked device、降低 minimum security version、错误 firmware measurement 失败。
- 每个 scope 做正/负矩阵；无 `firmware.update` 不能下载或升级。
- 服务器、Redis、KMS 任一不可用时只允许公开设备信息，不允许离线写配置。
- authorization current/next 两次方向的真实轮换演练通过。
- 已建立 permit 的吊销最坏传播时间不超过 5 分钟，并验证断开/角色变化可提前
  清除；不得把服务器 token 的即时拒绝误报为设备端即时吊销。

### WebHID、CH585 与 100Hz 遥测

- FS 和 HS 各连续运行 100Hz 30 分钟，达到既定丢包/延迟门限。
- `PERF_EDGE` 零丢失且顺序正确；sample gap 后 checkpoint 能恢复。
- 浏览器后台、主线程阻塞、Worker 重启、拔插和重新授权后不突发旧数据。
- OTA/图片并发时降采样、credit/backpressure 和控制优先级符合设计。
- 板级实体确认已实现为原始 PC6 `GPIO1` + PC9 `FN` 双键手势：必须处于物理
  USB 档且 CH585 已锁定 Maintenance role，双键先连续释放 50ms，再同时按住
  2 秒（按下消抖 30ms），产生一个 10 秒内可消费一次的授权。输入直接读取
  active-low GPIO，不接受逻辑映射、宏、USB 或 RF 输入；角色/物理档变化、
  超时或一次消费都会清除授权。创建升级会话和重启仍需完成实机按键时序与
  误触测试，未通过前不得解除 OTA 量产门禁。
- USB 断开、suspend、PI10 掉电或角色切换都会终止当前 transport generation：
  STM32 销毁会话，CH585 清空双向旧报告并把 credit 归零；resume 后必须重新认证，
  不允许突发发送 suspend 前的响应或边沿。
- 显式 session reset 同样先同步清空 CH585 WebConfig channel、在途 USB 报告和
  STM32 分片状态，再返回控制 ACK；STM32 必须等 CH585 reset 后的新 credit 才能
  从 fragment 0 开始发送下一代报告。复位时若端点已有 pending DONE，必须先在
  SIE idle 后推进对应 DATA toggle，避免 host/device PID 失步；OUT 仅在
  `TOG_MATCH` 时推进。SIE 在 1ms 内未空闲时保持端点关闭、强制 USB detach，
  CLEAR_FAULT 返回失败，必须重新枚举和认证。
  正常复位时 EP2 也保持 NAK，直到 CH585 BoardLink 与 STM32 WebHID session
  reset 都完成后才在同一临界区恢复 ACK，旧代 OUT 无法穿过同步复位边界。
- Maintenance 模式频谱仪确认 RF PHY 未初始化；RF 模式确认 WebHID parser/IRQ
  未运行。
- CH585 固件确认不含制造私钥、authorization 私钥或 session key 持久化。

### RF 回归

- frozen manifest 全部一致；
- 10B/14B/7B/12B golden vectors 逐字节一致；
- RX binary、配对/跳频/ACK 和 8K 125µs 时隙基线不变；
- WebHID/USB 编译宏不改变 RF 核心对象。

只有制造、安全、固件、服务端和 QA 五方完成上述签字，V2 才能解除
“engineering validation”状态。缺少安全元件的当前 PCB 仍只能声明
“受保护 MCU 软件根信任”，不能声明抵抗实验室级探针或故障注入。

截至本文更新，仓库自动测试不能替代以下未完成事项：真实 KMS/共享存储 adapter
部署、工厂一次性身份端点与治具、RDP/PCROP/Secure Access 顺序、板级实体确认
手势实机验证、FS/HS 100Hz 30 分钟测试、拔插/掉电/角色切换测试及 RF 频谱
回归。任一项缺失都不得解除量产门禁。
