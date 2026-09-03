# HBox V2 设备身份制造与密钥注入

本文定义 V2 主板的生产身份流程。它不是“生成一把测试密钥再烧录”的说明。
仓库不包含任何生产私钥，默认公钥全部为零；未注入生产信任根的固件会按设计
拒绝安全启动或 WebConfig 授权。

权威二进制格式位于
[`common/device_security_protocol.h`](../common/device_security_protocol.h) 和
[`common/device_identity_store.h`](../common/device_identity_store.h)：

| 结构 | 固定大小 | 签名/校验范围 |
|---|---:|---|
| `hbox_device_certificate_v1_t` | 208B | 制造 CA 对前 144B 做 ECDSA P-256/SHA-256 |
| `hbox_device_identity_record_v1_t` | 256B | 跳过 8..11 字节后的整记录 CRC32 |
| `hbox_device_identity_commit_v1_t` | 32B | 跳过 20..23 字节后的整 flashword CRC32 |

`deviceId = SHA-256(SEC1 uncompressed device public key)[0..15]`。证书签名为
固定 64B `r || s`，所有多字节整数均为 little-endian。

证书 TBS 的 `product_id_le`（offset 129，4B）是制造商分配的产品族标识；
当前 HBox 的线性字节固定为 ASCII `HBOX`，即 little-endian
`0x584F4248`。`hardware_version_le` 是 PCB revision，按
`MAJOR.MINOR.PATCH` 编码为 `major << 16 | minor << 8 | patch`。两者都位于
制造 CA 的签名范围内，不能从 STM32 `DEV_ID`、`REV_ID`、USB VID/PID 或浏览器
提交的普通 JSON 字段推断。offset 133..143 仍为保留区并必须全零。

## STM32H750 内部 Flash 布局

STM32H750xB 只有一个 128KiB 内部用户 Flash sector，编程粒度为 32B，且本目标
没有可用 OTP。`0x1FF0F000` 属于只读 system Flash，不能保存身份。

权威布局由
[`common/internal_flash_security_layout.h`](../common/internal_flash_security_layout.h)
和两个 bootloader linker script 共同固定：

| 区域 | 地址 | 大小 | 约束 |
|---|---:|---:|---|
| bootloader code | `0x08000000` | 112KiB | linker 不允许越过 `0x0801BFFF` |
| device identity | `0x0801C000` | 4KiB | 14 个 288B 一次性 slot |
| security version | `0x0801D000` | 12KiB | 384 个 32B 追加记录 |

identity 与 security-version 区域不重叠。它们仍与 bootloader 同属一个物理 erase
sector，因此现场固件绝对不能擦除该 sector。正常升级只写外部 QSPI；安全版本只
能追加 32B 记录；身份只允许工厂首次写入。返厂若要擦除内部 sector，必须把
bootloader、身份和最低安全版本视为一个事务重新烧录、重新发证并吊销旧证书。

每个 identity slot 为 `256B record + 32B commit`。工厂端先逐个写入并读回前
8 个 flashword，最后一次 32B 编程才写 commit。掉电产生的未提交 slot 永远
不会被 bootloader 接受；重试使用下一个完全擦除的 slot，不执行 sector erase。
一旦存在一个有效 commit，任何再次置备都会返回 `already-provisioned`。

为防止旧工具误擦这一个 sector，`make -C bootloader flash` 与
`python tools/build.py flash bootloader` 已默认禁用。仓库尚未提供可量产的
“bootloader + identity + minimum security version”全 sector 原子返厂工具；
在该工序完成评审、旧证书吊销和断电验证之前，不得绕过此门禁。

未置备开发板使用独立的
`python tools/hbox.py flash bootloader-dev`。该入口先读取并确认 identity 与
security-version 的完整 16KiB 保留区全为 `0xFF`，随后备份整个 128KiB sector，
才允许擦除、烧录和读回校验。任何非 `0xFF` 字节、读取失败或备份失败都会在
擦除前终止；该入口不是生产设备的 bootloader 更新或返厂恢复工具。

## 密钥边界

生产必须隔离三套密钥：

- 制造 CA 私钥：离线 HSM，唯一用途是签设备证书；不得进入服务器、CI 或工厂
  普通工作站。
- 固件发布私钥：离线签发系统，唯一用途是签 firmware manifest。
- WebConfig authorization 私钥：在线 KMS/HSM，为 5 分钟 permit 签名；不得以
  可导出 PEM 形式部署到生产服务器。

每块主板的 `Kdev` 必须由板上 STM32 TRNG 生成并留在 MCU 内。主机只接收公钥、
CSR、证书、设备 ID 和指纹。禁止通过 USB、SWD、日志、数据库或制造审计记录
导出 `Kdev`。

## 推荐工厂状态机

生产需要一个受控的、一次性工厂固件/安全服务。仓库当前提供二进制格式工具，
但尚未提供这个工厂端点；在端点和治具完成实机验证前，不能把 V2 身份流程标记
为量产就绪。

1. 治具校验芯片型号、板版本、供电和 TRNG 健康状态。
2. STM32 内部生成 `Kdev`，只返回 SEC1 P-256 公钥。
3. 设备生成并自签 PKCS#10 CSR；若不实现 ASN.1，则设备对
   `HBOX-FACTORY-POP-V1\0 || 32B factoryChallenge` 签名，工站验证 PoP。
4. 工站生成固定 144B certificate TBS，并把 TBS 哈希送离线制造 CA/HSM。
5. HSM 返回 ECDSA P-256 签名；工站先用 CA 公钥验证，再组装 208B 证书。
6. 工站只把证书传回设备。设备确认其中公钥等于自身 `Kdev` 的公钥。
7. MCU 内部组装 256B identity record，计算 CRC，逐 flashword 写入 linker
   预留区并读回；最后写入独立 32B commit flashword。不得在主机上组装生产
   identity slot，因为其中包含 `Kdev`。
8. 设备端只返回脱敏验证结果：device ID、证书序列号、证书指纹、记录 CRC 和
   lock 状态；禁止回读私钥槽。
9. 完整掉电重启，用正式 bootloader 产生 Boot Attestation，并在 staging
   server 完成一次在线认证。
10. 管理员把证书登记到 V2 设备库，记录批次和证书指纹。重复 device ID、序列号
    或公钥必须阻断整批流程。
11. 应用经批准的 RDP/PCROP/Secure Access/调试口策略，然后再次掉电验证。

仓库实现了布局、portable commit-last 状态机和一个显式选择的内部 Flash
provider，但还没有完成实际 `STM32H750XBH6` revision 的工厂治具、Secure
Access 流程、所有 32B 边界断电和 option-byte 顺序验证。因此这些代码不是
“量产写入已验证”的声明，不能仅依据本工具执行不可逆操作。

正式 bootloader 默认
`HBOX_DEVICE_IDENTITY_PROVIDER_READY=0`，读取身份也会 fail-closed。经过实机评审
后才能显式选择仓库内 provider：

```powershell
make -C bootloader clean all `
  HBOX_DEVICE_IDENTITY_INTERNAL_FLASH_PROVIDER=1
```

精确 silicon `REV_ID` 不再作为产品身份或 provider 前置条件。如勘误评审要求限制
某个 revision，另行同时设置 `HBOX_STM32H750_REVISION_QUALIFICATION=1` 与
`HBOX_STM32H750_REVISION_ID=<批准值>`。HBox 产品归属和 PCB 版本必须由制造商签名
证书中的产品字段/`hardwareVersion` 及 `Kdev` 持有证明决定。

工厂镜像若要运行 MCU 内 Kdev 注册服务，必须同时显式打开：

```powershell
make -C bootloader clean all `
  HBOX_SECURITY_VERSION_INTERNAL_FLASH_PROVIDER=1 `
  HBOX_DEVICE_IDENTITY_INTERNAL_FLASH_PROVIDER=1 `
  HBOX_DEVICE_IDENTITY_FACTORY_PROVISIONING=1 `
  HBOX_FACTORY_IDENTITY_ENROLLMENT=1
```

其中 `HBoxFactoryIdentityEnrollment_Begin()` 只用 STM32 TRNG 在 MCU 内生成
`Kdev`，返回公钥及
`"HBOX-FACTORY-POP-V1\0" || challenge` 的 raw `r||s` 签名；
`Install()` 验证制造 CA、证书公钥/deviceId、硬件版本后，先初始化 append-only
security-version journal，再通过 commit-last identity backend 安装私钥和证书。
任一安装尝试结束都会清零 pending 私钥和中间量。

工厂构建还必须由受审计的安全服务覆盖
`HBoxIdentityFactoryGate_IsAuthorized()`。弱默认实现始终返回 false；provider
还会在每次写入前复用统一 lifecycle 校验，核对 `DEV_ID=0x450`、RDP1、
SECURITY/Secure mode、完整 128KiB SCAR 和内部 Flash 执行位置；启用可选 silicon
qualification 时才核对编译时批准的 `REV_ID`。覆盖实现必须验证：受认证且不可
重放的治具 session、已批准的 PCB/产品版本和工站工单。
GPIO、UID、普通 USB 命令或仅凭编译宏均不得作为授权。正式 field bootloader
不得包含这个工厂写入开关。

## 制造工具

[`tools/device_identity_provisioning.py`](../tools/device_identity_provisioning.py)
只依赖 Python 3 和 OpenSSL。

### CSR 路径

设备先产生 CSR。工站验证 CSR 的自签名并生成 TBS：

```powershell
python tools/device_identity_provisioning.py make-certificate-tbs `
  --csr D:/factory/session/device.csr `
  --certificate-serial 00112233445566778899aabbccddeeff `
  --product-id HBOX `
  --hardware-version 2.0.0 `
  --issued-at 1784851200 `
  --production-batch 2026-07-A `
  --auth-level 1 `
  --output D:/factory/session/device-cert.tbs
```

`--issued-at` 应由受控工站的可信时间产生。证书序列号必须来自制造数据库的
128-bit 唯一值，不能使用设备 UID。

### 原始公钥 + proof-of-possession 路径

不使用 CSR 时，以下三个参数必须同时提供：

```powershell
python tools/device_identity_provisioning.py make-certificate-tbs `
  --device-public-key D:/factory/session/device-public.pem `
  --factory-challenge D:/factory/session/challenge-32b.bin `
  --proof-signature D:/factory/session/pop-signature-raw64.bin `
  --certificate-serial 00112233445566778899aabbccddeeff `
  --product-id HBOX `
  --hardware-version 2.0.0 `
  --production-batch 2026-07-A `
  --output D:/factory/session/device-cert.tbs
```

challenge 必须由 CSPRNG 生成、只使用一次，并与治具 session 原子绑定。签名是
64B `r || s`。

### HSM 签名与证书组装

HSM 必须对完整 144B TBS 做 SHA-256 后 ECDSA P-256 签名。HSM 命令由供应商
定义，不应写进仓库。得到签名后：

```powershell
python tools/device_identity_provisioning.py assemble-certificate `
  --tbs D:/factory/session/device-cert.tbs `
  --manufacturer-signature D:/factory/session/manufacturer-signature.bin `
  --signature-format raw `
  --manufacturer-ca-public D:/public/hbox-manufacturer-ca.pem `
  --output D:/factory/session/device-certificate.bin

python tools/device_identity_provisioning.py verify-certificate `
  --certificate D:/factory/session/device-certificate.bin `
  --manufacturer-ca-public D:/public/hbox-manufacturer-ca.pem
```

若 HSM 输出 ASN.1 DER，使用 `--signature-format der`。组装命令总会先用制造 CA
公钥验证签名，失败时不会产生证书。

### 开发专用内部 Flash slot 命令

`development-build-flash-slot` 与 `development-verify-flash-slot` 会接触包含
私钥的 288B 文件，只用于格式单元测试。它们要求显式
`--acknowledge-private-key-export-risk`，输出权限尽可能收紧到当前用户。

这些命令不得用于生产设备、生产密钥或真实制造记录；生产记录必须在 MCU 内部
组装和验证。

## 生产构建的公钥注入

生成的 trust header 只包含三个体系的公钥，可以进入受审计的构建输入，但不要
提交到通用源码仓库：

```powershell
python tools/device_identity_provisioning.py render-trust-header `
  --manufacturer-ca-public D:/public/hbox-manufacturer-ca.pem `
  --firmware-release-public D:/public/hbox-release.pem `
  --authorization-current-public D:/public/hbox-webconfig-current.pem `
  --authorization-next-public D:/public/hbox-webconfig-next.pem `
  --output D:/secure-build/hbox-production-trust.h
```

current/next 两个 authorization 槽用于轮换；未提供 next 时 mask 只开放 slot 0。
生成后记录 header SHA-256、对应 HSM key IDs、批准人和构建号。

bootloader 与 application 使用同一外部 header：

```powershell
$env:HBOX_TRUST_HEADER = "D:/secure-build/hbox-production-trust.h"
make -C bootloader clean all
make -C application clean all
```

也可把 `HBOX_TRUST_HEADER=...` 直接作为 make 参数。路径建议使用绝对路径和
正斜杠。Makefile 会在路径不存在时立即失败。没有 header 时仍可编译开发镜像，
但 all-zero fallback 保证其不能通过生产身份/授权门禁。

必须同时核对：

- header 的制造 CA 公钥与签设备证书的 CA 相同；
- firmware release 公钥与 release signer 相同；
- authorization slot 与服务器 permit signer 的 `signingKeySlot` 相同；
- bootloader 与 application 的 header 哈希完全一致；
- 轮换时先发布同时信任 current/next 的固件，再切 KMS signer，最后移除旧槽。

## 工厂审计数据

允许保存：

- device ID、证书序列号/指纹、公钥指纹；
- product ID、PCB revision（`hardwareVersion`）、生产批次、签发时间；
- HSM key ID/签名操作审计 ID；
- identity commit/slot、RDP/PCROP/Secure Access 策略版本和最终在线认证结果。

禁止保存：

- `Kdev`、任何私钥或私钥派生中间量；
- raw STM32 UID；
- challenge nonce、完整 permit、API token、会话密钥；
- 能重放工厂 PoP 的完整 session 资料。

## 不可跳过的制造验收

- CSR/PoP 使用错误私钥、重复 challenge 或篡改公钥时签发失败。
- 对证书任意一位做翻转，bootloader 与服务器均拒绝。
- 在 identity record 与 commit 的每个 32B 写入边界断电，设备只能是完整有效
  或 unprovisioned/fail-closed，不能接受半写记录；重试不得触发 sector erase。
- linker map 证明 bootloader load section 结束于 `0x0801C000` 之前，identity
  和 security-version 区域没有任何 loadable section。
- 量产 option bytes 生效后，普通 SWD/USB/应用日志不能读出 `Kdev`。
- 设备证书公钥、设备内私钥派生公钥和 device ID 三者一致。
- 同一个证书或 device ID 重复进站时停止流程并告警。
- 量产镜像没有测试 CA、测试 authorization key 或测试 release key。
- 工厂固件、治具脚本、HSM policy 和 option-byte policy 分别完成双人复核。
