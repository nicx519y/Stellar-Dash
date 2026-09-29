# RF v2 短包与按需测量（RX build 0x1909）

状态：实现、离线回归、STM32/TX/RX 和 connect-monitor 编译完成；本轮未烧录、未重启设备。尚未进行新固件 4K/8K 实机对比，不能据此宣称丢包率或延迟已经改善。

## 空口格式

长度均指交给 RFIP 的协议包长度，不包括 PHY 前导码、地址、CRC 等开销。

| 包 | 长度 | 内容与触发 |
|---|---:|---|
| DATA | 5 B | header0、空口序号、3 B 完整按键状态 |
| DATA + aux | 7 B | 同上 + 2 B 附带字段 |
| 普通 ACK | 5 B | 2 B 头、请求包序号、2 B 链路损失统计 |
| 扩展 ACK / 事务 | 12 B | 连接、配对、跳频，或正常 ACK 中必须下发的配置 |

3 B 状态保留全部 18 个按键；高 6 bit 在测量开启时携带事件关联标识，不映射为按键。每个 DATA 都带当前完整状态，诊断是否完整不影响按键解析。

CONNECT 的协议版本为 2、会话标识为 HOP2。TX/RX 都必须更新，旧版本不能建立兼容的数据会话。配对存储、固定配对信息和下载布局保持原样；握手验证完成前不能用 DATA 静默兼容旧包。

7 B 的两种附带字段：

* 普通分片：2 bit 记录代号 + 6 bit 分片下标，以及 1 B 记录内容。
* 每轮分片前的序号锚点：CMD_PRESENT 标记，附带本包的 16 bit 完整空口序号。它仍是完整按键包，既不是时钟同步，也没有单独的发送时隙。

附带记录为 type、payload length、serial32、payload、CRC32，总长 10–64 B，最大 payload 54 B。最多三轮，只有 RF 启动成功才消费分片；接收端合并各轮收到的片段，完整校验后交付一次。记录不可变，超过边界或 CRC 错误不交付。分片重组 500 ms 超时，测量记录 1 s 过期。

电池变化时排队、每 5 s 补报；TX 统计每秒合并，未发送的旧统计被最新窗口替代；配置/速率回执优先处理。电池、统计、回执、测量均不再生成独立 RF 控制发送。USB 的 RLT2/RHP2 是本机诊断上报，不是空口包。

测量关闭、无配置变化时，一个 32 B 统计 payload 最多占 129 个 7 B 输入包/秒（含三次序号锚点）；2 B 电池 payload 最多占 39 个 7 B 输入包/5 秒。其余普通输入为 5 B。一次完整测量记录最多占 195 个 7 B 输入包，在 8K/4K 理想连续发送下分别约 24.4/48.8 ms 完成附带传输，按键从首包起正常处理。

## 调度与 ACK

正常 ACK 请求仍每 100 ms 一次，搭载在短输入包头，用该包序号作为请求 token；保留失败探测、逻辑窗口和重连路径。扩展信息利用这次回复，不增加回复次数。

5/7 B 共用短包路径，交替 DMA 缓冲。按上一次实际包长计算安全间隔；短请求不依赖可能迟到的 TX_FINISH 判断 RX 窗口归属，而在发送保护间隔结束后进入接收。12 B 连接/跳频等事务保留长包保护、watchdog 和统一事务清理。没有直接移除保护后抢发。

反向 ACK 仍有无线切换及静默接收窗口，不能同时发输入。统计把 ACK 保留槽和长事务保护槽单列；它们不是全部停顿原因。

## 按需相对延迟

connect-monitor 的 Latency 开关默认关闭，程序重新启动也不恢复旧的开启状态。开启指令经已有配置 ACK 到 TX，再经 SPI 通知 STM32；关闭会清理测量积压。桌面每秒通过 USB 给 RX 续期，3 s 无续期后关闭测量并通知 TX；续期本身不产生 RF 包。

测量各段：

1. ADC：采样触发 → ADC 完成。
2. Logic：ADC 完成 → 最终输入状态就绪。
3. SPI wait：状态就绪 → 实际 SPI 传输开始。
4. SPI：实际 SPI 开始 → NSS 结束。
5. TX：TX 侧捕获对应 NSS 结束 → 实际成功启动的 RF 发送尝试。
6. RF≈：按实际 5/7 B、PHY 和配置的启动时间估算到 RX 接收回调边界。
7. RX：接收回调 → 首个对应 USB 报告就绪。
8. USB：报告就绪 → 对应 EP2 IN 的 T_DONE 完成中断。

STM32 记录以 SPI sidecar 附在后续输入传输中，最多重复三次。SPI 总长可能是 38 B，这不代表 RF 包变长。TX 将四个源阶段和最多六个实际成功 RF 发送尝试的序号/等待耗时放进附带记录。RX 按事件、完整 16 bit 空口序号和状态匹配实际收到的尝试，不能仅用相同按键值匹配。

源事件槽、输入队列、分片重组和桌面记录均有界。超过六次 RF 尝试才第一次收到事件、源 sidecar 被快速新事件替换、分片三轮仍缺失或记录过期时，只显示部分阶段，不生成总耗时。TX 槽覆盖/过期有累计计数；STM32 sidecar 被新事件替换没有独立计数，桌面完整记录覆盖率必须与延迟分布同时看。

RX 在 USB 报告提交时绑定事件和行号，只有对应 T_DONE 才结束，失败提交、复位、旧完成回调不能补出有效延迟。重复状态提前准备下一个报告也不能覆盖第一次就绪时间。RF 序号有效性在收包入队时保存，不能被后续锚点追溯修复。

桌面接收两页 RLT2，按会话/行号/修订号更新同一行，先显示可用阶段。时间计算不使用 HID 读取到达时刻、Windows/XInput 时间或 PC 时钟同步。终点是 USB IN 完成；它不等于 Windows 应用读取到按键。RF/IRQ 边界尚未做物理校准，因此总和标为 Total≈，不是精确端到端实测值。

## 统计和验证

RHP2 通过 USB 上报 TX 的 5/7/12 B 成功启动计数、ACK 保留槽、长控制保护槽和测量覆盖/过期计数。原有 RF 序号缺口、CRC、TX due/started/dropped、RX pending/edge 丢弃分别保留。界面原来的 Packet Loss 改名为 Input Deficit，避免把发送不足等同于射频干扰丢包。空口序号在启动尝试时推进，序号缺口也可能包含本地启动失败。

已通过：

* 41 项 native/Python 行为测试：生产短包构造和重组、缺片/重复/CRC/代号回绕、发送失败、ACK 迟到/丢失、事务恢复、计时回绕、SPI 源关联、USB 完成归属/背压、排队序号有效性、测量过期，以及冻结烧录契约。
* 30 项 connect-monitor 测试：相对阶段重组、部分记录、修订/会话复位、现有诊断和显示回归。
* STM32 unlocked-development、TX、RX 编译；监视器 TypeScript 检查和生产构建。

测试入口：

```powershell
python -m unittest tools.tests.test_rf_short_transport tools.tests.test_rf_runtime_recovery tools.tests.test_rf_link_reliability tools.tests.test_rf_trace_transport tools.tests.test_frozen_flash_contract
cd connect-monitor
npm run typecheck
npm run build
node --test tests/relative-latency.test.cjs tests/button-latency.test.cjs tests/rf-diagnostics.test.cjs
```

既有 JSONL 可离线汇总，不重新打开硬件接口：

```powershell
python tools/rf_short_report.py <log.jsonl> --since-ms <开始时间戳>
```

待实机验证：固定频道 39，旧实现 / 新版测量关闭 / 新版测量开启，各跑 4K 与 8K，分别比较有效输入率、RF 序号缺口、CRC、TX 未发送、ACK/控制窗口、RX 软件丢弃、最长输入间隔和延迟 P50/P95/max/完整率。再验证短按释放、单边复位、ACK 丢失、遮挡恢复和开启/关闭测量。不以面板比例下降代替这些验收。

## 交付

匹配归档：`.hbox/rf-short-v9-20260919/`，以其中 manifest 与 SHA256SUMS.txt 为准。

* RX：`RX.hex`，build ID `0x1909`，由用户沿用原普通无锁流程自行烧录。
* TX：`TX.bin` 供既有 `python tools/hbox.py flash tx` 流程使用；归档 `TX_app.bin` 是 0x1000 以上应用，仅作核对，不能当从 0 地址开始的整片镜像。不得覆盖已验收 4 KB IAP。
* STM32：`application-slot-a.bin` 和配套 `metadata.bin`，已有入口 `python tools/hbox.py flash app A`，只更新普通应用槽位及配套提交元数据。
* 监视器：本仓库 `connect-monitor/dist` 已构建，需要重新启动该构建才能使用新开关和解析器。

本次协议需要 STM32、TX、RX 一起更新；仅换 RX 无法完成测量闭环，旧 TX/RX 也不应混用。归档不是更改地址或烧录入口的理由。未改冻结烧录脚本、保护位、锁定状态或 Standby 策略。未安排自动烧录。
