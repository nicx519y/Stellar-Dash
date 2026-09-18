# HBox V2 防降级安全版本存储

## 当前硬件结论

本仓库现在显式缩短 bootloader linker region，并为安全状态保留尾部 16KiB；
这解决代码覆盖问题，但不会把同一 erase sector 伪装成独立硬件安全存储。

- bootloader 链接脚本只允许 load section 使用
  `0x08000000..0x0801BFFF`（112KiB）。
- device identity 固定为 `0x0801C000..0x0801CFFF`，security-version journal
  固定为 `0x0801D000..0x0801FFFF`。常量由
  `common/internal_flash_security_layout.h` 统一，并有 linker/test 重叠门禁。
- STM32H750xB 的内部用户 Flash 只有一个 128KB sector。即使当前镜像尾部仍为
  擦除态，更新或返厂重刷这个 sector 也会同时擦除 bootloader 和尾部数据；它
  不是独立的 monotonic counter。现场代码禁止 sector erase。
- 当前仓库的 STM32H750 CMSIS 目标头文件没有定义
  `FLASH_OPTCR_PG_OTP`，因此 HAL 的 `FLASH_TYPEPROGRAM_OTPWORD` 路径不会为
  本目标编译。HAL 在其他受支持 H7 目标上使用的条件式 OTP 编程窗口是
  `0x08FFF000..0x08FFF3FF`。
- 旧设备身份契约使用 `0x1FF0F000`，但 RM0433 的 H750xB memory map 将
  `0x1FF00000..0x1FF1FFFF` 定义为只读 system Flash；它不是可写 OTP。因此
  新布局不再使用该地址。

ST 的 [RM0433 参考手册](https://www.st.com/resource/en/reference_manual/dm00314099-stm32h742-stm32h743-753-and-stm32h750-value-line-advanced-arm-based-32-bit-mcus-stmicroelectronics.pdf)
也说明 STM32H750xB 的 bank 1 仅有一个 128KB sector。

预留区不等于量产验证完成。因此当前实现仍采用显式 provider 门禁：仓库默认镜像
可以编译，但
`SecurityVersionStore_Load()` 返回 `provider-unavailable`，安全 bootloader
拒绝跳转。量产镜像必须注入经过评审和实机断电测试的 provider。

## 启动与升级语义

权威流程如下：

1. bootloader 读取 provider 中已经制造预置的最低安全版本。
2. 元数据的 `security_version` 低于该值时，签名正确也拒绝。
3. bootloader 验证 manifest 发布签名，并完整验证目标 slot 的所有组件 SHA-256。
4. 只有完整 slot 已被接受后，才调用
   `FirmwareSecurity_CommitValidatedSecurityVersion()`。
5. provider 原子、单调地推进到新版本；随后必须读回同一值。
6. 推进失败、读回失败、provider 未配置、记录损坏或日志已满均进入受控恢复，
   不产生 application handoff 和 Boot Attestation。
7. 版本相等是幂等操作；任何降低请求都不写入并返回 `rollback`。

推进发生在跳转 application 前。因此一旦新签名版本被接受，复位也不能重新打开
较低版本。发布方必须把“已签名但不能启动”的风险纳入分阶段发布和恢复镜像设计；
恢复镜像的 `security_version` 也必须不低于已提交值。

## Provider 接口

量产 provider 源文件实现：

```c
hbox_security_version_status_t
HBoxSecurityVersionProvider_Load(uint32_t *minimum_version);

hbox_security_version_status_t
HBoxSecurityVersionProvider_Advance(uint32_t requested_minimum_version);
```

构建时显式注入：

```powershell
make -C bootloader clean all `
  HBOX_SECURITY_VERSION_PROVIDER_READY=1 `
  HBOX_SECURITY_VERSION_PROVIDER_SOURCE=D:/secure-build/hbox_version_provider.c
```

选择仓库内 STM32H750 internal-Flash provider 时，不得再传通用 source/ready：

```powershell
make -C bootloader clean all `
  HBOX_SECURITY_VERSION_INTERNAL_FLASH_PROVIDER=1
```

silicon `REV_ID` 是可选的芯片勘误/兼容性资格门禁，不是 HBox 产品身份，也不再是
启用 identity/anti-rollback provider 的前置条件。确有独立批准清单时可显式启用：

```powershell
make -C bootloader clean all `
  HBOX_SECURITY_VERSION_INTERNAL_FLASH_PROVIDER=1 `
  HBOX_STM32H750_REVISION_QUALIFICATION=1 `
  HBOX_STM32H750_REVISION_ID=0x1003
```

`0x1003` 仅为命令格式示例。运行时始终核对 `DEV_ID=0x450`、RDP1、
SECURITY/Secure mode、覆盖完整 128KiB 的 SCAR 和内部 Flash 执行位置；只有显式
启用 qualification 时才额外核对精确 `REV_ID`。产品身份由制造证书和 `Kdev`
持有证明建立。

只传 source、不打开 ready，或打开 ready 但不传 source，Makefile 都会停止。默认
`READY=0` 不提供可被误认为量产安全的 RAM/QSPI/内部 Flash fallback。

Provider 必须满足：

- 地址必须位于 `0x0801D000..0x0801FFFF`，不得接触 identity
  `0x0801C000..0x0801CFFF` 或 linker code 区；
- 内部 Flash provider 只允许 32B 追加编程，现场绝不允许擦除单一 128KiB
  sector；返厂全 sector 重刷必须重新建立身份和最低版本，并由制造数据库/吊销
  系统阻止安全版本回退；
- 普通 application、WebHID、SWD 生产策略和服务器不能降低或擦除该值；
- `Advance` 在任意写入边界掉电后只能表现为旧值、新值或 fail-closed，不能接受
  中间/伪造值；
- 未制造预置返回 `unprovisioned`，读写或鉴权失败返回明确错误；
- 不得把“空存储”自动采用为升级包中的版本；
- 推进后进行独立读回，且设备日志只记录状态和版本，不记录密钥。

若下一版 PCB 增加安全元件，优先使用其硬件 monotonic counter。当前预留的
12KiB journal 最多容纳 384 条记录，但在实际芯片 revision 的 32B 编程、断电、
RDP1/Secure Access 和工厂全 sector 重建流程通过验证前，仍不得把内部 Flash
provider 标记为量产就绪。

## 可移植追加式 journal

[`common/security_version_journal.c`](../common/security_version_journal.c)
提供给具有受控、原子记录存储的 provider 使用：

- 每条固定 32B；
- 包含 ordinal、最低版本及其按位反码、前记录 CRC、当前记录 CRC 和 commit
  marker；
- 记录必须从 index 0 连续追加，版本严格上升；
- 中间空洞、重排、位翻转、部分写、重复 ordinal 或 CRC 链断裂全部返回
  `corrupt`；
- 全擦除返回 `unprovisioned`，只能由制造流程调用 `Provision()`；bootloader
  运行时只调用 provider 的 `Load/Advance`；
- backend 的 `program_record_atomic()` 必须保证完整 32B 或完全不变。若硬件
  不具备这个原子粒度，应使用硬件 monotonic counter 或在 provider 内实现更强的
  事务，不得虚报该契约。

## 验收

主机测试：

```powershell
python -m unittest tools.tests.test_security_version_journal -v
```

量产 provider 还必须完成：

- erased、未锁定、错误 lock mask、单 bit 损坏、记录空洞和 CRC 链损坏测试；
- 每个物理编程边界的断电注入；
- 从版本 N 升到 N+1 后，版本 N 的签名固件拒绝启动；
- 相等版本重复启动不消耗 journal；
- journal 容量预警和耗尽时受控恢复；
- field bootloader/升级流程无法触发全 sector erase；
- 返厂全 sector 重刷的工单、旧证书吊销和最低版本恢复不能把设备重新开放到较低
  security version；
- release 签名失败或组件 hash 失败时绝不调用 `Advance`；
- provider 缺失的构建在实机上拒绝 application handoff。
