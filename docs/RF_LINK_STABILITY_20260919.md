# RF 稳定性与快速连接实现（2026-09-19）

本文件是本次 TX/RX 链路实现的说明。历史 `design.md`、`pairing_design.md` 和实验记录中
1 秒/500ms ACK、3 秒恢复扫描、旧预约跳频及旧配对提交描述不能作为当前运行参数。

## 交付状态

代码及本地行为验证已实现；本次没有向设备烧录固件。两小时稳定性、100 次实物拔插、
遮挡/干扰、实际 P95 初连/重连时延仍需两端更新后验收，不能用主机模拟测试代替。
RX 由用户自行烧录，TX 继续使用 `python tools/hbox.py flash tx` 的既有无锁流程。
正式配对构建仅用于兼容性编译检查；交付镜像使用固定 bond 调试模式。

## 当前协议与策略

| 项目 | 实现 |
|---|---|
| 默认模式 | `RFH_TEST_FIXED_BOND_ENABLE=1`，地址 `0x6D35B8C9`，发现频道 16/39 |
| 数据 | 7B 输入、12B 控制，支持 1K/2K/4K/8K；TX 等待 STM32 明确设置速率 |
| 初连 | SYN TX/RX 各 20ms、TX dwell 5ms、RX dwell 3ms、连接 ACK 间隔 5ms |
| FINAL | ACK 后立即发送，FINAL_READY 后立即 DATA；100ms 无应答进入 provisional |
| 首个输入 | RX FINAL 后最多等待 150ms；TX provisional 只有合法 LINK_OK ACK 才公开 Connected |
| 普通 ACK | 100ms 逻辑窗口，3 包同 token burst，1200µs RX timeout |
| 失败探测 | 当前窗口最多加两次，间隔 10ms；不会多计逻辑 miss |
| 失联 | 连续 3 个失败窗口或 500ms 无有效 ACK，重新发现；TX 控制发送 watchdog 10ms |
| RX 恢复 | 静默 100ms 后原频道等待 40ms，再回发现频道；有效输入静默 50ms 中立保护 |
| 跳频 | PREPARE/CONFIRM 各 100ms，失败旧/新频道恢复最多 200ms，随后重新发现 |
| 质量 | 3 个连续 1 秒坏窗口触发普通跳频；排队延迟不单独触发跳频 |
| 候选 | 60 秒内实测、分数改善至少 150；未知频道按轮转探索，最多每 30 秒一次 |
| 紧急切换 | 仍有合法 ACK，连续 3 个 ACK 间隔损失 ≥50%；尝试至少间隔 5 秒 |
| 试用/回退 | 3 秒评估实测改善；失败用新的 PREPARE/CONFIRM 事务协商回退 |
| 防抖 | 普通冷却 30 秒、失败频道隔离 60 秒，重连不会清除历史 |

固定频道也先在共同发现频道握手，再协商回指定 DATA 频道；RX 在新握手后重发运行配置，
避免单边重启造成 auto/manual 不一致。重复 PREPARE ACK 可以重试 CONFIRM，但不能重置
首次恢复开始后的 200ms 总期限；过期 CONFIRM 不能越过超时状态。

RX 的约 4 秒人为启动等待已去除，保留 BLE/HAL/RF Role/USB 初始化顺序。
RF 初始化不等待 USB 完成枚举。没有改动启动地址、IAP、下载流程或保护状态。

## 收发边界

- TX 统一 abort 清理 busy、ACK 窗口及 burst；中止期间忽略回调，非活动操作的回调不能
  推进状态。主循环状态转换与 TMR/RF 回调串行化；公共 SET_RATE 也受同一临界区保护。
- RX 在 RF 接收路径统计序号，再将普通输入入队；CRC 单独累计，不占输入队列，也不
  与序号缺口重复相加。序号重复/回退不更新统计，超过半个序号周期静默后重新建立基线。
- ACK 控制绕开输入积压，在接收时间戳对应的绝对期限发送，同 token 不重排、不重复发送。
  发送前冻结内容；过期、改频道或恢复会取消 ACK。RX ACK 完成回调缺失也有 10ms 兜底。
- 普通输入不修改跳频事务的 old/target/seq。重复 PREPARE 不延长截止时间；CONFIRM
  必须匹配事务和实际频道，完成后保留重复确认能力。
- 软件待处理包带本地 radio generation，旧连接/频道的积压包不能重新激活新状态。
- 输入到 USB 之间增加 31 个有效槽的有序队列，只合并相同按键状态。端点忙或提交失败时
  保留待发报告；中立释放必须成功提交。极端边沿溢出保留旧边沿和最终状态并明确计数。
- 时间换算缓存系统时钟；按键延迟时间戳随队列项保存，避免错配到后来的按键。
- watchdog/ACK 健康检查在进入临界区后取时，避免 ISR 更新起始时间后出现无符号时间差误超时。
- ACK 完成动作也随发送内容冻结；后来的控制命令不能覆盖该 ACK 对应的跳频动作。

硬件库是黑盒，主机测试验证的是状态处理和给定事件条件下的行为；RF callback 的实际
延迟、短临界区耗时、过期中断的硬件行为、8K CPU 裕量必须通过实机记录/GPIO 波形验证。

## 诊断接口

保留既有空口、SPI 命令和 HID 帧，新增 32B `RHD1`，每 500ms 最多一帧，两个页面交替。
公共头：`0..3=RHD1`、`4..7=seq`、`8=version(1)`、`9=page`、`10=state`、`11=channel`。

| 页 | 字段（小端） |
|---|---|
| 0 | 12:u16 RF ready ms；14:u16 USB ready ms（FFFF=未就绪）；16:u16 最近连接 ms；18:u16 最近跳频握手 ms；20:u32 连接次数；24:u32 ACK watchdog 次数；28:u8 队列最高水位；29:u8 当前水位；30..31 保留 |
| 1 | 12:u32 ACK late；16:u32 同 token 抑制次数；20:u32 输入待处理队列丢弃；24:u32 按键边沿队列溢出；28:u32 CRC 总数 |

ready 时间以 SDK 时钟启动为基准，不包括其启动之前的上电时间。连接时间以 RX 本轮发现/
恢复开始为基准，不等于从外部干扰解除的时刻开始。跳频握手时间不是输入中断时间。
旧 RHM1 的 `sampleCount / elapsedMs` 仍是墙钟吞吐率，包含协议让出的空槽；不能称为纯空口丢包率。

TX 通过既有 SPI `STATE_CHANGED` 的 reason byte 发送信息性恢复上下文：
`0x81=发送 watchdog`、`0x82=ACK 健康期限`、`0x83=跳频超时`、`0x84=无法协商回退`。
命令 result/error reason 语义不变。既有 STM32 UI 不展示这些新增诊断含义，可从原始事件读取。

connect-monitor 会解析并保存 RHD1；运行中的旧 monitor 需要重启才能载入新编译的 parser。
诊断页不计入 DATA 吞吐率或频道切换事件。有 DATA 链路的 PREPARED_DUAL 显示 Connected，
实际恢复单独显示 Reconnecting；离线报告也不把正常协商跳频计为断连。
离线统计不用另开 HID 读取者，不会抢占当前 monitor：

```powershell
python tools/rf_link_report.py "$env:APPDATA/Electron/db/monitor-events.jsonl" --since-ms <测试开始的Unix毫秒>
```

输出初连、重连、主机观察到的恢复和跳频握手的 P50/P95/max、丢失样本数及最新诊断计数。
没有新诊断数据时输出 null，不会把缺失数据当作零延迟或验收通过。

## 本地验证与实机验收

```powershell
python -m unittest tools.tests.test_rf_runtime_recovery tools.tests.test_rf_link_report tools.tests.test_rf_link_reliability tools.tests.test_frozen_flash_contract -v
cd connect-monitor
npm run typecheck
npm run build
node --test tests/rf-diagnostics.test.cjs tests/hid-device-selection.test.cjs
```

主机测试执行生产策略及从当前固件抽取的函数，覆盖无完成回调中止、watchdog、回退必须
协商、逻辑窗口/重试/时钟回绕、过期/重复 ACK、DATA 穿插跳频、幂等控制、USB 提交失败和短按释放。
固件分别以固定 bond 默认模式和独立目录的正式 bond 模式编译，避免复用不同宏的对象文件。

本次本地结果：21 项 Python/原生 C 行为与契约测试、9 项 monitor 测试通过；monitor 类型检查、
前后端构建及 TX/RX 四种构建通过。TX ELF 保留原链接配置的 RWX 段提示；本次未修改链接/烧录布局。
TX 默认镜像前 4096 字节与改动前 IAP 完全一致。镜像、SHA-256 清单和源文件指纹见
`.hbox/rf-stable-20260919/`；该目录只交付固定配对、默认 8K 的匹配版本。

实机待验收矩阵：两种上电顺序和随机启动相位；1K/2K/4K/8K；auto/manual；遥测开/关；
RX 拔插、TX Off/On、遮挡恢复、单频道干扰、PREPARE/CONFIRM/ACK 丢失及单边复位。
目标为 8K 两小时无永久断连/持续积压，100 次故障恢复无需人工重启，初连 P95≤300ms、
重连 P95≤500ms（干净环境最长≤1s）、正常跳频输入中断 P95≤20ms。
外部计时基准和输入中断需用硬件时间戳/波形补齐，不能仅凭低频 HID 状态判断达标。
