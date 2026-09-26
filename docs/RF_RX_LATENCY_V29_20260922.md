# RX 输入重复重试修复 v29

本轮仅修改 RX 固件，build ID `0x1929`，继续使用空口 v5，兼容现有 v27/v28 TX 和 v27 monitor。已编译并检查 ELF；未运行回归测试、未新开设备采集、未烧录，实机效果仍待验证。

## 已有日志证据

离线读取现有 `connect-monitor/db/monitor-events.jsonl`，按 `traceId` 合并修订后的 150 条按键记录，与截图数量一致。事件时间范围为 Unix 毫秒 `1790081927776..1790082029541`。

- 149 条有本地 RX 时长，RX P50=738us、P95=1993us、最大=2987us，8 条达到 2ms。
- 截图中 2.85ms RX 对应原始 2845us；这一行 USB=102us，总延迟=3738us。
- 运行固件的 RHD1 build ID 为 `0x1927`。窗口两端原始队列丢弃和输入边沿丢弃均为零。
- 后续流水线审查确认：原始队列丢弃不包含 latest/prepared 合并，输入边沿丢弃计数器目前没有递增路径。上述零值不能作为无输入丢失的证据，详见 `RF_RX_PIPELINE_AUDIT_20260922.md`。
- RHD1 的接收回调最大值 2634us 在按键窗口之前就已存在，属于累计最大值，不能把它当作这些按键尖峰的逐事件原因。

RX 列是首条已记录事件的 RF 接收边界到首次 report-ready；USB 列是 report-ready 到 IN complete。当前日志没有逐事件的“解码被拒绝次数”或 RX 内部细分时间，所以不能证明全部尖峰都来自同一条路径。

## 确认的代码问题

`demo_queue_rx_pending_packet()` 原先对每个合法短 DATA 包执行 `++g_channel_input_serial`。主循环取出快照，执行 `short_rx_edge()` 和输入解码，然后在关中断的提交区核对这个序号。

只要解码期间收到下一包，就会序号不匹配并返回，即使下一包携带完全相同的按键状态。8K 每 125us 一包，重复包能够使一个仍然有效的按键状态反复被拒绝。首次 `short_rx_edge()` 已保存 RX 起点；后续同 tag 不会改写该起点，所以所有重试等待都会如实计入 RX 列。

此外，事件初始化和 XInput 构包之前仍包含 Flash 代码/库函数调用，延长了可被打断的路径。并非已经量出了这些调用的具体微秒开销。

## 修复内容

1. `input_serial` 改成输入状态修订号：只有三个输入字节变化（全部 18 个按键位及测量 tag），或 radio generation 变化时递增。普通包的空口序号、长度及辅助遥测变化不会使相同输入失效。
2. 主循环提交仍核对状态修订号、radio generation 和连接状态。真实新状态会拒绝旧输入；即使 A→B→A 在解码期间发生，修订号也已变化，不会把旧 A 当成从未更新。
3. 将短输入解码、首次事件建档和 XInput 构包放入 RAM；建档和构包清零使用已有 RAM 字节循环，避免调用 Flash `memset`。

没有调整 RF/USB 时间戳或扣除等待时间。接收快照仍随每包更新，首次已记录事件的 RX 起点也保持不变。

## 静态检查与构建

- `mingw32-make -C RF_PHY_Hop/RX -j8` 成功。保留原有未使用函数 `demo_queue_latency_sync_echo` 警告。
- ELF 确认 RAM 地址：`demo_zero_bytes=0x200002be`、`demo_decode_short_input_payload=0x200002ce`、`demo_build_xinput_report=0x200002f8`、`short_rx_edge=0x200005ca`、`demo_process_short_input=0x20000688`。
- 检查最终提交反汇编，radio generation / input serial / connection state 门禁仍保留。
- RAM 占用 42436/131072 字节。没有扩展中断屏蔽范围，没有将构包移进 RF ISR。
- `git diff --check` 通过；固定烧录契约文件 SHA-256 核对通过。

交付快照：`.hbox/rf-rx-latency-v29-20260922/RX.hex`，含 bin、elf、RX 源文件和 SHA-256 清单。只需更新 RX；STM32、TX 和 monitor 本轮无需更新。没有修改任何保护位、锁定状态、IAP 或烧录流程。

用户实机验证时，应先确认 RX build ID 为 `0x1929`，再用同样速率/测量开关重新观察 RX 分布。若仍有尖峰，需要进一步分解主循环等待和中断占用；本轮不宣称 2–4ms 已实机消除。
