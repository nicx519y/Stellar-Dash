# XORA USB / WebConfig SPI 统一为 15 MHz

2026-09-27，按用户要求，将 USB 高速输入与 WebConfig 的应用数据 SPI 时钟统一为与 RF 驱动相同的 15 MHz。

## 实现

- STM32 USB 高速输入改用 SPI4 120 MHz /8；同步修改快速模式判定和帧传输时间预算。14 字节输入帧的纯线传输时间由约 14.93 µs 缩短至约 7.47 µs；另有原有 20 µs NSS 仲裁保护间隔与软件开销。
- WebConfig `kWebHidSpiHz` 改为 15000000，继续通过能力查询、PREPARE、探测和 COMMIT 建立通道。
- TX 已接受 15 MHz WebConfig 协商，USB 输入 SPI 从机无需改频率常量，因此本轮只烧录 STM32。保留上轮 RAM/CRC/公平调度优化。
- 启动、角色选择与 IAP 仍使用原有 /256 时钟。USB 输入失败恢复策略与 WebConfig 的失败关闭行为不变。
- 未修改 RF 数据路径、自动跳频或恢复 RF 自动采样。

## 验证和部署

- `python -m unittest tools.tests.test_usb_input_fairness tools.tests.test_webhid_startup tools.tests.test_webhid_fast_link`：10 项通过，8.8 秒。新增实际端口函数测试覆盖 /8 选择、幂等启用、初始化失败退回启动时钟、错误内核时钟和未交接时拒绝切换；WebConfig 启动测试使用 15 MHz。
- `python tools/hbox.py web local-build --unlocked-development --slot A --skip-web --jobs 4`：通过，56.4 秒。
- manifest 为 `unlocked-development`、槽 A、无手动生命周期要求。
- `python tools/hbox.py flash app A`：物理回读成功，metadata 最后提交，内部 Flash 一致而跳过写入。STM32 Application 371596 字节，SHA-256 `1bf51a74a7db26fbc52c409c2dad4d40b0f6c95958b312ff37b0ae6838c88500`。
- TX 包仍为 `1e0e78dd4a75424f1c7b4ec17c8853df6779de08a3057c0b9e7f1419d53e9eb7`，本轮未重复刷写 TX。
- 烧录后 USB 读回：请求 8000 Hz、快速输入探测阶段 4、首次快速输入溢出标记 0；硬件 SPI CFG1 为 /8，即 15 MHz。原始结果见 [快照](usb-webconfig-spi15-evidence-20260927.json)。普通缓存对象中的历史 last_fault 不作为本次新错误计数。

本地日志：`.hbox/webhid-hs/spi15-host.log`、`spi15-build.log`、`spi15-stm-flash.log`。

浏览器自动控制返回 `nodeRepl.fetch request failed`，网页测试由用户操作真实浏览器；设备侧能力查询与 SRAM 读取单独记录。用户确认 USB 输入正常，但 WebConfig 连接失败，不能判定两模式都已通过。

## 15 MHz WebConfig CRC 错误调查

故障现场的 Feature 为 HS / 1024 字节 / 15 MHz / bridgeReady=1，表明枚举和启动握手已通过。STM32 首错记录：tick=9496、块长 1056、SPI=15000000，CRC 错误计数为 1；块头 `5b02200401000000020000000000000000000000080000000100000000c3e1f1`，重新计算 CRC 为 `0x97ae94fa`。会话错误记录为空。这将调查定位于配置块传输完整性，不能归为浏览器未弹出设备选择框。

原生 HID 只读诊断曾完成直接会话握手，但首个加密读取写入失败；它不是浏览器端验收结果。失败原始诊断保存在 `.hbox/webhid-hs/spi15-failed-first-fault.log`。

代码检查发现 TX 在 CNT_END 中断中立即释放 DMA/FIFO，即使 NSS 仍低。修复让发送缓冲、FIFO 和方向所有权保留到主机释放 NSS；中途屏蔽 CNT_END 中断源，保留完成标志，由已有主循环收尾。此项不改变 15 MHz 时钟，也不削弱 CRC。

新增实际 IRQ 函数测试在旧代码下失败（片选有效时调用收尾），修改后通过；连同输入/启动/窗口协议共 11 项通过，约 6.3 秒。TX 构建通过，RAM 125580/131072 字节。修复镜像包 SHA-256 `d660620445b388b6941af56c82984bc1fefe63f495408e291a950b87dfbbc5ac`，Application 105656 字节。此次修复是否消除现场 CRC 错误仍须烧录后实机验证，不能只凭测试认定原因已完全证实。

上述 TX 修复已通过既有入口烧录，APPLIED/COMPLETE/100%，但随后只读连接仍超时，STM32 仍记录 1056 字节块 CRC 错误。因此收尾修复保留为所有权保护，不能宣称它是本次 CRC 错误的完整根因。

随后发送一份不含 RPC、配置或密钥的固定 1024 字节测试图样，读取 STM32 DMA 物理接收缓冲。后续短控制传输覆盖了前 4 字节，故不将这 4 字节用于归因；剩余数据中有 191 个错误，异或值全部为 `0x80`，即每字节最高位发生错误。这与连续字节边界的首位建立时间问题相符，不是整段丢包的典型表现。测试脚本为 `.hbox/webhid-hs/spi15_pattern.cjs` / `compare_spi15_pattern.py`。

保持 SCK 15 MHz，为 WebConfig 加入一个 SPI 字节间空闲周期（MIDI=1）。USB 输入与启动/IAP 切换时显式恢复 MIDI=0，避免配置模式时序残留。

该修复的主机定向测试 11 项通过，完整无锁构建通过（57.8 秒），STM32 A 槽普通烧录及物理回读通过。最终 Application 371612 字节，SHA-256 `5cf20963169ab2ec4b48971d42ab0fbceff267167686e40ef678bcc1e42d32e6`；TX 保持上述 NSS 收尾修复版本。

烧录后的硬件读回为 /8、MIDI=1。相同固定图样在排除被后续控制传输覆盖的前 4 字节后，剩余 1020 字节的错误从 191 降到 0，块首错记录也为 0。普通复位后，直接加密会话和 128/512/995/996/1200/1992 字节只读请求全部通过；再将六种长度重复十轮，总计 60 次请求全部返回匹配的事务 ID 和零业务错误，SPI 块错误记录与会话错误记录均为空。Feature 仍为 HS / 1024 / 15 MHz / ready=1 / fault=0。

这些 A/B 结果支持连续字节边界建立时间不足的判断，但未用示波器测量具体建立/保持时间。一个周期的字节间隙保留 15 MHz SCK，理论连续字节传输效率按 8/9 计算；不能把它报告为无间隙的 1.875 MB/s 有效业务吞吐。

原生 HID 检查结束后已普通复位并确认就绪，等待用户真实浏览器重新连接。最终诊断摘要见 [修复证据](usb-webconfig-spi15-repair-evidence-20260927.json)。日志为 `.hbox/webhid-hs/spi15-gap-host.log`、`spi15-gap-build.log`、`spi15-gap-flash.log`、`spi15-gap-readonly.log`；原生 HID 通过不替代浏览器验收。

15 MHz 是 SPI 时钟，不能代替 Windows 8000 reports/s 测量，也不代表 WebConfig 有效吞吐翻倍。长期满载、USB 拔插和温压边界尚未验收。

未修改任何保护位或锁定状态。
